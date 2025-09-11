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

#include "app.hh"

#include "dir_cache.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

namespace Caches {

	static DirCache::Sz _entry_sz( ::string const& entry_s , ::string const& data_file , size_t deps_sz , size_t info_sz ) {
		SWEAR( entry_s.back()=='/' , entry_s ) ;
		return
			FileInfo(data_file).sz
		+	deps_sz
		+	info_sz
		+	sizeof(DirCache::Lru) + 2*entry_s.size() // an estimate of the lru file size
		;
	}

	void DirCache::chk(ssize_t delta_sz) const {                               // START_OF_NO_COV debug only
		AcFd     head_fd          { _lru_file(HeadS) , true/*err_ok*/ } ;
		Lru      head             ;                                       if (+head_fd) deserialize(head_fd.read(),head) ;
		::uset_s seen             ;
		::string expected_newer_s = HeadS                               ;
		Sz       total_sz         = 0                                   ;
		for( ::string entry_s=head.older_s ; entry_s!=HeadS ;) {
			auto here = deserialize<Lru>(AcFd(_lru_file(entry_s)).read()) ;
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
	}                                                                          // END_OF_NO_COV

	void DirCache::config( ::vmap_ss const& dct , bool may_init ) {
		Trace trace("DirCache::config",dct.size(),STR(may_init)) ;
		for( auto const& [key,val] : dct ) {
			try {
				switch (key[0]) {
					case 'd' : if (key=="dir"      ) { dir_s     = with_slash       (val) ; continue ; } break ;
					case 'f' : if (key=="file_sync") { file_sync = mk_enum<FileSync>(val) ; continue ; } break ;
					case 'k' : if (key=="key"      ) { key_crc   = Crc(New          ,val) ; continue ; } break ;
					case 'p' : if (key=="perm"     ) { perm_ext  = mk_enum<PermExt >(val) ; continue ; } break ;
				DN}
			} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry "    ,key,": ",val) ; }
			/**/                        { trace("bad_key",key    ) ; throw cat("unexpected config entry : ",key         ) ; }
		}
		_compile() ;
		//
		::string sz_file = admin_dir_s+"size"         ;
		AcFd     sz_fd   { sz_file , true/*err_ok*/ } ; throw_unless( +sz_fd , "file must exist and contain the size of the cache : ",sz_file ) ;
		try                       { max_sz = from_string_with_unit(strip(sz_fd.read())) ; }
		catch (::string const& e) { throw cat("cannot read ",sz_file," : ",e) ;           }
		//
		try                     { chk_version( may_init , admin_dir_s , perm_ext) ;         }
		catch (::string const&) { throw "version mismatch for dir_cache "+no_slash(dir_s) ; }
		//
		mk_dir_s( admin_dir_s+"reserved/" , perm_ext ) ;
		AcFd( lock_file , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ) ;
	}

	DirCache::Sz DirCache::_reserved_sz( uint64_t upload_key , NfsGuard& nfs_guard ) const {
		return deserialize<Sz>(AcFd(nfs_guard.access(_reserved_file(upload_key,"sz"))).read()) ;
	}

	template<class T> T _full_deserialize( size_t&/*out*/ sz , ::string const& file ) {
		::string      data = AcFd(file).read()             ;
		::string_view view { data }                        ;
		T             res  = deserialize<T>(/*inout*/view) ;
		throw_unless( !view , "superfluous data" ) ;
		sz = data.size() ;
		return res ;
	}
	void DirCache::_qualify_entry( RepairEntry&/*inout*/ entry , ::string const& entry_s ) const {
		try {
			if (entry.tags!=~RepairTags()) throw "incomplete"s ;
			//
			::string df_s     = dir_s + entry_s                                                        ;
			size_t   deps_sz  ;
			size_t   info_sz  ;
			auto     deps     = _full_deserialize<::vmap_s<DepDigest>>( /*out*/deps_sz , df_s+"deps" ) ;
			auto     job_info = _full_deserialize<JobInfo            >( /*out*/info_sz , df_s+"info" ) ;
			Sz       entry_sz = _entry_sz( entry_s , df_s+"data" , deps_sz , info_sz )                 ;
			//
			try                     { entry.old_lru = _full_deserialize<Lru>( ::ref(size_t()) , df_s+"lru" ) ; }                             // lru file size is already estimated
			catch (::string const&) { entry.old_lru = {}                                                     ; }                             // avoid partial info
			//
			// check coherence
			//
			throw_unless( entry.old_lru.last_access<New  , "bad date" ) ;
			throw_unless( deps==job_info.end.digest.deps , "bad deps" ) ;
			// XXX : check coherence between rule_crc_cmd,stems and f
			job_info.chk(true/*for_cache*/) ;
			throw_unless( FileInfo(df_s+"data").sz!=job_info.end.compressed_sz , "bad data size" ) ;
			//
			entry.sz = entry_sz ;
			SWEAR(+entry) ;
		} catch (::string const& e) {
			Fd::Stdout.write(cat("erase entry (",e,") : ",no_slash(entry_s),'\n')) ;
			SWEAR(!entry) ;
		}
	}
	void DirCache::repair(bool dry_run) {
		static Re::RegExpr const entry_re { "((.*)/(\\d+-\\d+\\+)*rule-[\\dabcdef]{16}/key-[\\dabcdef]{16}-(?:first|last)/)(data|deps|info|lru)" } ;
		static Re::RegExpr const key_re   {                                           "key-[\\dabcdef]{16}-(?:first|last)"                       } ;
		static Re::RegExpr const hint_re  {  "(.*)/(\\d+-\\d+\\+)*rule-[\\dabcdef]{16}/deps_hint-[\\dabcdef]{16}"                                } ;
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
		for( auto const& [af,t] : walk(dir_s) ) {
			if (!af) continue ;
			::string f = af.substr(1) ;                                                                                                      // suppress leading /
			switch (t) {
				case FileTag::Dir :
					dirs_s.try_emplace(with_slash(f),false/*keep*/) ;
					continue ;
				break ;
				case FileTag::Reg   :
				case FileTag::Empty :
					if (f.starts_with(HeadS)) {
						::string_view sv = substr_view(f,sizeof(HeadS)-1) ;                                                                  // -1 to account for terminating null
						if (sv=="lru"    ) continue ;
						if (sv=="size"   ) continue ;
						if (sv=="version") continue ;
					}
					if ( Re::Match m=entry_re.match(f) ; +m )
						if ( ::string tag{m.group(f,4)} ; can_mk_enum<RepairTag>(tag)) {
							entries[::string(m.group(f,1))].tags |= mk_enum<RepairTag>(tag) ;
							continue ;
						}
				break ;
				case FileTag::Lnk :
					if ( Re::Match m=hint_re.match(f) ; +m )
						if ( ::string k=read_lnk(dir_s+f) ; +key_re.match(k)) {
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
			::string df = dir_s+f ;
			Fd::Stdout.write(cat("rm ",df,'\n')) ;
			if (!dry_run) unlnk( df , false/*dir_ok*/ , true/*abs_ok*/ ) ;
		}
		//
		::vector_s to_rmdir ;
		for( auto const& [d_s,k] : dirs_s ) { if (!k) to_rmdir.push_back(d_s) ; }
		::sort( to_rmdir , []( ::string const& a , ::string const& b )->bool { return a>b ; } ) ;                                            // sort to ensure subdirs are rmdir'ed before their parent
		for( ::string const& d_s : to_rmdir ) {
			::string dd = no_slash(dir_s+d_s) ;
			Fd::Stdout.write(cat("rmdir ",dd,'\n')) ;
			if (!dry_run) ::rmdir(dd.c_str()) ;
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
				if (!dry_run) AcFd( lru_file , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ).write(serialize(new_lru)) ;
			}
		}
		::string head_lru_file = _lru_file(HeadS) ;
		Lru      old_head_lru  ;
		try                     { old_head_lru = deserialize<Lru>(AcFd(head_lru_file).read()) ; }
		catch (::string const&) { old_head_lru = {} ;                                           }                                            // ensure no partial info
		Lru new_head_lru {
			.newer_s = +to_mk_lru ? to_mk_lru.back ().first : HeadS
		,	.older_s = +to_mk_lru ? to_mk_lru.front().first : HeadS
		,	.sz      = total_sz
		} ;
		if (new_head_lru!=old_head_lru) {
			Fd::Stdout.write(cat("rebuild head lru (total ",to_short_string_with_unit(new_head_lru.sz),"B) to ",head_lru_file,'\n')) ;
			if (!dry_run) AcFd(head_lru_file,{.flags=O_WRONLY|O_TRUNC}).write(serialize(new_head_lru)) ;
		}
	}

	void DirCache::_mk_room( Sz old_sz , Sz new_sz , NfsGuard& nfs_guard ) {
		Trace trace("DirCache::_mk_room",max_sz,old_sz,new_sz) ;
		if (new_sz>max_sz) {
			trace("too_large1") ;
			throw cat("cannot store entry of size ",new_sz," in cache of size ",max_sz) ;
		}
		//
		::string   head_file   = _lru_file(HeadS)                               ;
		AcFd       head_fd     { nfs_guard.access(head_file) , true/*err_ok*/ } ;
		Lru        head        ;                                                  if (+head_fd) deserialize(head_fd.read(),head) ;
		Sz         old_head_sz = head.sz                                        ;                                                  // for trace only
		::vector_s to_unlnk    ;                                                                                                   // delay unlink actions until all exceptions are cleared
		//
		SWEAR( head.sz>=old_sz , head.sz,old_sz ) ;                                                                                // total size contains old_sz
		head.sz -= old_sz ;
		while (head.sz+new_sz>max_sz) {
			if (head.newer_s==HeadS) {
				trace("too_large2",head.sz) ;
				throw cat("cannot store entry of size ",new_sz," in cache of size ",max_sz," with ",head.sz," bytes already reserved") ;
			}
			auto here = deserialize<Lru>(AcFd(nfs_guard.access(_lru_file(head.newer_s))).read()) ;
			//
			trace("evict",head.sz,here.sz,head.newer_s) ;
			if (+to_unlnk) SWEAR( here.older_s==to_unlnk.back() , here.older_s,to_unlnk.back() ) ;
			else           SWEAR( here.older_s==HeadS           , here.older_s,HeadS           ) ;
			/**/           SWEAR( head.sz     >=here.sz         , head.sz     ,here.sz         ) ;                                 // total size contains this entry
			//
			to_unlnk.push_back(::move(head.newer_s)) ;
			head.sz      -=        here.sz       ;
			head.newer_s  = ::move(here.newer_s) ;
		}
		head.sz += new_sz ;
		SWEAR( head.sz<=max_sz , head.sz,max_sz ) ;
		//
		if (+to_unlnk) {
			for( ::string const& e : to_unlnk ) unlnk( nfs_guard.change(dir_s+e) , true/*dir_ok*/ , true/*abs_ok*/ ) ;
			if (head.newer_s==HeadS) {
				head.older_s = HeadS ;
			} else {
				::string last_file = _lru_file(head.newer_s)                                    ;
				auto     last      = deserialize<Lru>(AcFd(nfs_guard.access(last_file)).read()) ;
				last.older_s = HeadS ;
				AcFd(last_file,{.flags=O_WRONLY|O_TRUNC}).write(serialize(last)) ;
			}
		}
		AcFd( nfs_guard.change(dir_guard(head_file,perm_ext)) , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ).write(serialize(head)) ;
		trace("total_sz",old_head_sz,"->",head.sz) ;
	}

	DirCache::Sz DirCache::_lru_remove( ::string const& entry_s , NfsGuard& nfs_guard ) {
		SWEAR(entry_s!=HeadS) ;
		//
		AcFd here_fd { nfs_guard.access(_lru_file(entry_s)) , true/*err_ok*/ } ; if (!here_fd) return 0 ; // nothing to remove
		auto here    = deserialize<Lru>(here_fd.read())                        ;
		if (here.newer_s==here.older_s) {
			::string newer_older_file = _lru_file(here.newer_s)                                           ;
			auto     newer_older      = deserialize<Lru>(AcFd(nfs_guard.access(newer_older_file)).read()) ;
			//
			newer_older.older_s = here.older_s ;
			newer_older.newer_s = here.newer_s ;
			//
			AcFd(nfs_guard.change(newer_older_file),{.flags=O_WRONLY|O_TRUNC}).write(serialize(newer_older)) ;
		} else {
			::string newer_file = _lru_file(here.newer_s)                                     ;
			::string older_file = _lru_file(here.older_s)                                     ;
			auto     newer      = deserialize<Lru>(AcFd(nfs_guard.access(newer_file)).read()) ;
			auto     older      = deserialize<Lru>(AcFd(nfs_guard.access(older_file)).read()) ;
			//
			newer.older_s = here.older_s ;
			older.newer_s = here.newer_s ;
			//
			AcFd(nfs_guard.change(newer_file),{.flags=O_WRONLY|O_TRUNC}).write(serialize(newer)) ;
			AcFd(nfs_guard.change(older_file),{.flags=O_WRONLY|O_TRUNC}).write(serialize(older)) ;
		}
		return here.sz ;
	}

	void DirCache::_lru_mk_newest( ::string const& entry_s , Sz sz , NfsGuard& nfs_guard ) {
		SWEAR(entry_s!=HeadS) ;
		//
		::string head_file = _lru_file(HeadS)                                           ;
		auto     head      = deserialize<Lru>(AcFd(nfs_guard.access(head_file)).read()) ;
		::string here_file = _lru_file(entry_s)                                         ;
		Lru      here      { .older_s=head.older_s , .sz=sz , .last_access=New }        ;
		if (head.older_s==HeadS) {
			head.older_s = entry_s ;
			head.newer_s = entry_s ;
		} else {
			::string newest_file = _lru_file(head.older_s)                                      ;
			auto     newest      = deserialize<Lru>(AcFd(nfs_guard.access(newest_file)).read()) ;
			head  .older_s = entry_s ;
			newest.newer_s = entry_s ;
			AcFd( nfs_guard.change(newest_file) , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ).write(serialize(newest)) ;
		}
		AcFd( nfs_guard.change(head_file) , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ).write(serialize(head)) ;
		AcFd( nfs_guard.change(here_file) , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=perm_ext} ).write(serialize(here)) ;
	}

	void DirCache::_dismiss( uint64_t upload_key , Sz sz , NfsGuard& nfs_guard ) {
		_mk_room( sz , 0 , nfs_guard ) ; //!                        dir_ok  abs_ok
		unlnk( nfs_guard.change(_reserved_file(upload_key,"sz"  )) , false , true ) ;
		unlnk( nfs_guard.change(_reserved_file(upload_key,"data")) , false , true ) ;
	}

	static ::string _mk_crc(::vmap_s<DepDigest> const& deps) {
		Xxh xxh { New , NodeIdx(deps.size()) } ;
		for( auto const& [n,dd] : deps ) {
			xxh += n           ;
			xxh += dd.accesses ;
			xxh += dd.crc()    ;
		}
		return xxh.digest().hex() ;
	}

	::pair_s<Cache::Match> DirCache::_sub_match( ::string const& job , ::vmap_s<DepDigest> const& repo_deps ) const {
		Trace trace("DirCache::_sub_match",job) ;
		//
		NfsGuard nfs_guard { file_sync }                                                                         ;
		::string abs_jn_s  = dir_s+job+'/'                                                                       ;
		AcFd     dfd       { nfs_guard.access_dir_s(abs_jn_s) , true/*err_ok*/ , {.flags=O_RDONLY|O_DIRECTORY} } ; if (!dfd) return {{},{.hit=No}} ;
		//
		::vector_s/*lazy*/                      repos         ;
		::optional<::vmap_s<DepDigest>>         matching_deps ;
		::optional<::umap_s<DepDigest>>/*lazy*/ repo_dep_map  ;                                              // map used when not in order
		bool                                    truncated     = false                ;                       // if true <= matching_deps has been truncated because of multi-match
		CacheHitInfo                            hit_info      = CacheHitInfo::NoRule ;
		for( ssize_t candidate=-1 ;; candidate++ ) {                                                         // candidate==-1 means try deps_hint
			::string r ;
			switch (candidate) {
				case -1 :
					r = read_lnk( dfd , "deps_hint-"+_mk_crc(repo_deps) ) ;                                  // may point to the right entry (hint only as link is not updated when entry is modified)
					if (!r) continue ;
					SWEAR( r.starts_with("key-") && r.find('/')==Npos , r ) ;                                // fast check
				break ;
				case 0 :
					repos = lst_dir_s(dfd) ;                                                                 // solve lazy
				[[fallthrough]] ;
				default :
					/**/                           if (candidate>=ssize_t(repos.size())) goto Epilog ;       // seen all candidates
					r = ::move(repos[candidate]) ; if (!r.starts_with("key-")          ) continue    ;       // not an entry
			}
			::string                    deps_file    = abs_jn_s+r+"/deps" ;
			AcFd                        fd           ;                      try { fd = AcFd(nfs_guard.access(deps_file)) ; } catch (::string const& e) { trace("no_deps" ,deps_file,e) ; continue ; }
			::vmap_s<DepDigest>         cache_deps   ;                      try { deserialize(fd.read(),cache_deps) ;      } catch (::string const& e) { trace("bad_deps",deps_file,e) ; continue ; }
			bool                        in_order     = true               ;                                  // first try in order match, then revert to name based match
			size_t                      idx          = 0                  ;                                  // index in repo_deps used when in order, maintain count when not in order
			bool                        hit          = true               ;
			size_t                      dvg          = 0                  ;                                  // first index in cache_deps not found in repo_deps/repo_dep_map
			//
			hit_info = CacheHitInfo::BadDeps ;                                                               // used only when not hit
			//
			for( auto const& [dn,dd] : cache_deps ) {
				DepDigest const* repo_dd ;
				if (idx>=repo_deps.size()) { hit = false ; break ; }                                         // repo_deps is exhausted, no more need to search for mismatch
				if (in_order) {
					if (dn==repo_deps[idx].first) {
						repo_dd = &repo_deps[idx].second ;
						goto FoundDep ;
					}
					in_order = false ;
					if (!repo_dep_map) repo_dep_map.emplace(mk_umap(repo_deps)) ;                            // solve lazy
				}
				if ( auto it=repo_dep_map->find(dn) ; it!=repo_dep_map->end() )   repo_dd = &it->second ;
				else                                                            { hit = false ; continue ; } // this entry is not found, no more a hit, but search must continue
			FoundDep :
				if (!dd.crc().match(repo_dd->crc(),dd.accesses)) goto Miss ;
				//
				/**/     idx++ ;                                                                             // count entries even when not in order or early break
				if (hit) dvg++ ;
			}
			if (hit) return { r , { .hit=Yes , .hit_info=CacheHitInfo::Hit , .key=cat(job,'/',r,'/') } } ;
			//
			for( NodeIdx i : iota(dvg,cache_deps.size()) ) {                                                 // stop recording deps at the first unmatched critical dep
				if (!cache_deps[i].second.dflags[Dflag::Critical]) continue ;
				//
				if (in_order)   SWEAR( idx>=repo_deps.size() , idx,repo_deps ) ;                             // if still in_order, in order entries must have been exhausted, nothing to check
				else          { if (repo_dep_map->contains(cache_deps[i].first)) continue ; }                // if dep was known, it was ok
				//
				cache_deps.resize(i+1) ;
				break ;
			}
			if (+matching_deps) {                                                                            // if several entries match, only keep deps that are needed for all of them ...
				::uset_s names = mk_key_uset(cache_deps) ;                                                   // ... so as to prevent making useless deps at the risk of loss of parallelism
				truncated |= ::erase_if(
					*matching_deps
				,	[&](auto const& n_dd) { return !names.contains(n_dd.first) ; }
				) ;
			} else {
				matching_deps = ::move(cache_deps) ;
				for( auto& [_,dd] : *matching_deps ) dd.del_crc() ;                                          // defensive programming : dont keep useless crc as job will rerun anyway
			}
		Miss : ;
		}
	Epilog :
		if (+matching_deps) {
			bool has_new_deps =                                                                              // avoid loop by guaranteeing new deps when we return hit=Maybe
				!truncated                                                                                   // if not truncated, new deps are guaranteed, no need to search
			||	!repo_dep_map                                                                                // if not repo_dep_map, repo_deps has been exhausted for all matching entries
			||	::any_of(
					*matching_deps
				,	[&](auto const& n_dd) { return !repo_dep_map->contains(n_dd.first) ; }
				)
			;
			if (has_new_deps) return { {} , { .hit=Maybe , .hit_info=hit_info , .deps{::move(*matching_deps)} } } ;
			trace("no_new_deps") ;
		} else {
			trace("miss") ;
		}
		return { {} , { .hit=No , .hit_info=hit_info } } ;
	}

	::optional<Cache::Match> DirCache::sub_match( ::string const& job , ::vmap_s<DepDigest> const& repo_deps ) const {
		Trace trace("DirCache::sub_match",job) ;
		LockedFd lock { lock_file } ;
		::optional<Match> res = _sub_match( job , repo_deps ).second ;
		trace("done") ;
		return res ;
	}

	::pair<JobInfo,AcFd> DirCache::sub_download(::string const& match_key) {                                 // match_key is returned by sub_match()
		Trace trace("DirCache::sub_download",match_key) ;
		SWEAR(match_key.back()=='/') ;
		NfsGuard nfs_guard { file_sync }                                                               ;
		AcFd     dfd       { nfs_guard.access_dir_s(dir_s+match_key) , {.flags=O_RDONLY|O_DIRECTORY} } ;
		AcFd     info_fd   ;
		AcFd     data_fd   ;
		{	LockedFd lock { lock_file }                          ;                                           // because we manipulate LRU, we need exclusive
			Sz       sz   = _lru_remove( match_key , nfs_guard ) ; throw_if( !sz , "no entry ",match_key ) ;
			_lru_mk_newest( match_key , sz , nfs_guard ) ;
			//                            err_ok
			info_fd = AcFd( dfd , "info" , true ) ; SWEAR(+info_fd) ;                                        // _lru_remove worked => everything should be accessible
			data_fd = AcFd( dfd , "data" , true ) ; SWEAR(+data_fd) ;                                        // .
		}
		trace("done") ;
		return { deserialize<JobInfo>(info_fd.read()) , ::move(data_fd) } ;
	}

	::pair<uint64_t/*upload_key*/,AcFd> DirCache::sub_upload(Sz reserve_sz) {
		Trace trace("DirCache::sub_upload",reserve_sz) ;
		//
		NfsGuard nfs_guard  { file_sync }        ;
		uint64_t upload_key = random<uint64_t>() ; if (!upload_key) upload_key = 1 ;                                                                                     // reserve 0 for no upload_key
		{	LockedFd lock { lock_file } ;                                                                                                                                // lock for minimal time
			_mk_room( 0 , reserve_sz , nfs_guard ) ;
		}
		AcFd         (nfs_guard.change(_reserved_file(upload_key,"sz"  )),{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444                   }).write(serialize(reserve_sz)) ; // reserved files are private
		AcFd data_fd {nfs_guard.change(_reserved_file(upload_key,"data")),{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444,.perm_ext=perm_ext}} ;                              // will be moved to ...
		trace("done",data_fd,upload_key) ;                                                                                                                               // ... permanent storage
		return { upload_key , ::move(data_fd) } ;
	}

	void DirCache::sub_commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		Trace trace("DirCache::sub_commit",upload_key,job) ;
		// START_OF_VERSIONING
		::string deps_str = serialize(job_info.end.digest.deps) ;
		// END_OF_VERSIONING
		NfsGuard        nfs_guard     { file_sync }                                               ;
		::string        jn_s          = job+'/'                                                   ;
		::string        abs_deps_hint = dir_s+jn_s+"deps_hint-"+_mk_crc(job_info.end.digest.deps) ;                       // deps_hint are hint only, hence no versioning problem
		LockedFd        lock          { lock_file }                                               ;                       // lock as late as possible
		::pair_s<Match> key_match     = _sub_match( job , job_info.end.digest.deps )              ;
		Sz              old_sz        = _reserved_sz(upload_key,nfs_guard)                        ;
		::string        key           ;
		if (key_match.second.hit==Yes) {                                                                                  // dont populate if a matching entry appeared while the job was running
			key = ::move(key_match.first) ;
			trace("hit",key) ;
			::string job_data = AcFd(_reserved_file(upload_key,"data")).read() ;
			_dismiss( upload_key , old_sz , nfs_guard ) ;                                                                 // finally, we did not populate
			throw_unless( AcFd(dir_s+key_match.second.key+"data").read()==job_data , "incoherent targets" ) ;             // check coherence
		} else {
			key = cat( "key-" , key_crc.hex() ) ; key += is_target(cat(dir_s,jn_s,key,"-first/lru")) ? "-last" : "-first" ;
			//
			::string jnid_s     = jn_s+key+'/'   ;
			::string abs_jnid_s = dir_s + jnid_s ;
			// START_OF_VERSIONING
			::string job_info_str = serialize(job_info) ;
			// END_OF_VERSIONING
			mk_dir_s(nfs_guard.change(abs_jnid_s),perm_ext) ;
			AcFd dfd       { nfs_guard.access_dir_s(abs_jnid_s) , {.flags=O_RDONLY|O_DIRECTORY} }                                              ;
			Sz   new_sz    = _entry_sz( jnid_s , nfs_guard.access(_reserved_file(upload_key,"data")) , deps_str.size() , job_info_str.size() ) ;
			bool made_room = false                                                                                                             ;
			bool unlnked   = false                                                                                                             ;
			trace("upload",key,new_sz) ;
			try {
				old_sz += _lru_remove(jnid_s,nfs_guard) ;
				unlnk_inside_s(dfd)                     ; unlnked   = true ;
				_mk_room( old_sz , new_sz , nfs_guard ) ; made_room = true ;
				// store meta-data and data
				// START_OF_VERSIONING
				AcFd(dfd,"info",{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444,.perm_ext=perm_ext}).write(job_info_str) ;
				AcFd(dfd,"deps",{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444,.perm_ext=perm_ext}).write(deps_str    ) ;     // store deps in a compact format so that matching is fast
				int rc = ::renameat( Fd::Cwd,nfs_guard.change(_reserved_file(upload_key,"data")).c_str() , dfd,"data" ) ;
				// END_OF_VERSIONING
				if (rc!=0) throw "cannot move data from tmp to final destination"s ;
				unlnk( nfs_guard.change(_reserved_file(upload_key,"sz")) , false/*dir_ok*/ , true/*abs_ok*/ ) ;
				_lru_mk_newest( jnid_s , new_sz , nfs_guard ) ;
				//
			} catch (::string const& e) {
				trace("failed",e) ;
				if (!unlnked) unlnk_inside_s(dfd) ;                                                                       // clean up in case of partial execution
				_dismiss( upload_key , made_room?new_sz:old_sz , nfs_guard ) ;                                            // finally, we did not populate the entry
				throw e ;
			}
		}
		unlnk( abs_deps_hint , false/*dir_ok*/ , true/*abs_ok*/ ) ;
		lnk  ( abs_deps_hint , key )                              ; // set a symlink from a name derived from deps to improve match speed in case of hit (hint only so target may be updated)
		trace("done") ;
	}

	void DirCache::sub_dismiss(uint64_t upload_key) {
		Trace trace("DirCache::sub_dismiss",upload_key) ;
		NfsGuard nfs_guard { file_sync } ;
		LockedFd lock      { lock_file } ;                                        // lock as late as possible
		_dismiss( upload_key , _reserved_sz(upload_key,nfs_guard) , nfs_guard ) ;
		trace("done") ;
	}

}
