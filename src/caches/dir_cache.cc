// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// cache format :
//	- Lru contains
//		- prev : more recently used entry. For most  recently used, contains head. For head, contains least recently used entry.
//		- next : less recently used entry. For least recently used, contanis head. For head, contains most  recently used entry.
//		- sz   : size of the entry. For head, total size of the cache.
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
using namespace Time ;

namespace Caches {

	// START_OF_VERSIONING

	struct Lru {
		::string     prev_s      = DirCache::HeadS ;
		::string     next_s      = DirCache::HeadS ;
		DirCache::Sz sz          = 0               ; // size of entry, or overall size for head
		Pdate        last_access ;
	} ;

	void DirCache::chk(ssize_t delta_sz) const {                          // START_OF_NO_COV debug only
		AcFd     head_fd         { _lru_file(HeadS) } ;
		Lru      head            ;                      if (+head_fd) deserialize(head_fd.read(),head) ;
		::uset_s seen            ;
		::string expected_prev_s = HeadS              ;
		size_t   total_sz        = 0                  ;
		for( ::string entry_s=head.next_s ; entry_s!=HeadS ;) {
			auto here = deserialize<Lru>(AcFd(_lru_file(entry_s)).read()) ;
			//
			SWEAR(seen.insert(entry_s).second ,entry_s) ;
			SWEAR(here.prev_s==expected_prev_s,entry_s) ;
			total_sz        += here.sz     ;
			expected_prev_s  = entry_s     ;
			entry_s          = here.next_s ;
		}
		SWEAR(head.prev_s==expected_prev_s  ,HeadS                    ) ;
		SWEAR(head.sz    ==total_sz+delta_sz,head.sz,total_sz,delta_sz) ;
	}                                                                     // END_OF_NO_COV

	// END_OF_VERSIONING

	void DirCache::config(::vmap_ss const& dct) {
		::umap_ss config_map = mk_umap(dct) ;
		//
		if ( auto it=config_map.find("dir") ; it!=config_map.end() ) dir_s = with_slash(it->second) ;
		else                                                         throw "dir not found"s ;
		//
		Hash::Xxh key_hash ;
		if ( auto it=config_map.find("key") ; it!=config_map.end() ) key_hash += it->second ;
		else                                                         throw "key not found"s ;
		key_s = "key-"+key_hash.digest().hex()+'/' ;
		//
		if ( auto it=config_map.find("file_sync") ; it!=config_map.end() ) {
			if      (it->second=="None"                ) file_sync = FileSync::None ;
			else if (!can_mk_enum<FileSync>(it->second)) throw cat("unexpected value for file_sync : ",it->second) ;
			else                                         file_sync = mk_enum<FileSync>(it->second) ;
		}
		//
		try                     { chk_version(true/*may_init*/,dir_s+AdminDirS) ;                    }
		catch (::string const&) { throw "cache version mismatch, running without "+no_slash(dir_s) ; }
		//
		::string sz_file = cat(dir_s,AdminDirS,"size") ;
		AcFd     sz_fd   { sz_file }                   ;
		throw_unless( +sz_fd , "file ",sz_file," must exist and contain the size of the cache" ) ;
		try                       { sz = from_string_with_unit(strip(sz_fd.read())) ; }
		catch (::string const& e) { throw "cannot read "+sz_file+" : "+e ;            }
		mk_dir_s( cat(dir_s,AdminDirS,"reserved") ) ;
	}

