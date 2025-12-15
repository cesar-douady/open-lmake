// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// cache format :
//	- Lru contains
//		- newer       : more recently used entry. For most  recently used, contains head. For head, contains least recently used entry.
//		- older       : less recently used entry. For least recently used, contanis head. For head, contains most  recently used entry.
//		- sz          : size of the entry. For head, total size of the cache.
//		- last_access : the last time entry was downloaded
//	- global info :
//		- LMAKE/lru : head
//	- job_dir : <job>/key-<repo_crc>-<order> where :
//		- <job> is made after its name with suffixes replaced by readable suffixes and rule idx by rule crc
//		- <repo_crc> is computed after the repo/revision, as indicated in config.caches.<name>.key
//		- <order> is either 'first' or 'last'
//	- each job has :
//		- lru info       in <job_dir>/lru
//		- meta-data      in <job_dir>/meta_data (the content of job.ancillary_file() with dep crc's instead of dep dates + target sizes)
//		- deps crcs      in <job_dir>/deps      (the deps part of meta-data for fast matching)
//		- target content in <job_dir>/data      (concatenation of all targets that can be split based on target sizes stored in meta-data)
//
// When an entry is uploaded, <order> is 'first' if it does not already exists, else it is 'last'.
// This way, each repo/revision keeps at most 2 potentially active entries : the first time it was uploaded and the last time it was uploaded.
// By default, under git the revision is the git sha1.
// This way, between 2 commits (where the git sha1 changes), we keep the first run (anticipated as the one with a base that has undergone no modif)
// and the last run (the one just before the commit).
// Hence, whether the run before a push is always saved for use by other users, while avoiding cache pollution with numberous runs of the same job
// when one is working in a repo.

// XXX? : implement timeout when locking cache (cache v1 is a proof of concept anyway)

#if !CACHE_LIGHT     // the major purpose of light implementation is to avoid loading python
	#include "py.hh" // /!\ must be included first as Python.h must be included first
#endif

#include "app.hh"
#include "msg.hh"

#include "rpc_job_exec.hh"

#include "dir_cache.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;
#if !CACHE_LIGHT         // the major purpose of light implementation is to avoid loading python
	using namespace Py ;
#endif

namespace Caches {

	::vmap_ss DirCache::descr() const {
		return {
			{ "dir_s"     ,     dir_s           }
		,	{ "file_sync" , cat(file_sync)      }
		,	{ "repo_key"  ,     repo_key .hex() }
		,	{ "max_sz"    , cat(max_sz   )      }
		,	{ "perm_ext"  , cat(perm_ext )      }
		} ;
	}

	static DirCache::Sz _entry_sz( ::string const& entry_s , FileRef sz_data_file , size_t info_sz ) {
		SWEAR( entry_s.back()=='/' , entry_s ) ;
		return
			FileInfo(sz_data_file).sz
		+	info_sz
		+	sizeof(DirCache::Lru) + 2*entry_s.size() // an estimate of the lru file size
		;
	}

	void DirCache::chk(ssize_t delta_sz) const {                                    // START_OF_NO_COV debug only
		AcFd     head_fd          { {root_fd,_lru_file(HeadS)} , {.err_ok=true} } ;
		Lru      head             ;                                                 if (+head_fd) deserialize(head_fd.read(),head) ;
		::uset_s seen             ;
		::string expected_newer_s = HeadS                               ;
		Sz       total_sz         = 0                                   ;
		for( ::string entry_s=head.older_s ; entry_s!=HeadS ;) {
			auto here = deserialize<Lru>(AcFd({root_fd,_lru_file(entry_s)}).read()) ;
			//
			bool inserted = seen.insert(entry_s).second ;
			SWEAR( inserted                       , entry_s ) ;
			SWEAR( here.newer_s==expected_newer_s , entry_s ) ;
			total_sz         += here.sz      ;
			expected_newer_s  = entry_s      ;
			entry_s           = here.older_s ;
		}
		SWEAR( head.newer_s==expected_newer_s  , HeadS                     ) ;
		SWEAR( head.sz     ==total_sz+delta_sz , head.sz,total_sz,delta_sz ) ;
	}                                                                               // END_OF_NO_COV

