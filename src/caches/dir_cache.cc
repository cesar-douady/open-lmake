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
//	- job_dir : <job>/<repo_crc> where :
//		- <job> is made after its name with suffixes replaced by readable suffixes and rule idx by rule crc
//		- <repo_crc> is computed after the repo as indicated in config.repo
//	- each job has :
//		- lru info       in <job_dir>/lru
//		- meta-data      in <job_dir>/meta_data (the content of job.ancillary_file() with dep crc's instead of dep dates + target sizes)
//		- deps crcs      in <job_dir>/deps      (the deps part of meta-data for fast matching)
//		- target content in <job_dir>/data      (concatenation of all targets that can be split based on target sizes stored in meta-data)

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
		AcFd     head_fd          { _lru_file(HeadS) } ;
		Lru      head             ;                      if (+head_fd) deserialize(head_fd.read(),head) ;
		::uset_s seen             ;
		::string expected_newer_s = HeadS              ;
		Sz       total_sz         = 0                  ;
		for( ::string entry_s=head.older_s ; entry_s!=HeadS ;) {
			auto here = deserialize<Lru>(AcFd(_lru_file(entry_s)).read()) ;
			//
			SWEAR(seen.insert(entry_s).second ,entry_s) ;
			SWEAR(here.newer_s==expected_newer_s,entry_s) ;
			total_sz         += here.sz      ;
			expected_newer_s  = entry_s      ;
			entry_s           = here.older_s ;
		}
		SWEAR( head.newer_s==expected_newer_s  , HeadS                     ) ;
		SWEAR( head.sz     ==total_sz+delta_sz , head.sz,total_sz,delta_sz ) ;
	}                                                                          // END_OF_NO_COV

	void DirCache::config( ::vmap_ss const& dct , bool may_init ) {
		::umap_ss config_map = mk_umap(dct) ;
		//
		if ( auto it=config_map.find("dir") ; it!=config_map.end() ) dir_s = with_slash(it->second) ;
		else                                                         throw "dir not found"s ;
		//
		if ( auto it=config_map.find("key") ; it!=config_map.end() ) key_crc = Crc(New,it->second) ;
		//
		if ( auto it=config_map.find("file_sync") ; it!=config_map.end() ) {
			if      (it->second=="None"                ) file_sync = FileSync::None ;
			else if (!can_mk_enum<FileSync>(it->second)) throw cat("unexpected value for file_sync : ",it->second) ;
			else                                         file_sync = mk_enum<FileSync>(it->second) ;
		}
		//
		::string sz_file = cat(dir_s,AdminDirS,"size") ;
		AcFd     sz_fd   { sz_file }                   ;
		throw_unless( +sz_fd , "file ",sz_file," must exist and contain the size of the cache" ) ;
		try                       { sz = from_string_with_unit(strip(sz_fd.read())) ; }
		catch (::string const& e) { throw "cannot read "+sz_file+" : "+e ;            }
		//
		try                     { chk_version(may_init,dir_s+AdminDirS) ;                   }
		catch (::string const&) { throw "version mismatch for dir_cache "+no_slash(dir_s) ; }
		//
		mk_dir_s(cat(dir_s,AdminDirS,"reserved/")) ;
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
			Sz       entry_sz = _entry_sz( entry_s , df_s+"data" , deps_sz , info_sz ) ;
			//
			try                     { entry.old_lru = _full_deserialize<Lru>( ::ref(size_t()) , df_s+"lru" ) ; }          // lru file size is already estimated
			catch (::string const&) { entry.old_lru = {}                                                     ; }          // avoid partial info
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
		static Re::RegExpr const entry_re { "((.*)/(\\d+-\\d+\\+)*rule-[\\dabcdef]{16}/key-[\\dabcdef]{16}/)(data|deps|info|lru)" } ;
		static Re::RegExpr const key_re   {                                           "key-[\\dabcdef]{16}"                       } ;
		static Re::RegExpr const hint_re  {  "(.*)/(\\d+-\\d+\\+)*rule-[\\dabcdef]{16}/deps_hint-[\\dabcdef]{16}"                 } ;
		//
		::umap_s<bool/*keep*/> dirs_s   { {"",true/*keep*/} , {HeadS,true/*keep*/} , {cat(HeadS,"reserved/"),true/*keep*/} } ;
		::uset_s               to_unlnk ;
		::umap_s<RepairEntry>  entries  ;
		::umap_ss              hints    ;
		//
		auto uphill = [&](::string const& d)->void {
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
			::string f = af.substr(1) ;                                                                                   // suppress leading /
			switch (t) {
				case FileTag::Dir :
					dirs_s.try_emplace(with_slash(f),false/*keep*/) ;
					continue ;
				break ;
				case FileTag::Reg   :
				case FileTag::Empty :
					if (f.starts_with(HeadS)) {
						::string_view sv = substr_view(f,sizeof(HeadS)-1) ;                                               // -1 to account for terminating null
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
		SWEAR_PROD(+dir_s) ;                                                                                              // avoid unlinking random files
		for( ::string const& f : to_unlnk ) {
			::string df = dir_s+f ;
			Fd::Stdout.write(cat("rm ",df,'\n')) ;
			if (!dry_run) unlnk( df , false/*dir_ok*/ , true/*abs_ok*/ ) ;
		}
		//
		::vector_s to_rmdir ;
		for( auto const& [d_s,k] : dirs_s ) { if (!k) to_rmdir.push_back(d_s) ; }
		::sort( to_rmdir , []( ::string const& a , ::string const& b )->bool { return a>b ; } ) ;                         // sort to ensure subdirs are rmdir'ed before their parent
		for( ::string const& d_s : to_rmdir ) {
			::string dd = no_slash(dir_s+d_s) ;
			Fd::Stdout.write(cat("rmdir ",dd,'\n')) ;
			if (!dry_run) ::rmdir(dd.c_str()) ;
		}
		//
		::vmap_s<RepairEntry> to_mk_lru ;
		for( auto const& f_e : entries ) if (+f_e.second) to_mk_lru.push_back(f_e) ;
		::sort(                                                                                                           // sort in LRU order, newer first
			to_mk_lru
		,	[]( ::pair_s<RepairEntry> const& a , ::pair_s<RepairEntry> const& b )->bool { return a.second.old_lru.last_access>b.second.old_lru.last_access ; }
		) ;
		Sz     total_sz = 0 ;
		size_t w        = 0 ; for ( auto const& [_,e] : to_mk_lru ) w = ::max(w,to_short_string_with_unit(e.sz).size()) ; // too expensive to filter out only non-printed entries
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
				if (!dry_run) AcFd(lru_file,FdAction::Create).write(serialize(new_lru)) ;
			}
		}
		::string head_lru_file = _lru_file(HeadS) ;
		Lru      old_head_lru  ;
		try                     { old_head_lru = deserialize<Lru>(AcFd(head_lru_file).read()) ; }
		catch (::string const&) { old_head_lru = {} ;                                           }                         // ensure no partial info
		Lru new_head_lru {
			.newer_s = +to_mk_lru ? to_mk_lru.back ().first : HeadS
		,	.older_s = +to_mk_lru ? to_mk_lru.front().first : HeadS
		,	.sz      = total_sz
		} ;
		if (new_head_lru!=old_head_lru) {
			Fd::Stdout.write(cat("rebuild head lru (total ",to_short_string_with_unit(new_head_lru.sz),"B) to ",head_lru_file,'\n')) ;
			if (!dry_run) AcFd(head_lru_file,FdAction::Write).write(serialize(new_head_lru)) ;
		}
	}

	void DirCache::_mk_room( Sz old_sz , Sz new_sz , NfsGuard& nfs_guard ) {
		throw_unless( new_sz<=sz , "cannot store entry of size ",new_sz," in cache of size ",sz ) ;
		//
		::string head_file        = _lru_file(HeadS)              ;
		AcFd     head_fd          { nfs_guard.access(head_file) } ;
		Lru      head             ;                                 if (+head_fd) deserialize(head_fd.read(),head) ;
		bool     some_removed     = false                         ;
		::string expected_older_s = HeadS                         ;                                     // for assertion only
		//
		SWEAR( head.sz>=old_sz , head.sz , old_sz ) ;                                                   // total size contains old_sz
		head.sz -= old_sz ;
		while (head.sz+new_sz>sz) {
			SWEAR(head.newer_s!=HeadS) ;                                                                // else this would mean an empty cache and we know an empty cache can accept new_sz
			auto here = deserialize<Lru>(AcFd(nfs_guard.access(_lru_file(head.newer_s))).read()) ;
			SWEAR( here.older_s==expected_older_s , here.older_s,expected_older_s ) ;
			SWEAR( head.sz     >=here.sz          , head.sz     ,here.sz          ) ;                   // total size contains this entry
			unlnk( nfs_guard.change(dir_s+no_slash(head.newer_s)) , true/*dir_ok*/ , true/*abs_ok*/ ) ;
			expected_older_s  =        head.newer_s  ;
			head.sz          -=        here.sz       ;
			head.newer_s      = ::move(here.newer_s) ;
			some_removed      =        true          ;
		}
		head.sz += new_sz ;
		SWEAR( head.sz<=sz , head.sz , sz ) ;
		//
		if (some_removed) {
			if (head.newer_s==HeadS) {
				head.older_s = HeadS ;
			} else {
				::string last_file = _lru_file(head.newer_s)                                    ;
				auto     last      = deserialize<Lru>(AcFd(nfs_guard.access(last_file)).read()) ;
				last.older_s = HeadS ;
				AcFd(last_file,FdAction::Write).write(serialize(last)) ;
			}
		}
		AcFd(nfs_guard.change(dir_guard(head_file)),FdAction::Create).write(serialize(head)) ;
	}

	DirCache::Sz DirCache::_lru_remove( ::string const& entry_s , NfsGuard& nfs_guard ) {
		SWEAR(entry_s!=HeadS) ;
		//
		AcFd here_fd { nfs_guard.access(_lru_file(entry_s)) } ; if (!here_fd) return 0 ; // nothing to remove
		auto here    = deserialize<Lru>(here_fd.read())       ;
		if (here.newer_s==here.older_s) {
			::string newer_older_file = _lru_file(here.newer_s)                                           ;
			auto     newer_older      = deserialize<Lru>(AcFd(nfs_guard.access(newer_older_file)).read()) ;
			//
			newer_older.older_s = here.older_s ;
			newer_older.newer_s = here.newer_s ;
			//
			AcFd(nfs_guard.change(newer_older_file),FdAction::Write).write(serialize(newer_older)) ;
		} else {
			::string newer_file = _lru_file(here.newer_s)                                     ;
			::string older_file = _lru_file(here.older_s)                                     ;
			auto     newer      = deserialize<Lru>(AcFd(nfs_guard.access(newer_file)).read()) ;
			auto     older      = deserialize<Lru>(AcFd(nfs_guard.access(older_file)).read()) ;
			//
			newer.older_s = here.older_s ;
			older.newer_s = here.newer_s ;
			//
			AcFd(nfs_guard.change(newer_file),FdAction::Write).write(serialize(newer)) ;
			AcFd(nfs_guard.change(older_file),FdAction::Write).write(serialize(older)) ;
		}
		return here.sz ;
	}

	void DirCache::_lru_mk_newest( ::string const& entry_s , Sz sz_ , NfsGuard& nfs_guard ) {
		SWEAR(entry_s!=HeadS) ;
		//
		::string head_file = _lru_file(HeadS)                                           ;
		auto     head      = deserialize<Lru>(AcFd(nfs_guard.access(head_file)).read()) ;
		::string here_file = _lru_file(entry_s)                                         ;
		Lru      here      { .older_s=head.older_s , .sz=sz_ , .last_access=New }       ;
		if (head.older_s==HeadS) {
			head.older_s = entry_s ;
			head.newer_s = entry_s ;
		} else {
			::string newest_file = _lru_file(head.older_s)                                      ;
			auto     newest      = deserialize<Lru>(AcFd(nfs_guard.access(newest_file)).read()) ;
			head  .older_s = entry_s ;
			newest.newer_s = entry_s ;
			AcFd(nfs_guard.change(newest_file),FdAction::Create).write(serialize(newest)) ;
		}
		AcFd(nfs_guard.change(head_file),FdAction::Create).write(serialize(head)) ;
		AcFd(nfs_guard.change(here_file),FdAction::Create).write(serialize(here)) ;
	}

	::string DirCache::_reserved_file( uint64_t upload_key , ::string const& sfx ) const {
		return cat(dir_s,AdminDirS,"reserved/",to_hex(upload_key),'.',sfx) ;
	}

	DirCache::Sz DirCache::_reserved_sz( uint64_t upload_key , NfsGuard& nfs_guard ) const {
		return deserialize<Sz>(AcFd(nfs_guard.access(_reserved_file(upload_key,"sz"))).read()) ;
	}

	Cache::Match DirCache::_sub_match( ::string const& job , ::vmap_s<DepDigest> const& repo_deps , bool do_lock ) const {
		Trace trace("DirCache::_sub_match",job) ;
		//
		NfsGuard                    nfs_guard    { file_sync }                                         ;
		::string                    abs_jn_s     = dir_s+job+'/'                                       ;
		AcFd                        dfd          { nfs_guard.access_dir(abs_jn_s) , FdAction::Dir }    ;
		LockedFd                    lock         ;                                                       if (do_lock) lock = { dir_s , false/*exclusive*/ } ;
		::umap_s<DepDigest>/*lazy*/ repo_dep_map ;
		::string                    deps_hint    = read_lnk(dfd,"deps_hint-"+Crc(New,repo_deps).hex()) ; // may point to the right entry (hint only as link is not updated when its target is modified)
		::vector_s                  repos        ;
		//
		try                       { repos = lst_dir_s(dfd) ;            }
		catch (::string const& e) { trace("dir_not_found",abs_jn_s,e) ; }                                // if directory does not exist, it is as it was empty
		if ( +deps_hint && repos.size()>1 )                                                              // reorder is only meaningful if there are several entries
			for( size_t r : iota(repos.size()) ) {
				if (repos[r]!=deps_hint) continue ;
				for( size_t i=r ; i>0 ; i-- ) repos[i] = ::move(repos[i-1]) ;                            // rotate repos to put hint in front
				repos[0] = ::move(deps_hint) ;                                                           // .
				break ;
			}
		for( ::string const& r : repos ) {
			::string            deps_file  = abs_jn_s+r+"/deps" ;
			::vmap_s<DepDigest> cache_deps ;
			try                       { deserialize( AcFd(nfs_guard.access(deps_file)).read() , cache_deps ) ; }
			catch (::string const& e) { trace("bad_deps",deps_file,e) ; continue ;                             }                                                    // cannot read deps, ignore entry
			//
			NodeIdx dvg     = 0     ;
			bool    has_dvg = false ;
			for( auto const& [dn,dd] : cache_deps ) {
				if (!has_dvg) {
					if      ( dvg>=repo_deps.size() || dn!=repo_deps[dvg].first       ) { has_dvg = true ; if (!repo_dep_map) repo_dep_map = mk_umap(repo_deps) ; } // solve lazy evaluation
					else if ( dd.crc().match(repo_deps[dvg].second.crc(),dd.accesses) ) { dvg++ ; continue  ;                                                     }
					else                                                                          goto Miss ;
				}
				if      ( auto it=repo_dep_map.find(dn) ; it==repo_dep_map.end() ) continue  ;
				else if ( dd.crc().match( it->second.crc() , dd.accesses )       ) continue  ;
				else                                                               goto Miss ;
			}
			if (!has_dvg) {
				trace("hit",r) ;
				return { .completed=true , .hit=Yes , .key{job+'/'+r+'/'} } ;                                                                                       // hit
			}
			for( NodeIdx i : iota(dvg,cache_deps.size()) ) {
				if ( cache_deps[i].second.dflags[Dflag::Critical] && ! repo_dep_map.contains(cache_deps[i].first) ) {
					cache_deps.resize(i+1) ;
					break ;
				}
			}
			trace("deps",cache_deps) ;
			return { .completed=true , .hit=Maybe , .new_deps{::move(cache_deps)} } ;
		Miss : ;                                                                                                                                                    // missed this entry, try next one
		}
		trace("miss") ;
		return { .completed=true , .hit=No } ;
	}

	Cache::Match DirCache::sub_match( ::string const& job , ::vmap_s<DepDigest> const& repo_deps ) const {
		return _sub_match( job , repo_deps , true/*do_lock*/ ) ;
	}

	::pair<JobInfo,AcFd> DirCache::sub_download(::string const& match_key) {                                 // match_key is returned by sub_match()
		SWEAR(match_key.back()=='/') ;
		NfsGuard nfs_guard { file_sync }                                             ;
		AcFd     dfd       { nfs_guard.access_dir(dir_s+match_key) , FdAction::Dir } ;
		AcFd     info_fd   ;
		AcFd     data_fd   ;
		{	LockedFd lock { dir_s , true /*exclusive*/ }         ;                                           // because we manipulate LRU, we need exclusive
			Sz       sz   = _lru_remove( match_key , nfs_guard ) ; throw_if( !sz , "no entry ",match_key ) ;
			_lru_mk_newest( match_key , sz , nfs_guard ) ;
			info_fd = AcFd( dfd , "info"s ) ; SWEAR(+info_fd) ;                                              // _lru_remove worked => everything should be accessible
			data_fd = AcFd( dfd , "data"s ) ; SWEAR(+data_fd) ;                                              // .
		}
		 ;
		return { deserialize<JobInfo>(info_fd.read()) , ::move(data_fd) } ;
	}

	::pair<uint64_t/*upload_key*/,AcFd> DirCache::sub_upload(Sz max_sz) {
		Trace trace("DirCache::reserve",max_sz) ;
		//
		NfsGuard nfs_guard  { file_sync }        ;
		uint64_t upload_key = random<uint64_t>() ; if (!upload_key) upload_key = 1 ; // reserve 0 for no upload_key
		{	LockedFd lock { dir_s , true/*exclusive*/ } ;
			_mk_room( 0 , max_sz , nfs_guard ) ;
		}
		AcFd         ( nfs_guard.change(_reserved_file(upload_key,"sz"  )) , FdAction::CreateReadOnly ).write(serialize(max_sz)) ;
		AcFd data_fd { nfs_guard.change(_reserved_file(upload_key,"data")) , FdAction::CreateReadOnly } ;
		trace(data_fd,upload_key) ;
		return { upload_key , ::move(data_fd) } ;
	}

	bool/*ok*/ DirCache::sub_commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		NfsGuard nfs_guard { file_sync }                   ;
		::string jn_s      = job+'/'                       ;
		::string key_s     = cat("key-",key_crc.hex(),'/') ;
		::string jnid_s    = jn_s+key_s                    ;
		Trace trace("DirCache::sub_commit",upload_key,job) ;
		//
		// START_OF_VERSIONING
		::string job_info_str = serialize(job_info                ) ;
		::string deps_str     = serialize(job_info.end.digest.deps) ;
		// END_OF_VERSIONING
		::string abs_jnid_s = dir_s + jnid_s ;
		mk_dir_s(nfs_guard.change(abs_jnid_s)) ;
		AcFd dfd { nfs_guard.access_dir(abs_jnid_s) , FdAction::Dir } ;
		//
		// upload is the only one to take several locks and it starts with the global lock
		// this way, we are sure to avoid deadlocks
		Sz       old_sz    = _reserved_sz(upload_key,nfs_guard)                                                                                ;
		Sz       new_sz    = _entry_sz( jnid_s , nfs_guard.access(_reserved_file(upload_key,"data")) , deps_str.size() , job_info_str.size() ) ;
		bool     made_room = false                                                                                                             ;
		LockedFd lock      { dir_s , true/*exclusive*/ }                                                                                       ; // lock as late as possible
		Bool3    hit       = _sub_match( job , ::ref(::vmap_s<DepDigest>()) , false/*do_lock*/ ).hit                                           ;
		if (hit==Yes) {
			::string job_data = AcFd(_reserved_file(upload_key,"data")).read() ;
			_dismiss( upload_key , made_room?new_sz:old_sz , nfs_guard ) ;                                                // finally, we did not populate the entry
			throw_unless( AcFd(dfd,"data"s).read()==job_data , "incoherent targets" ) ;                                   // check coherence
		} else {
			try {
				old_sz += _lru_remove(jnid_s,nfs_guard) ;
				unlnk_inside_s(dfd) ;
				// store meta-data and data
				_mk_room( old_sz , new_sz , nfs_guard ) ;
				made_room = true ;
				// START_OF_VERSIONING
				AcFd(dfd,"info",FdAction::CreateReadOnly).write(job_info_str) ;
				AcFd(dfd,"deps",FdAction::CreateReadOnly).write(deps_str    ) ;                                           // store deps in a compact format so that matching is fast
				int rc = ::renameat( Fd::Cwd,nfs_guard.change(_reserved_file(upload_key,"data")).c_str() , dfd,"data" ) ;
				// END_OF_VERSIONING
				if (rc<0) throw "cannot move data from tmp to final destination"s ;
				unlnk( nfs_guard.change(_reserved_file(upload_key,"sz")) , false/*dir_ok*/ , true/*abs_ok*/ ) ;
				_lru_mk_newest( jnid_s , new_sz , nfs_guard ) ;
			} catch (::string const& e) {
				trace("failed",e) ;
				unlnk_inside_s(dfd) ;                                                                                     // clean up in case of partial execution
				_dismiss( upload_key , made_room?new_sz:old_sz , nfs_guard ) ;                                            // finally, we did not populate the entry
				return false/*ok*/ ;
			}
		}
		// set a symlink from a name derived from deps to improve match speed in case of hit
		// this is a hint only as when link target is updated, link is not
		::string abs_deps_hint = dir_s+jn_s+"deps_hint-"+Crc(New,deps_str).hex() ;
		unlnk( abs_deps_hint , false/*dir_ok*/ , true/*abs_ok*/ ) ;
		lnk  ( abs_deps_hint , no_slash(key_s) ) ;
		//
		trace("done",new_sz) ;
		return true/*ok*/ ;
	}

	void DirCache::sub_dismiss(uint64_t upload_key) {
		NfsGuard nfs_guard { file_sync }                 ;
		LockedFd lock      { dir_s , true/*exclusive*/ } ;                        // lock as late as possible
		_dismiss( upload_key , _reserved_sz(upload_key,nfs_guard) , nfs_guard ) ;
	}

	void DirCache::_dismiss( uint64_t upload_key , Sz sz , NfsGuard& nfs_guard ) {
		_mk_room( sz , 0 , nfs_guard ) ; //!                        dir_ok  abs_ok
		unlnk( nfs_guard.change(_reserved_file(upload_key,"sz"  )) , false , true ) ;
		unlnk( nfs_guard.change(_reserved_file(upload_key,"data")) , false , true ) ;
	}

}