	void DirCache::_mk_room( Sz old_sz , Sz new_sz , NfsGuard& nfs_guard ) {
		throw_unless( new_sz<=sz , "cannot store entry of size ",new_sz," in cache of size ",sz ) ;
		//
		::string head_file       = _lru_file(HeadS)              ;
		AcFd     head_fd         { nfs_guard.access(head_file) } ;
		Lru      head            ;                                 if (+head_fd) deserialize(head_fd.read(),head) ;
		bool     some_removed    = false                         ;
		::string expected_next_s = HeadS                         ;                                // for assertion only
		//
		SWEAR( head.sz>=old_sz , head.sz , old_sz ) ;                                             // total size contains old_sz
		head.sz -= old_sz ;
		while (head.sz+new_sz>sz) {
			SWEAR(head.prev_s!=HeadS) ;                                                           // else this would mean an empty cache and we know an empty cache can accept new_sz
			auto here = deserialize<Lru>(AcFd(nfs_guard.access(_lru_file(head.prev_s))).read()) ;
			SWEAR( here.next_s==expected_next_s , here.next_s , expected_next_s ) ;
			SWEAR( head.sz    >=here.sz         , head.sz     , here.sz         ) ;               // total size contains this entry
			unlnk(nfs_guard.change(dir_s+no_slash(head.prev_s)),true/*dir_ok*/) ;
			expected_next_s  = head.prev_s         ;
			head.sz         -= here.sz             ;
			head.prev_s      = ::move(here.prev_s) ;
			some_removed     = true                ;
		}
		head.sz += new_sz ;
		SWEAR( head.sz<=sz , head.sz , sz ) ;
		//
		if (some_removed) {
			if (head.prev_s==HeadS) {
				head.next_s = HeadS ;
			} else {
				::string last_file = _lru_file(head.prev_s)                                     ;
				auto     last      = deserialize<Lru>(AcFd(nfs_guard.access(last_file)).read()) ;
				last.next_s = HeadS ;
				AcFd(last_file,Fd::Write).write(serialize(last)) ;
			}
		}
		AcFd(nfs_guard.change(dir_guard(head_file)),Fd::Write).write(serialize(head)) ;
	}

	DirCache::Sz DirCache::_lru_remove( ::string const& entry_s , NfsGuard& nfs_guard ) {
		SWEAR(entry_s!=HeadS) ;
		//
		AcFd here_fd { nfs_guard.access(_lru_file(entry_s)) } ; if (!here_fd) return 0 ; // nothing to remove
		auto here    = deserialize<Lru>(here_fd.read())       ;
		if (here.prev_s==here.next_s) {
			::string pn_file = _lru_file(here.prev_s)                                   ;
			auto     pn      = deserialize<Lru>(AcFd(nfs_guard.access(pn_file)).read()) ;
			//
			pn.next_s = here.next_s ;
			pn.prev_s = here.prev_s ;
			//
			AcFd(nfs_guard.change(pn_file),Fd::Write).write(serialize(pn)) ;
		} else {
			::string prev_file = _lru_file(here.prev_s)                                     ;
			::string next_file = _lru_file(here.next_s)                                     ;
			auto     prev      = deserialize<Lru>(AcFd(nfs_guard.access(prev_file)).read()) ;
			auto     next      = deserialize<Lru>(AcFd(nfs_guard.access(next_file)).read()) ;
			//
			prev.next_s = here.next_s ;
			next.prev_s = here.prev_s ;
			//
			AcFd(nfs_guard.change(prev_file),Fd::Write).write(serialize(prev)) ;
			AcFd(nfs_guard.change(next_file),Fd::Write).write(serialize(next)) ;
		}
		return here.sz ;
	}

	void DirCache::_lru_first( ::string const& entry_s , Sz sz_ , NfsGuard& nfs_guard ) {
		SWEAR(entry_s!=HeadS) ;
		//
		::string head_file = _lru_file(HeadS)                                           ;
		auto     head      = deserialize<Lru>(AcFd(nfs_guard.access(head_file)).read()) ;
		::string here_file = _lru_file(entry_s)                                         ;
		Lru      here      { .next_s=head.next_s , .sz=sz_ , .last_access=New }         ;
		if (head.next_s==HeadS) {
			head.next_s = entry_s ;
			head.prev_s = entry_s ;
		} else {
			::string first_file = _lru_file(head.next_s)                                      ;
			auto     first      = deserialize<Lru>(AcFd(nfs_guard.access(first_file)).read()) ;
			head .next_s = entry_s ;
			first.prev_s = entry_s ;
			AcFd(nfs_guard.change(first_file),Fd::Write).write(serialize(first)) ;
		}
		AcFd(nfs_guard.change(head_file),Fd::Write).write(serialize(head)) ;
		AcFd(nfs_guard.change(here_file),Fd::Write).write(serialize(here)) ;
	}