	#if CACHE_LIGHT                                                                        // avoid loading python, in exchange no config is necessary (used for job_exec)
		void DirCache::config( ::vmap_ss const& /*dct*/ , bool /*may_init*/ ) { FAIL() ; }
	#else
		void DirCache::config( ::vmap_ss const& dct , bool may_init ) {
			Trace trace(CacheChnl,"DirCache::config",dct.size(),STR(may_init)) ;
			//
			for( auto const& [key,val] : ::vmap_ss(dct) ) {
				try {
					switch (key[0]) {
						case 'd' : if (key=="dir") { dir_s    = with_slash(val) ; continue ; } break ;
						case 'k' : if (key=="key") { repo_key = Crc(New,val)    ; continue ; } break ;
					DN}
				} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry ",key," : ",val) ; }
				trace("bad_repo_key",key) ;
				throw cat("wrong key (",key,") in lmake.config") ;
			}
			throw_unless( +dir_s        , "dir must be specified for dir_cache" ) ;        // dir is necessary to access cache
			throw_unless( is_abs(dir_s) , "dir must be absolute for dir_cache"  ) ;
			_compile() ;
			//
			::string  config_file = ADMIN_DIR_S "config.py"                  ;
			AcFd      config_fd   { {root_fd,config_file} , {.err_ok=true} } ;
			if (+config_fd) {
				Gil gil ;
				for( auto const& [key,val] : ::vmap_ss(*py_run(config_fd.read())) ) {
					try {
						switch (key[0]) {
							case 'f' : if (key=="file_sync") { file_sync = mk_enum<FileSync>    (val) ; continue ; } break ;
							case 'i' : if (key=="inf"      ) {                                          continue ; } break ;
							case 'n' : if (key=="nan"      ) {                                          continue ; } break ;
							case 'p' : if (key=="perm"     ) { perm_ext  = mk_enum<PermExt >    (val) ; continue ; } break ;
							case 's' : if (key=="size"     ) { max_sz    = from_string_with_unit(val) ; continue ; } break ;
						DN}
					} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry ",key," : ",val) ; }
					trace("bad_cache_key",key) ;
					throw cat("wrong key (",key,") in ",dir_s+config_file) ;
				}
			}
			//
			if (!max_sz) {                                                                 // XXX> : remove when compatibility with v25.07 is no more required
				::string sz_file = ADMIN_DIR_S "size" ;
				if (FileInfo(sz_file).exists()) {
					Fd::Stderr.write(cat(sz_file," is deprecated, use size=<value> entry in ",dir_s,config_file,'\n')) ;
					try                       { max_sz = from_string_with_unit(strip(AcFd({root_fd,sz_file}).read())) ; }
					catch (::string const& e) { throw cat("cannot read ",sz_file," : ",e) ;                             }
				}
			}
			throw_unless( max_sz , "size must be specified for dir_cache ",dir_s,rm_slash," as size=<value> in ",config_file ) ;
			//
			try                     { chk_version({ .chk_version=Maybe|!may_init , .perm_ext=perm_ext , .read_only_ok=false , .version=Version::DirCache } , dir_s ) ; }
			catch (::string const&) { throw cat("version mismatch for dir_cache ",dir_s,rm_slash) ;                                                                    }
			//
		}
	#endif

	DirCache::Sz DirCache::_reserved_sz( uint64_t upload_key , NfsGuardLock& lock ) const {
		AcFd fd { {root_fd,_reserved_file(upload_key)} , {.nfs_guard=&lock} } ;
		return decode_int<Sz>(fd.read(sizeof(Sz)).data()) ;
	}

