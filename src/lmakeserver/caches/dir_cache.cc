// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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

#include <sys/sendfile.h>

#include "dir_cache.hh"

using namespace Disk ;

namespace Caches {

	// START_OF_VERSIONING

	struct Lru {
		::string     prev_s = DirCache::HeadS ;
		::string     next_s = DirCache::HeadS ;
		DirCache::Sz sz     = 0               ; // size of entry, or overall size for head
	} ;

	void DirCache::chk(ssize_t delta_sz) const {
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
	}

	// END_OF_VERSIONING

	void DirCache::config(Config::Cache const& config) {
		::map_ss dct = mk_map(config.dct) ;
		//
		if ( auto it=dct.find("dir" ) ; it!=dct.end() ) dir_s  = with_slash(it->second) ;
		else                                            throw "dir not found"s  ;
		//
		Hash::Xxh repo_hash ;
		if ( auto it=dct.find("repo") ; it!=dct.end() ) repo_hash.update(it->second              ) ;
		else                                            repo_hash.update(no_slash(*g_repo_root_s)) ;
		repo_s = "repo-"+repo_hash.digest().hex()+'/' ;
		//
		try                     { chk_version(true/*may_init*/,dir_s+AdminDirS) ;                    }
		catch (::string const&) { throw "cache version mismatch, running without "+no_slash(dir_s) ; }
		//
		dir_fd = { dir_s , Fd::Dir , true/*no_std*/ } ;                                  // avoid poluting standard descriptors
		if (!dir_fd) throw "cannot configure cache "+no_slash(dir_s)+" : no directory" ;
		//
		::string sz_file = cat(AdminDirS,"size") ;
		AcFd     sz_fd   { dir_fd , sz_file }    ;
		throw_unless( +sz_fd , "file ",dir_s,sz_file," must exist and contain the size of the cache" ) ;
		try                       { sz = from_string_with_units<size_t>(strip(sz_fd.read())) ; }
		catch (::string const& e) { throw "cannot read "+dir_s+sz_file+" : "+e ;               }
	}

	// START_OF_VERSIONING
	static ::string _unique_name_s(Job job) {
		Rule     rule      = job->rule()                            ;
		::string full_name = job->full_name()                       ; rule->validate(full_name) ;                                   // only name suffix is considered to make Rule
		size_t   user_sz   = full_name.size() - rule->job_sfx_len() ;
		::string res       = full_name.substr(0,user_sz)            ; res.reserve(res.size()+1+rule->n_static_stems*(2*(3+1))+16) ; // allocate 2x3 digits per stem, this is comfortable
		//
		for( char& c : res ) if (c==Rule::StarMrkr) c = '*' ;
		res.push_back('/') ;
		//
		char* p = &full_name[user_sz+1] ;                                                                                           // start of suffix
		for( [[maybe_unused]] VarIdx _ : iota(rule->n_static_stems) ) {
			FileNameIdx pos = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			res << pos << '-' << sz << '+' ;
		}
		res << "rule-" << rule->crc->cmd.hex() << '/' ;
		return res ;
	}
	// END_OF_VERSIONING