	::string DirCache::_reserved_file( uint64_t upload_key , ::string const& sfx ) const {
		return cat(dir_s,AdminDirS,"reserved/",to_hex(upload_key),'.',sfx) ;
	}

	DirCache::Sz DirCache::_reserved_sz( uint64_t upload_key , NfsGuard& nfs_guard ) const {
		return deserialize<Sz>(AcFd(nfs_guard.access(_reserved_file(upload_key,"sz"))).read()) ;
	}

	Cache::Match DirCache::sub_match( ::string const& job , ::vmap_s<DepDigest> const& repo_deps ) {
		Trace trace("DirCache::match",job) ;
		NfsGuard            nfs_guard    { file_sync }                                ;
		::string            abs_jn_s     = dir_s+job+'/'                              ;
		AcFd                dfd          { nfs_guard.access_dir(abs_jn_s) , Fd::Dir } ;
		LockedFd            lock         { dir_s , false/*exclusive*/ }               ;
		::umap_s<DepDigest> repo_dep_map ;                                              // lazy evaluated
		// deps_hint may point to the right entry (hint only as link is not updated when its target is modified)
		Hash::Xxh deps_hint_hash ;                                                            deps_hint_hash += repo_deps ;
		::string  deps_hint      = read_lnk(dfd,"deps_hint-"+deps_hint_hash.digest().hex()) ;
		//
		::vector_s repos ;
		try                       { repos = lst_dir_s(dfd) ;            }
		catch (::string const& e) { trace("dir_not_found",abs_jn_s,e) ; }               // if directory does not exist, it is as it was empty
		if ( +deps_hint && repos.size()>1 )                                             // reorder is only meaningful if there are several entries
			for( size_t r : iota(repos.size()) )
				if (repos[r]==deps_hint) {                                              // hint found
					for( size_t i=r ; i>0 ; i-- ) repos[i] = ::move(repos[i-1]) ;       // rotate repos to put hint in front
					repos[0] = ::move(deps_hint) ;                                      // .
					break ;
				}
		for( ::string const& r : repos ) {
			::string            deps_file  = abs_jn_s+r+"/deps" ;
			::vmap_s<DepDigest> cache_deps ;
			try                       { deserialize( AcFd(nfs_guard.access(deps_file)).read() , cache_deps ) ; }
			catch (::string const& e) { trace("bad_deps",deps_file,e) ; continue ;                             }                                                      // cannot read deps, ignore entry
			//
			NodeIdx dvg     = 0     ;
			bool    has_dvg = false ;
			for( auto const& [dn,dd] : cache_deps ) {
				if (!has_dvg) {
					if      ( dvg>=repo_deps.size() || dn!=repo_deps[dvg].first         ) { has_dvg = true ; if (!repo_dep_map) repo_dep_map = mk_umap(repo_deps) ; } // solve lazy evaluation
					else if (dd.crc().match( repo_deps[dvg].second.crc() , dd.accesses )) { dvg++ ; continue  ;                                                     }
					else                                                                            goto Miss ;
				}
				if      ( auto it=repo_dep_map.find(dn) ; it==repo_dep_map.end() ) continue  ;
				else if ( dd.crc().match( it->second.crc() , dd.accesses )       ) continue  ;
				else                                                               goto Miss ;
			}
			if (!has_dvg) {
				trace("hit",r) ;
				return { .completed=true , .hit=Yes , .key{job+'/'+r} } ;                                                                                             // hit
			}
			for( NodeIdx i : iota(dvg,cache_deps.size()) ) {
				if ( cache_deps[i].second.dflags[Dflag::Critical] && ! repo_dep_map.contains(cache_deps[i].first) ) {
					cache_deps.resize(i+1) ;
					break ;
				}
			}
			trace("deps",cache_deps) ;
			return { .completed=true , .hit=Maybe , .new_deps{::move(cache_deps)} } ;
		Miss : ;                                                                                                                                                      // missed this entry, try next one
		}
		trace("miss") ;
		return { .completed=true , .hit=No } ;
	}