	void DirCache::_qualify_entry( RepairEntry&/*inout*/ entry , ::string const& entry_s ) const {
		try {
			if (entry.tags!=~RepairTags()) throw "incomplete"s ;
			//
			AcFd    info_fd  { {root_fd,entry_s+"info"} }                                    ;
			IMsgBuf info_buf ;
			auto    deps     = info_buf.receive<MDD    >( info_fd , No/*once*/ , {}/*key*/ ) ;                                               // deps are stored in front to ease matching
			auto    job_info = info_buf.receive<JobInfo>( info_fd , No/*once*/ , {}/*key*/ ) ;
			Sz      info_sz  = ::lseek(info_fd,0,SEEK_CUR)                                   ;
			Sz      entry_sz = _entry_sz( entry_s , {root_fd,entry_s+"sz_data"} , info_sz )  ;
			//
			throw_unless( FileInfo(info_fd).sz==info_sz , "inconsistent job_info" ) ;
			//
			try {
				::string      data = AcFd({root_fd,entry_s+"lru"}).read() ;
				::string_view view { data }                               ;
				deserialize( /*inout*/view , entry.old_lru ) ;
				throw_unless( !view , "superfluous data" ) ;
			}
			catch (::string const&) {
				entry.old_lru = {} ;                                                                                                         // avoid partial info
			}
			//
			// check coherence
			//
			throw_unless( entry.old_lru.last_access<New , "bad date" ) ;
			// XXX : check coherence between rule_crc_cmd,stems and f
			job_info.chk(true/*for_cache*/) ;
			Sz   expected_z_sz = job_info.end.total_z_sz ? job_info.end.total_z_sz : job_info.end.total_sz ;                                 // if not compressed, compressed size is not reported
			AcFd data_fd       { {root_fd,entry_s+"sz_data"} }                                             ; data_fd.read(sizeof(Sz)) ;      // skip initial max_z_sz
			Hdr  hdr           = IMsgBuf().receive<Hdr>( data_fd , Yes/*once*/ , {}/*key*/ )               ;
			Sz   actual_z_sz   = FileInfo(data_fd).sz - ::lseek(data_fd,0,SEEK_CUR)                        ;                                 // lseek is used to tell current offset
			throw_unless( hdr.target_szs.size()==job_info.end.digest.targets.size() , "inconsistent number of targets" ) ;
			throw_unless( actual_z_sz==expected_z_sz                                , "inconsistent data size"         ) ;
			//
			entry.sz = entry_sz ;
			SWEAR(+entry) ;
		} catch (::string const& e) {
			Fd::Stdout.write(cat("erase entry (",e,") : ",entry_s,rm_slash,'\n')) ;
			SWEAR(!entry) ;
		}
	}
	void DirCache::repair(bool dry_run) {
		static Re::RegExpr const entry_re { "((.*)/(\\d+-\\d+\\+)*rule-[\\dabcdef]{16}/key-[\\dabcdef]{16}-(?:first|last)/)(sz_data|deps|info|lru)" } ;
		static Re::RegExpr const key_re   {                                           "key-[\\dabcdef]{16}-(?:first|last)"                          } ;
		static Re::RegExpr const hint_re  {  "(.*)/(\\d+-\\d+\\+)*rule-[\\dabcdef]{16}/deps_hint-[\\dabcdef]{16}"                                   } ;
		//
		::umap_s<bool/*keep*/> dirs_s   { {"",true/*keep*/} , {HeadS,true/*keep*/} , {cat(HeadS,"reserved/"),true/*keep*/} } ;
		::uset_s               to_unlnk ;
		::umap_s<RepairEntry>  entries  ;
		::umap_ss              hints    ;
		//
		auto uphill = [&](::string const& d) {
			for( ::string u = d ;; u=dir_name_s(u) ) {
				auto [it,inserted] = dirs_s.try_emplace(u,true/*keep*/) ;
				if (!inserted) {
					if (it->second) break ;
					it->second = true ;
				}
				SWEAR(+u) ;
			}
		} ;
		//
		for( auto const& [af,t] : walk(File(root_fd,".")) ) {
			if (!af) continue ;
			::string f = af.substr(1) ;                                                                                                      // suppress leading /
			switch (t) {
				case FileTag::Dir :
					dirs_s.try_emplace(with_slash(::move(f)),false/*keep*/) ;
					continue ;
				break ;
				case FileTag::Reg   :
				case FileTag::Empty :
					if (f.starts_with(HeadS)) {
						::string_view sv = substr_view(f,sizeof(HeadS)-1) ;                                                                  // -1 to account for terminating null
						switch (sv[0]) {
							case 'c' : if (sv=="config.py") continue ; break ;
							case 'l' : if (sv=="lru"      ) continue ; break ;
							case 's' : if (sv=="size"     ) continue ; break ;
							case 'v' : if (sv=="version"  ) continue ; break ;
						DN}
					}
					if ( Re::Match m=entry_re.match(f) ; +m )
						if ( ::string tag{m.group(f,4)} ; can_mk_enum<RepairTag>(tag)) {
							entries[::string(m.group(f,1))].tags |= mk_enum<RepairTag>(tag) ;
							continue ;
						}
				break ;
				case FileTag::Lnk :
					if ( Re::Match m=hint_re.match(f) ; +m )
						if ( ::string k=read_lnk({root_fd,f}) ; +key_re.match(k)) {
							hints.try_emplace(f,dir_name_s(f)+k+'/') ;
							continue ;
						}
				break ;
			DN}
			to_unlnk.insert(f) ;
		}
		//
		for( auto& [f_s,e] : entries ) {
			_qualify_entry( /*inout*/e , f_s ) ;
			if (+e)
				uphill(f_s) ;
			else
				for( RepairTag t : iota(All<RepairTag>) )
					if (e.tags[t]) to_unlnk.insert(f_s+snake(t)) ;
		}
		//
		for( auto& [f,h] : hints ) {
			auto it = entries.find(h) ;
			if ( it!=entries.end() && +it->second ) uphill(dir_name_s(f)) ;
			else                                    to_unlnk.insert  (f)  ;
		}
		//
		SWEAR_PROD(+dir_s) ;                                                                                                                 // avoid unlinking random files
		for( ::string const& f : to_unlnk ) {
			Fd::Stdout.write(cat("rm ",f,'\n')) ;
			if (!dry_run) unlnk( {root_fd,f} ) ;
		}
		//
		::vector_s to_rmdir ;
		for( auto const& [d_s,k] : dirs_s ) { if (!k) to_rmdir.push_back(d_s) ; }
		::sort( to_rmdir , []( ::string const& a , ::string const& b )->bool { return a>b ; } ) ;                                            // sort to ensure subdirs are rmdir'ed before their parent
		for( ::string const& d_s : to_rmdir ) {
			Fd::Stdout.write(cat("rmdir ",d_s,rm_slash,'\n')) ;
			if (!dry_run) ::unlinkat( root_fd , d_s.c_str() , AT_REMOVEDIR ) ;
		}
		//
		::vmap_s<RepairEntry> to_mk_lru ;
		for( auto const& f_e : entries ) if (+f_e.second) to_mk_lru.push_back(f_e) ;
		::sort(                                                                                                                              // sort in LRU order, newer first
			to_mk_lru
		,	[]( ::pair_s<RepairEntry> const& a , ::pair_s<RepairEntry> const& b )->bool { return a.second.old_lru.last_access>b.second.old_lru.last_access ; }
		) ;
		Sz     total_sz = 0                                                                                                                ;
		size_t w        = ::max<size_t>( to_mk_lru , [&](auto const& n_re) { return to_short_string_with_unit(n_re.second.sz).size() ; } ) ; // too expensive to filter out only non-printed entries
		for( size_t i : iota(to_mk_lru.size()) ) {
			RepairEntry const& here     = to_mk_lru[i].second           ;
			Lru         const& old_lru  = here.old_lru                  ;
			::string           lru_file = _lru_file(to_mk_lru[i].first) ;
			Lru new_lru {
				.newer_s     = i==0                  ? HeadS : to_mk_lru[i-1].first
			,	.older_s     = i==to_mk_lru.size()-1 ? HeadS : to_mk_lru[i+1].first
			,	.sz          = here.sz
			,	.last_access = old_lru.last_access
			} ;
			total_sz += new_lru.sz ;
			if (new_lru!=old_lru) {
				Fd::Stdout.write(cat("rebuild lru (",widen(to_short_string_with_unit(new_lru.sz),w),"B, last accessed ",new_lru.last_access.str(),") to ",lru_file,'\n')) ;
				if (!dry_run) AcFd( {root_fd,lru_file} , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ).write( serialize(new_lru) ) ;
			}
		}
		::string head_lru_file = _lru_file(HeadS) ;
		Lru      old_head_lru  ;
		try                     { old_head_lru = deserialize<Lru>(AcFd({root_fd,head_lru_file}).read()) ; }
		catch (::string const&) { old_head_lru = {} ;                                                     }                                  // ensure no partial info
		Lru new_head_lru {
			.newer_s = +to_mk_lru ? to_mk_lru.back ().first : HeadS
		,	.older_s = +to_mk_lru ? to_mk_lru.front().first : HeadS
		,	.sz      = total_sz
		} ;
		if (new_head_lru!=old_head_lru) {
			Fd::Stdout.write(cat("rebuild head lru (total ",to_short_string_with_unit(new_head_lru.sz),"B) to ",head_lru_file,'\n')) ;
			if (!dry_run) AcFd({root_fd,head_lru_file},{.flags=O_WRONLY|O_TRUNC}).write(serialize(new_head_lru)) ;
		}
		//
		SWEAR_PROD( lock_file.ends_with("/lock") ) ;
		unlnk( lock_file , {.dir_ok=true,.abs_ok=true} ) ;                                                                                   // ensure no lock
	}