	void DirCache::_mk_room( Sz old_sz , Sz new_sz ) {
		throw_unless( new_sz<=sz , "cannot store entry of size ",new_sz," in cache of size ",sz ) ;
		//
		::string head_file       = _lru_file(HeadS) ;
		AcFd     head_fd         { head_file }      ;
		Lru      head            ;                    if (+head_fd) deserialize(head_fd.read(),head) ;
		bool     some_removed    = false            ;
		::string expected_next_s = HeadS            ;                               // for assertion only
		//
		SWEAR( head.sz>=old_sz , head.sz , old_sz ) ;                               // total size contains old_sz
		head.sz -= old_sz ;
		while (head.sz+new_sz>sz) {
			SWEAR(head.prev_s!=HeadS) ;                                             // else this would mean an empty cache and we know an empty cache can accept new_sz
			auto here = deserialize<Lru>(AcFd(_lru_file(head.prev_s)).read()) ;
			SWEAR( here.next_s==expected_next_s , here.next_s , expected_next_s ) ;
			SWEAR( head.sz    >=here.sz         , head.sz     , here.sz         ) ; // total size contains this entry
			unlnk(dir_fd,no_slash(head.prev_s),true/*dir_ok*/) ;
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
				::string last_file = _lru_file(head.prev_s)                   ;
				auto     last      = deserialize<Lru>(AcFd(last_file).read()) ;
				last.next_s = HeadS ;
				AcFd(last_file,Fd::Write).write(serialize(last)) ;
			}
		}
		AcFd(dir_guard(head_file),Fd::Write).write(serialize(head)) ;
	}

	DirCache::Sz DirCache::_lru_remove(::string const& entry_s) {
		SWEAR(entry_s!=HeadS) ;
		//
		AcFd here_fd { _lru_file(entry_s) }             ; if (!here_fd) return 0 ; // nothing to remove
		auto here    = deserialize<Lru>(here_fd.read()) ;
		if (here.prev_s==here.next_s) {
			::string pn_file = _lru_file(here.prev_s)                 ;
			auto     pn      = deserialize<Lru>(AcFd(pn_file).read()) ;
			//
			pn.next_s = here.next_s ;
			pn.prev_s = here.prev_s ;
			//
			AcFd(pn_file,Fd::Write).write(serialize(pn)) ;
		} else {
			::string prev_file = _lru_file(here.prev_s)                   ;
			::string next_file = _lru_file(here.next_s)                   ;
			auto     prev      = deserialize<Lru>(AcFd(prev_file).read()) ;
			auto     next      = deserialize<Lru>(AcFd(next_file).read()) ;
			//
			prev.next_s = here.next_s ;
			next.prev_s = here.prev_s ;
			//
			AcFd(prev_file,Fd::Write).write(serialize(prev)) ;
			AcFd(next_file,Fd::Write).write(serialize(next)) ;
		}
		return here.sz ;
	}

	void DirCache::_lru_first( ::string const& entry_s , Sz sz_ ) {
		SWEAR(entry_s!=HeadS) ;
		//
		::string head_file  = _lru_file(HeadS)                         ;
		auto     head       = deserialize<Lru>(AcFd(head_file).read()) ;
		::string here_file  = _lru_file(entry_s)                       ;
		Lru      here       { .next_s=head.next_s , .sz=sz_ }          ;
		if (head.next_s==HeadS) {
			head.next_s = entry_s ;
			head.prev_s = entry_s ;
		} else {
			::string first_file = _lru_file(head.next_s)                    ;
			auto     first      = deserialize<Lru>(AcFd(first_file).read()) ;
			head .next_s = entry_s ;
			first.prev_s = entry_s ;
			AcFd(first_file,Fd::Write).write(serialize(first)) ;
		}
		AcFd(head_file,Fd::Write).write(serialize(head)) ;
		AcFd(here_file,Fd::Write).write(serialize(here)) ;
	}

	Cache::Match DirCache::match( Job job , Req req ) {
		Trace trace("DirCache::match",job,req) ;
		::string     jn_s     = _unique_name_s(job)        ;
		::uset<Node> new_deps ;
		AcFd         dfd      { dir_fd , jn_s , Fd::Dir }  ;
		LockedFd     lock     { dfd , false/*exclusive*/ } ;
		bool         found    = false                      ;
		//
		try {
			for( ::string const& r : lst_dir_s(dfd) ) {
				::uset<Node> nds      ;
				auto         deps     = deserialize<::vmap_s<DepDigest>>(AcFd(dir_s+jn_s+r+"/deps").read()) ;
				bool         critical = false                                                               ;
				//
				for( auto const& [dn,dd] : deps ) {
					if ( critical && !dd.parallel ) break ;           // if a critical dep needs reconstruction, do not proceed past parallel deps
					Node d{dn} ;
					if (!d->done(req,NodeGoal::Status)) {
						nds.insert(d) ;
						critical |= dd.dflags[Dflag::Critical] ;      // note critical flag to stop processing once parallel deps are exhausted
						if (!nds) trace("not_done",dn) ;
					} else if (!d->up_to_date(dd)) {
						trace("diff",dn) ;
						goto Miss ;
					}
				}
				if (!nds) {
					trace("hit",r) ;
					return { .completed=true , .hit=Yes , .id{r} } ;  // hit
				}
				if (!found) {
					found    = true        ;
					new_deps = ::move(nds) ;                          // do as if new_deps contains the whole world
				} else {
					for( auto it=new_deps.begin() ; it!=new_deps.end() ;)
						if (nds.contains(*it))                it++  ;
						else                   new_deps.erase(it++) ; // /!\ be careful with erasing while iterating : increment it before erasing is done at it before increment
				}
			Miss : ;                                                  // missed for this entry, try next one
			}
		} catch (::string const&) {                                   // if directory does not exist, it is as it was empty
			trace("dir_not_found") ;
		}
		if (!found) {
			trace("miss") ;
			return { .completed=true , .hit=No } ;
		}
		// demonstration that new_deps is not empty :
		// - the name of a dep is determined by the content of the previous ones
		// - hence if an entry match the done deps, the first non-done dep is fully determined
		// - hence it is the same for all such entries
		// - and this dep belongs to new_deps
		SWEAR(+new_deps) ;
		trace("deps",new_deps) ;
		return { .completed=true , .hit=Maybe , .new_deps{::mk_vector(new_deps)} } ;
	}

	JobInfo DirCache::download( Job job , Id const& id , JobReason const& reason , NfsGuard& nfs_guard ) {
		::string                         jn         = _unique_name_s(job)+id  ;
		::string                         jn_s       = jn+'/'                  ;
		AcFd                             dfd        { dir_fd , jn , Fd::Dir } ;
		size_t                           n_copied   = 0                       ;
		::pair<JobInfo,::vector<size_t>> meta_data  ;
		JobInfo         &                job_info   = meta_data.first         ;
		::vector<size_t>&                target_szs = meta_data.second        ;
		JobDigest       &                digest     = job_info.end.digest     ;
		Trace trace("DirCache::download",job,id,jn) ;
		try {
			{	LockedFd lock { dfd , false/*exclusive*/ } ;                     // because we read the data, shared is ok
				deserialize(AcFd(dfd,"meta_data"s).read(),meta_data) ;
				// update some info
				job_info.start.pre_start.job       = +job   ;                    // id is not stored in cache
				job_info.start.submit_attrs.reason = reason ;
				//
				AcFd data_fd { ::openat( dfd , "data" , O_RDONLY|O_NOFOLLOW|O_CLOEXEC ) } ;
				for( NodeIdx ti : iota(digest.targets.size()) ) {
					auto&           entry = digest.targets[ti]     ;
					::string const& tn    = entry.first            ;
					FileTag         tag   = entry.second.sig.tag() ;
					n_copied = ti+1 ;
					nfs_guard.change(tn) ;
					trace("copy",ti,tn) ;
					unlnk(dir_guard(tn),true/*dir_ok*/) ;
					switch (tag) {
						case FileTag::Lnk   : lnk( tn , data_fd.read(target_szs[ti]) )                                                                                     ; break ;
						case FileTag::Empty :             AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , 0666 ))                                        ; break ;
						case FileTag::Reg   : ::sendfile( AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , 0666 )) , data_fd , nullptr , target_szs[ti] ) ; break ;
						case FileTag::Exe   : ::sendfile( AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , 0777 )) , data_fd , nullptr , target_szs[ti] ) ; break ;
					DN}
					FileSig sig { tn } ;
					entry.second.sig = sig ;                                     // target digest is not stored in cache
				}
				digest.end_date = New ;                                          // date must be after files are copied
			}
			// ensure we take a single lock at a time to avoid deadlocks
			// upload is the only one to take several locks
			{	LockedFd lock2 { dir_fd , true /*exclusive*/ } ;                 // because we manipulate LRU, need exclusive
				Sz sz_ = _lru_remove(jn_s) ;
				_lru_first(jn_s,sz_) ;
				trace("done",sz_) ;
			}
			return job_info ;
		} catch(::string const& e) {
			for( NodeIdx ti : iota(n_copied) ) unlnk(digest.targets[ti].first) ; // clean up partial job
			trace("failed") ;
			throw e ;
		}
	}

	bool/*ok*/ DirCache::upload( Job job , NfsGuard& nfs_guard ) {                                 // XXX : defer upload in a dedicated thread (attention : check file integrity after upload)
		::string jn_s = _unique_name_s(job)+repo_s ;
		Trace trace("DirCache::upload",job,jn_s) ;
		//
		// START_OF_VERSIONING
		::pair<JobInfo,::vector<size_t>> meta_data ;
		// END_OF_VERSIONING
		JobInfo         & job_info   = meta_data.first     ;
		::vector<size_t>& target_szs = meta_data.second    ;
		JobDigest       & digest     = job_info.end.digest ;
		job_info = job.job_info() ;
		if (!( +job_info.start || +job_info.end )) {                                               // we need a full report to cache job
			trace("no_ancillary_file") ;
			return false/*ok*/ ;
		}
		// check deps
		for( auto const& [dn,dd] : digest.deps ) if (!dd.is_crc) return false/*ok*/ ;
		//
		mk_dir_s(dir_fd,jn_s) ;
		AcFd dfd { dir_fd , jn_s , Fd::Dir } ;
		//
		// upload is the only one to take several locks and it starts with the global lock
		// this way, we are sure to avoid deadlocks
		LockedFd lock2{ dir_fd , true/*exclusive*/ } ;                                             // because we manipulate LRU and because we take several locks, need exclusive
		LockedFd lock { dfd    , true/*exclusive*/ } ;                                             // because we write the data , need exclusive
		//
		Sz old_sz = _lru_remove(jn_s) ;
		Sz new_sz = 0                 ;
		unlnk_inside_s(dfd) ;
		//
		bool made_room = false ;
		try {
			::vector<FileSig> target_sigs ;                                                        // keep sigs to check *after* copy to ensure reliability
			// remove useless info
			job_info.start.pre_start.seq_id    = 0  ;                                              // no seq_id   since no execution
			job_info.start.start    .small_id  = 0  ;                                              // no small_id since no execution
			job_info.start.pre_start.job       = 0  ;                                              // job_id may not be the same in the destination repo
			job_info.start.eta                 = {} ;                                              // dont care about timing info in cache
			job_info.start.submit_attrs.reason = {} ;                                              // cache does not care about original reason
			job_info.start.rsrcs.clear() ;                                                         // caching resources is meaningless as they have no impact on content
			target_szs .reserve(digest.targets.size()) ;
			target_sigs.reserve(digest.targets.size()) ;
			for( auto& [tn,td] : digest.targets ) {
				SWEAR(!td.pre_exist) ;                                                             // else cannot be a candidate for upload as this must have failed
				size_t sz = td.sig.tag()==FileTag::Empty ? 0 : FileInfo(nfs_guard.access(tn)).sz ; // fast path : no need to access empty files as we know size is 0
				target_szs .push_back(sz    ) ;
				target_sigs.push_back(td.sig) ;                                                    // save info before it is cleared
				new_sz          += sz           ;
				td.sig           = td.sig.tag() ;                                                  // forget date, just keep tag
				td.extra_tflags  = {}           ;
			}
			digest.end_date = {} ;
			// store meta-data
			// START_OF_VERSIONING
			::string meta_data_str = serialize(::pair(job_info,target_szs)) ;
			::string deps_str      = serialize(digest.deps)                 ;
			AcFd(dfd,"meta_data",FdAction::CreateReadOnly).write(meta_data_str) ;
			AcFd(dfd,"deps"     ,FdAction::CreateReadOnly).write(deps_str     ) ;                  // store deps in a compact format so that matching is fast
			// END_OF_VERSIONING
			new_sz += meta_data_str.size() ;
			new_sz += deps_str     .size() ;
			_mk_room(old_sz,new_sz) ;
			made_room = true ;
			// store data
			// START_OF_VERSIONING
			AcFd data_fd { dfd , "data" , FdAction::CreateReadOnly } ;                             // data is created R/O to ensure no spurious further modifications
			for( NodeIdx ti : iota(digest.targets.size()) ) {
				::pair_s<TargetDigest> const& entry = digest.targets[ti]     ;
				::string               const& tn    = entry.first            ;
				FileTag                       tag   = entry.second.sig.tag() ;
				trace("copy",tn,ti) ;
				switch (tag) {
					case FileTag::Lnk : data_fd.write(read_lnk(tn))                                 ; break ;
					case FileTag::Reg :
					case FileTag::Exe : ::sendfile( data_fd , AcFd(tn) , nullptr , target_szs[ti] ) ; break ;
				DN}
				throw_unless( FileSig(tn)==target_sigs[ti] , "unstable ",tn ) ;                    // ensure cache entry is reliable by checking file *after* copy
			}
			// END_OF_VERSIONING
		} catch (::string const& e) {
			trace("failed",e) ;
			unlnk_inside_s(dfd) ;                                                                  // clean up in case of partial execution
			_mk_room( made_room?new_sz:old_sz , 0 ) ;                                              // finally, we did not populate the entry
			return false/*ok*/ ;
		}
		_lru_first(jn_s,new_sz) ;
		trace("done",new_sz) ;
		return true/*ok*/ ;
	}

}