	::pair<JobInfo,AcFd> DirCache::sub_download(::string const& match_key) {
		NfsGuard                nfs_guard { file_sync }                                   ;
		::string                key_s     = match_key+'/'                                 ;
		AcFd                    dfd       { nfs_guard.access_dir(dir_s+key_s) , Fd::Dir } ;
		AcFd                    info_fd   ;
		AcFd                    data_fd   ;
		{	LockedFd lock { dir_s , true /*exclusive*/ }     ;                                           // because we manipulate LRU, need exclusive
			Sz       sz   = _lru_remove( key_s , nfs_guard ) ; throw_if( !sz , "no entry ",match_key ) ;
			_lru_first( key_s , sz , nfs_guard ) ;
			info_fd = AcFd( dfd , "info"s ) ; SWEAR(+info_fd) ;                                          // _lru_remove worked => everything should be accessible
			data_fd = AcFd( dfd , "data"s ) ; SWEAR(+data_fd) ;                                          // .
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
		AcFd         ( nfs_guard.change(_reserved_file(upload_key,"sz"  )) , Fd::CreateReadOnly ).write(serialize(max_sz)) ;
		AcFd data_fd { nfs_guard.change(_reserved_file(upload_key,"data")) , Fd::CreateReadOnly } ;
		trace(data_fd,upload_key) ;
		return { upload_key , ::move(data_fd) } ;
	}

	bool/*ok*/ DirCache::sub_commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		NfsGuard nfs_guard { file_sync } ;
		::string jn_s      = job+'/'     ;
		::string jnid_s    = jn_s+key_s  ;
		Trace trace("DirCache::sub_commit",upload_key,job) ;
		//
		// START_OF_VERSIONING
		::string job_info_str = serialize(job_info                ) ;
		::string deps_str     = serialize(job_info.end.digest.deps) ;
		// END_OF_VERSIONING
		::string abs_jnid_s = dir_s + jnid_s ;
		mk_dir_s(nfs_guard.change(abs_jnid_s)) ;
		AcFd dfd { nfs_guard.access_dir(abs_jnid_s) , Fd::Dir } ;
		//
		// upload is the only one to take several locks and it starts with the global lock
		// this way, we are sure to avoid deadlocks
		Sz new_sz =
			job_info_str.size()
		+	deps_str.size()
		+	FileInfo(nfs_guard.access(_reserved_file(upload_key,"data") )).sz
		;
		Sz       old_sz    = _reserved_sz(upload_key,nfs_guard) ;
		bool     made_room = false                              ;
		LockedFd lock      { dir_s , true/*exclusive*/ }        ;                                                     // lock as late as possible
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
			_lru_first( jnid_s , new_sz , nfs_guard ) ;
		} catch (::string const& e) {
			trace("failed",e) ;
			unlnk_inside_s(dfd) ;                                                                                     // clean up in case of partial execution
			_dismiss( upload_key , made_room?new_sz:old_sz , nfs_guard ) ;                                            // finally, we did not populate the entry
			return false/*ok*/ ;
		}
		// set a symlink from a name derived from deps to improve match speed in case of hit
		// this is a hint only as when link target is updated, link is not
		Hash::Xxh deps_hash ; deps_hash += deps_str ;
		::string abs_deps_hint = dir_s+jn_s+"deps_hint-"+deps_hash.digest().hex() ;
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