	void DirCache::_mk_room( Sz old_sz , Sz new_sz , NfsGuardLock& lock ) {
		Trace trace(CacheChnl,"DirCache::_mk_room",max_sz,old_sz,new_sz) ;
		if (new_sz>max_sz) {
			trace("too_large1") ;
			throw cat("cannot store entry of size ",new_sz," in cache of size ",max_sz) ;
		}
		//
		::string   head_file   = _lru_file(HeadS)                                        ;
		AcFd       head_fd     { {root_fd,head_file} , {.err_ok=true,.nfs_guard=&lock} } ;
		Lru        head        ;                                                           if (+head_fd) deserialize(head_fd.read(),head) ;
		Sz         old_head_sz = head.sz                                                 ;                                                  // for trace only
		::vector_s to_unlnk    ;                                                                                                            // delay unlink actions until all exceptions are cleared
		//
		SWEAR( head.sz>=old_sz , head.sz,old_sz ) ;                                                // total size contains old_sz
		head.sz -= old_sz ;
		while (head.sz+new_sz>max_sz) {
			lock.keep_alive() ;                                                                    // locks have limited liveness and must be refreshed regularly
			if (head.newer_s==HeadS) {
				trace("too_large2",head.sz) ;
				throw cat("cannot store entry of size ",new_sz," in cache of size ",max_sz," with ",head.sz," bytes already reserved") ;
			}
			auto here = deserialize<Lru>(AcFd({root_fd,_lru_file(head.newer_s)},{.nfs_guard=&lock}).read()) ;
			//
			trace("evict",head.sz,here.sz,head.newer_s) ;
			if (+to_unlnk) SWEAR( here.older_s==to_unlnk.back() , here.older_s,to_unlnk.back() ) ;
			else           SWEAR( here.older_s==HeadS           , here.older_s,HeadS           ) ;
			/**/           SWEAR( head.sz     >=here.sz         , head.sz     ,here.sz         ) ; // total size contains this entry
			//
			to_unlnk.push_back(::move(head.newer_s)) ;
			head.sz      -=        here.sz       ;
			head.newer_s  = ::move(here.newer_s) ;
		}
		head.sz += new_sz ;
		SWEAR( head.sz<=max_sz , head.sz,max_sz ) ;
		//
		if (+to_unlnk) {
			for( ::string const& e : to_unlnk ) {
				lock.keep_alive() ;                                                                // locks have limited liveness and must be refreshed regularly
				unlnk( {root_fd,e} , {.dir_ok=true,.nfs_guard=&lock} ) ;
			}
			if (head.newer_s==HeadS) {
				head.older_s = HeadS ;
			} else {
				::string last_file = _lru_file(head.newer_s)                                               ;
				auto     last      = deserialize<Lru>(AcFd({root_fd,last_file},{.nfs_guard=&lock}).read()) ;
				last.older_s = HeadS ;
				AcFd({root_fd,last_file},{.flags=O_WRONLY|O_TRUNC}).write(serialize(last)) ;
			}
		}
		AcFd( {root_fd,head_file} , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext,.nfs_guard=&lock} ).write( serialize(head) ) ;
		trace("total_sz",old_head_sz,"->",head.sz) ;
	}

	DirCache::Sz DirCache::_lru_remove( ::string const& entry_s , NfsGuardLock& lock ) {
		SWEAR(entry_s!=HeadS) ;
		//
		AcFd here_fd { {root_fd,_lru_file(entry_s)} , {.err_ok=true,.nfs_guard=&lock} } ; if (!here_fd) return 0 ; // nothing to remove
		auto here    = deserialize<Lru>(here_fd.read())                                 ;
		if (here.newer_s==here.older_s) {
			AcFd newer_older_fd { {root_fd,_lru_file(here.newer_s)} , {.flags=O_RDWR,.nfs_guard=&lock} } ;
			auto newer_older    = deserialize<Lru>(newer_older_fd.read())                                ;
			//
			newer_older.older_s = here.older_s ;
			newer_older.newer_s = here.newer_s ;
			//
			::lseek(newer_older_fd,0,SEEK_SET) ; newer_older_fd.write(serialize(newer_older)) ;
		} else {
			AcFd newer_fd { {root_fd,_lru_file(here.newer_s)} , {.flags=O_RDWR,.nfs_guard=&lock} } ;
			AcFd older_fd { {root_fd,_lru_file(here.older_s)} , {.flags=O_RDWR,.nfs_guard=&lock} } ;
			auto newer    = deserialize<Lru>(newer_fd.read())                                      ;
			auto older    = deserialize<Lru>(older_fd.read())                                      ;
			//
			newer.older_s = here.older_s ;
			older.newer_s = here.newer_s ;
			//
			::lseek(newer_fd,0,SEEK_SET) ; newer_fd.write(serialize(newer)) ;
			::lseek(older_fd,0,SEEK_SET) ; older_fd.write(serialize(older)) ;
		}
		return here.sz ;
	}

	void DirCache::_lru_mk_newest( ::string const& entry_s , Sz sz , NfsGuardLock& lock ) {
		SWEAR(entry_s!=HeadS) ;
		//
		AcFd head_fd { {root_fd,_lru_file(HeadS  )} , {.flags=O_RDWR                                               ,.nfs_guard=&lock} } ;
		AcFd here_fd { {root_fd,_lru_file(entry_s)} , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext,.nfs_guard=&lock} } ;
		auto head    = deserialize<Lru>(head_fd.read())                                                                                 ;
		Lru  here    { .older_s=head.older_s , .sz=sz , .last_access=New }                                                              ;
		if (head.older_s==HeadS) {
			head.older_s = entry_s ;
			head.newer_s = entry_s ;
		} else {
			AcFd newest_fd { {root_fd,_lru_file(head.older_s)} , {.flags=O_RDWR,.nfs_guard=&lock} } ;
			auto newest    = deserialize<Lru>(newest_fd.read())                                     ;
			head  .older_s = entry_s ;
			newest.newer_s = entry_s ;
			::lseek(newest_fd,0,SEEK_SET) ; newest_fd.write(serialize(newest)) ;
		}
		::lseek(head_fd,0,SEEK_SET) ; head_fd.write(serialize(head)) ;
		/**/                          here_fd.write(serialize(here)) ;
	}

	void DirCache::_dismiss( uint64_t upload_key , Sz sz , NfsGuardLock& lock ) {
		_mk_room( sz , 0 , lock ) ;
		unlnk( {root_fd,_reserved_file(upload_key)} , {.nfs_guard=&lock} ) ;
	}

	static ::string _mk_crc(DirCache::MDD const& deps) {
		Xxh xxh { New , NodeIdx(deps.size()) } ;
		for( auto const& [n,dd] : deps ) {
			xxh += n           ;
			xxh += dd.accesses ;
			xxh += dd.crc()    ;
		}
		return xxh.digest().hex() ;
	}

	::pair_s/*key*/<DirCache::DownloadDigest> DirCache::_sub_match( ::string const& job , MDD const& repo_deps , bool for_commit , NfsGuardLock& lock ) const {
		Trace trace(CacheChnl,"DirCache::_sub_match",job) ;
		//
		AcFd     dfd   { {root_fd,job} , {.flags=O_RDONLY|O_DIRECTORY,.err_ok=true,.nfs_guard=&lock} } ; if (!dfd) return { {}/*key*/ , {.hit_info=CacheHitInfo::NoRule} } ;
		//
		::vector_s/*lazy*/                      repos         ;
		::string                                matching_key  ;
		::optional<MDD>                         matching_deps ;
		::optional<::umap_s<DepDigest>>/*lazy*/ repo_dep_map  ;                                         // map used when not in order
		bool                                    truncated     = false                ;                  // if true <= matching_deps has been truncated because of multi-match
		CacheHitInfo                            miss          = CacheHitInfo::NoRule ;                  // used only when miss
		::string                                hint_key      ;
		//
		for( ssize_t candidate=-1 ;; candidate++ ) {                                                    // candidate==-1 means try deps_hint
			::string key ;
			switch (candidate) {
				case -1 :
					key = read_lnk( {dfd,"deps_hint-"+_mk_crc(repo_deps)} ) ;                           // may point to the right entry (hint only as link is not updated when entry is modified)
					if (!key) continue ;
					SWEAR( key.starts_with("key-") && key.find('/')==Npos , key ) ;                     // fast check
					hint_key = key ;
				break ;
				case 0 :
					repos = lst_dir_s(dfd) ;                                                            // solve lazy
				[[fallthrough]] ;
				default :
					if (candidate>=ssize_t(repos.size())) goto Epilog ;                                 // seen all candidates
					key = ::move(repos[candidate]) ;
					if (!key.starts_with("key-")) continue ;                                            // not an entry
					if (key==hint_key           ) continue ;                                            // already processed
			}
			bool     in_order   = true                     ;                                            // first try in order match, then revert to name based match
			size_t   idx        = 0                        ;                                            // index in repo_deps used when in order, maintain count when not in order
			bool     hit        = true                     ;
			size_t   dvg        = 0                        ;                                            // first index in cache_deps not found in repo_deps/repo_dep_map
			::string deps_file  = cat(job,'/',key,"/info") ;
			IMsgBuf  cache_buf  ;
			//
			AcFd fd         ; try { fd         = AcFd({root_fd,deps_file},{.nfs_guard=&lock})       ; } catch (::string const& e) { trace("no_deps" ,deps_file,e) ; continue ; }
			MDD  cache_deps ; try { cache_deps = cache_buf.receive<MDD>(fd,Maybe/*once*/,{}/*key*/) ; } catch (::string const& e) { trace("bad_deps",deps_file,e) ; continue ; }
			//
			miss = CacheHitInfo::BadDeps ;
			//
			lock.keep_alive() ;                                                                         // locks have limited liveness and must be refreshed regularly
			//
			for( auto& [dn,dd] : cache_deps ) {
				DepDigest const* repo_dd  ;
				if (in_order) {
					if ( idx<repo_deps.size() && dn==repo_deps[idx].first ) {
						repo_dd = &repo_deps[idx].second ;
						goto FoundDep ;
					}
					in_order = false ;
					if (!repo_dep_map) repo_dep_map.emplace(mk_umap(repo_deps)) ;                       // solve lazy
				}
				if ( auto it = repo_dep_map->find(dn) ; it!=repo_dep_map->end() ) {
					repo_dd = &it->second ;
					goto FoundDep ;
				}
				hit = false ;                                                                           // this entry is not found, no more a hit, but search must continue
				continue ;
			FoundDep :
				if (!dd.crc().match(repo_dd->crc(),dd.accesses)) {
					trace("miss",dn,dd.accesses,dd.crc(),repo_dd->crc()) ;
					goto Miss ;
				}
				/**/     idx++ ;                                                                        // count entries even when not in order for early break
				if (hit) dvg++ ;
			}
			if (hit) {
				auto job_info = cache_buf.receive<JobInfo>(fd,No/*once*/,{}/*key*/) ;
				job_info.end.digest.deps = ::move(cache_deps) ;                                         // deps are stored upfront to ease matching
				return { ::move(key) , { .hit_info=CacheHitInfo::Hit , .job_info=::move(job_info) } } ;
			} else if (for_commit) {
				continue ;                                                                              // deps are not necessary for commit
			}
			//
			for( NodeIdx i : iota(dvg,cache_deps.size()) ) {                                            // stop recording deps at the first unmatched critical dep
				if (!cache_deps[i].second.dflags[Dflag::Critical]) continue ;
				//
				if (in_order)   SWEAR( idx>=repo_deps.size() , idx,repo_deps ) ;                        // if still in_order, in order entries must have been exhausted, nothing to check
				else          { if (repo_dep_map->contains(cache_deps[i].first)) continue ; }           // if dep was known, it was ok
				//
				cache_deps.resize(i+1) ;
				break ;
			}
			if (+matching_deps) {                                                                       // if several entries match, only keep deps that are needed for all of them ...
				::uset_s names = mk_key_uset(cache_deps) ;                                              // ... so as to prevent making useless deps at the risk of loss of parallelism
				truncated |= ::erase_if(
					*matching_deps
				,	[&](auto const& n_dd) { return !names.contains(n_dd.first) ; }
				) ;
			} else {
				matching_key  = ::move(key       ) ;                                                    // any key is ok, first is least expensive
				matching_deps = ::move(cache_deps) ;
				for( auto& [_,dd] : *matching_deps ) dd.del_crc() ;                                     // defensive programming : dont keep useless crc as job will rerun anyway
			}
		Miss : ;
		}
	Epilog :
		if (for_commit    ) return { {}/*key*/ , {.hit_info=CacheHitInfo::Miss} } ;                                 // for commit, we only need to know it did not hit
		if (+matching_deps) {
			bool has_new_deps =                                                                                     // avoid loop by guaranteeing new deps when we return hit=Maybe
				!truncated                                                                                          // if not truncated, new deps are guaranteed, no need to search
			||	!repo_dep_map                                                                                       // if not repo_dep_map, repo_deps has been exhausted for all matching entries
			||	::any_of( *matching_deps , [&](auto const& n_dd) { return !repo_dep_map->contains(n_dd.first) ; } )
			;
			if (has_new_deps) {
				JobInfo job_info ; job_info.end.digest.deps = ::move(*matching_deps) ;
				return { {}/*key*/ , { .hit_info=CacheHitInfo::Match , .job_info=job_info } } ;
			}
			trace("no_new_deps") ;
		} else {
			trace("no_matching") ;
		}
		return { {}/*key*/ , {.hit_info=miss} } ;
	}

	::pair<Cache::DownloadDigest,AcFd> DirCache::sub_download( ::string const& job , MDD const& repo_deps ) {
		Trace trace(CacheChnl,"DirCache::sub_download",job) ;
		//
		::pair<DownloadDigest,AcFd> res ;
		//
		NfsGuardLock lock { file_sync , lock_file , {.perm_ext=perm_ext} } ; trace("locked") ;
		::string     key  ;
		::tie(key,res.first) = _sub_match( job , repo_deps , false/*for_commit*/ , lock ) ; trace("hit_info",res.first.hit_info) ;
		//
		if (res.first.hit_info==CacheHitInfo::Hit) {               // download if hit
			::string job_key_s = cat(job,'/',key,'/')            ;
			Sz       sz        = _lru_remove( job_key_s , lock ) ; throw_if( !sz , "no entry ",job_key_s ) ; trace("step1") ;
			_lru_mk_newest( job_key_s , sz , lock ) ;                                                        trace("step2") ;
			res.second = AcFd({root_fd,cat(job_key_s,"sz_data")},{.nfs_guard=&lock}) ; ::lseek( res.second , sizeof(Sz) , SEEK_SET ) ;
			trace("done") ;
		}
		return res ;
	}

	::pair<uint64_t/*upload_key*/,AcFd> DirCache::sub_upload(Sz reserved_sz) {
		Trace trace(CacheChnl,"DirCache::sub_upload",reserved_sz) ;
		//
		{	NfsGuardLock lock { file_sync , lock_file , {.perm_ext=perm_ext}} ; trace("locked") ;      // lock for minimal time
			_mk_room( 0 , reserved_sz , lock ) ;
		}
		uint64_t upload_key = ::max( random<uint64_t>() , uint64_t(1) ) ;                              // reserve 0 for no upload_key
		::pair<uint64_t/*upload_key*/,AcFd> res {
			upload_key
		,	AcFd( {root_fd,_reserved_file(upload_key)} , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444} ) // will be moved to permanent storage
		} ;
		::string sz_str ( sizeof(Sz) ,0 ) ; encode_int<Sz>(sz_str.data(),reserved_sz) ;
		res.second.write(sz_str) ;
		trace("done",res) ;
		return res ;
	}

	void DirCache::sub_commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		Trace trace(CacheChnl,"DirCache::sub_commit",upload_key,job) ;
		// START_OF_VERSIONING DIR_CACHE
		::string deps_str     = serialize(job_info.end.digest.deps) ;
		::string job_info_str = serialize(job_info                ) ;
		// END_OF_VERSIONING
		::string       deps_hint = cat(job,"/deps_hint-",_mk_crc(job_info.end.digest.deps)) ;                   // deps_hint is hint only, hence no versioning
		NfsGuardLock   lock      { file_sync , lock_file , {.perm_ext=perm_ext} }           ; trace("locked") ; // lock as late as possible
		::string       key       ;
		DownloadDigest digest    ;
		Sz             old_sz    = _reserved_sz(upload_key,lock)                            ;
		//
		::tie(key,digest) = _sub_match( job , job_info.end.digest.deps , true/*for_commit*/ , lock ) ;
		//
		if (digest.hit_info==CacheHitInfo::Hit) {                                                               // dont populate if a matching entry appeared while the job was running
			trace("hit",key) ;
			::umap_s<::pair<Crc/*cache*/,Crc/*repo*/>> diff_targets ;
			for( auto const& [key,td] : job_info       .end.digest.targets )                    diff_targets.try_emplace( key , Crc::None,td.crc    ) ;
			for( auto const& [key,td] : digest.job_info.end.digest.targets ) { auto [it,insd] = diff_targets.try_emplace( key , td.crc   ,Crc::None ) ; if (!insd) it->second.first = td.crc ; }
			::string msg               ;
			size_t   w                 = 0                   ;
			::string only_in_repo      = "only in repo"      ;
			::string only_in_cache     = "only in cache"     ;
			::string different_content = "different content" ;
			for( auto const& [key,cache_repo] : diff_targets ) {
				if (cache_repo.first==cache_repo.second) continue ;
				if      (cache_repo.first ==Crc::None) w = ::max( w , only_in_repo     .size() ) ;
				else if (cache_repo.second==Crc::None) w = ::max( w , only_in_cache    .size() ) ;
				else                                   w = ::max( w , different_content.size() ) ;
			}
			for( auto const& [key,cache_repo] : diff_targets ) {
				if (cache_repo.first==cache_repo.second) continue ;
				if      (cache_repo.first ==Crc::None) msg << widen(only_in_repo     ,w)<<" : "<<key<<'\n' ;
				else if (cache_repo.second==Crc::None) msg << widen(only_in_cache    ,w)<<" : "<<key<<'\n' ;
				else                                   msg << widen(different_content,w)<<" : "<<key<<'\n' ;
			}
			_dismiss( upload_key , old_sz , lock ) ;                                                            // finally, we did not populate
			trace("throw_if",w,msg) ;
			throw_if( w , msg ) ;
		} else {
			key = cat( "key-" , repo_key.hex() ) ; key += FileInfo({root_fd,cat(job,'/',key,"-first/lru")}).exists() ? "-last" : "-first" ;
			trace("key",key) ;
			//
			::string jnid_s = cat(job,'/',key,'/') ;
			mk_dir_s( {root_fd,jnid_s} , {.perm_ext=perm_ext,.nfs_guard=&lock} ) ;
			//
			OMsgBuf info_buf ;
			info_buf.add(job_info.end.digest.deps) ; job_info.end.digest.deps = {} ;                            // deps are stored upfront to ease matching
			info_buf.add(job_info                ) ;
			AcFd dfd       { {root_fd,jnid_s} , {.flags=O_RDONLY|O_DIRECTORY,.nfs_guard=&lock} }                       ;
			Sz   new_sz    = _entry_sz( jnid_s , lock.access({root_fd,_reserved_file(upload_key)}) , info_buf.size() ) ;
			bool made_room = false                                                                                     ;
			bool unlnked   = false                                                                                     ;
			try {
				trace("upload",key,new_sz) ;
				old_sz += _lru_remove( jnid_s , lock ) ;
				unlnk_inside_s(dfd)                ; unlnked   = true ;
				_mk_room( old_sz , new_sz , lock ) ; made_room = true ;
				// store meta-data and data
				// START_OF_VERSIONING DIR_CACHE
				info_buf.send( AcFd(File(dfd,"info"),{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444,.perm_ext=perm_ext,.nfs_guard=&lock}) , {}/*key*/ ) ;
				rename( {root_fd,_reserved_file(upload_key)}/*src*/ , File(dfd,"sz_data")/*dst*/ , {.nfs_guard=&lock} ) ;
				// END_OF_VERSIONING
				_lru_mk_newest( jnid_s , new_sz , lock ) ;
				//
			} catch (::string const& e) {
				trace("failed",e) ;
				if (!unlnked) unlnk_inside_s(dfd) ;                                                             // clean up in case of partial execution
				_dismiss( upload_key , made_room?new_sz:old_sz , lock ) ;                                       // finally, we did not populate the entry
				trace("throw") ;
				throw ;
			}
		}
		unlnk  ( {root_fd,deps_hint} , {.abs_ok=true,.nfs_guard=&lock} ) ;
		sym_lnk( {root_fd,deps_hint} , key )                             ; // set a symlink from a name derived from deps to improve match speed in case of hit (hint only so target may be updated)
		trace("done") ;
	}

	void DirCache::sub_dismiss(uint64_t upload_key) {
		Trace trace(CacheChnl,"DirCache::sub_dismiss",upload_key) ;
		NfsGuardLock lock { file_sync , lock_file , {.perm_ext=perm_ext} } ; trace("locked") ;
		_dismiss( upload_key , _reserved_sz(upload_key,lock) , lock ) ;
		trace("done") ;
	}

}
