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
//		- lru info  in <job_dir>/lru
//		- meta-data in <job_dir>/data (the content of job.ancillary_file() with dep crc's instead of dep dates)
//		- deps crcs in <job_dir>/deps (in same order as in meta-data)
//		- data in <job_dir>/<target_id>
//			- target_id is the index of target as seen in meta-data
//			- may be a regular file or a link

#include "dir_cache.hh"

using namespace Disk ;

namespace Caches {

	// START_OF_VERSIONING

	struct Lru {
		::string     prev = DirCache::Head ;
		::string     next = DirCache::Head ;
		DirCache::Sz sz   = 0              ; // size of entry, or overall size for head
	} ;

	void DirCache::chk(ssize_t delta_sz) const {
		::ifstream head_stream   { _lru_file(Head) } ;
		Lru        head          ;                     if (head_stream) deserialize(head_stream,head) ;
		::uset_s   seen          ;
		::string   expected_prev = Head              ;
		size_t     total_sz      = 0                 ;
		for( ::string entry=head.next ; entry!=Head ;) {
			auto here = deserialize<Lru>(IFStream(_lru_file(entry))) ;
			//
			SWEAR(seen.insert(entry).second,entry) ;
			SWEAR(here.prev==expected_prev ,entry) ;
			total_sz      += here.sz ;
			expected_prev  = entry   ;
			//
			entry          = here.next ;
		}
		SWEAR(head.prev==expected_prev    ,Head                     ) ;
		SWEAR(head.sz  ==total_sz+delta_sz,head.sz,total_sz,delta_sz) ;
	}

	// END_OF_VERSIONING

	void DirCache::config(Config::Cache const& config) {
		::map_ss dct = mk_map(config.dct) ;
		//
		Hash::Xxh repo_hash ;
		if (dct.contains("repo")) repo_hash.update(dct.at("repo")) ; else throw "repo not found"s ;
		if (dct.contains("dir" )) dir =            dct.at("dir" )  ; else throw "dir not found"s  ;
		repo   = "repo-"+::string(repo_hash.digest()) ;
		//
		try                     { chk_version(true/*may_init*/,to_string(dir,'/',AdminDir)) ;       }
		catch (::string const&) { throw to_string("cache version mismatch, running without ",dir) ; }
		//
		dir_fd = open_read(dir)                               ; dir_fd.no_std() ;       // avoid poluting standard descriptors
		if (!dir_fd) throw to_string("cannot configure cache ",dir," : no directory") ;
		sz = from_string_with_units<size_t>(strip(read_content(to_string(dir,'/',AdminDir,"/size")))) ;
	}

	// START_OF_VERSIONING

	static ::string _unique_name(Job job) {
		Rule     rule      = job->rule                              ;
		::string full_name = job->full_name()                       ; SWEAR( Rule(full_name)==rule , full_name , rule->name ) ;     // only name suffix is considered to make Rule
		size_t   user_sz   = full_name.size() - rule->job_sfx_len() ;
		::string res       = full_name.substr(0,user_sz)            ; res.reserve(res.size()+1+rule->n_static_stems*(2*(3+1))+16) ; // allocate 2x3 digits per stem, this is comfortable
		//
		for( char& c : res ) if (c==Rule::StarMrkr) c = '*' ;
		res.push_back('/') ;
		//
		char* p = &full_name[user_sz+1] ;                                                                                           // start of suffix
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			FileNameIdx pos = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			append_to_string( res , pos , '-' , sz , '+' ) ;
		}
		append_to_string(res,"rule-",::string(rule->cmd_crc)) ;
		return res ;
	}
	static ::string _unique_name( Job job , ::string const& repo ) { return to_string(_unique_name(job),'/',repo) ; }
	// END_OF_VERSIONING

	void DirCache::_mk_room( Sz old_sz , Sz new_sz ) {
		if (new_sz>sz) throw to_string("cannot store entry of size ",new_sz," in cache of size ",sz) ;
		//
		::string   head_file     = _lru_file(Head) ;
		::ifstream head_stream   { head_file }     ;
		Lru        head          ;                   if (head_stream) deserialize(head_stream,head) ;
		bool       some_removed  = false           ;
		::string   expected_next = Head            ;                        // for assertion only
		//
		SWEAR( head.sz>=old_sz , head.sz , old_sz ) ;                       // total size contains old_sz
		head.sz -= old_sz ;
		while (head.sz+new_sz>sz) {
			SWEAR(head.prev!=Head) ;                                        // else this would mean an empty cache and we know an empty cache can accept new_sz
			auto here = deserialize<Lru>(IFStream(_lru_file(head.prev))) ;
			SWEAR( here.next==expected_next , here.next , expected_next ) ;
			SWEAR( head.sz  >=here.sz       , head.sz   , here.sz       ) ; // total size contains this entry
			unlnk(dir_fd,head.prev,true/*dir_ok*/) ;
			expected_next  = head.prev         ;
			head.sz       -= here.sz           ;
			head.prev      = ::move(here.prev) ;
			some_removed   = true              ;
		}
		head.sz += new_sz ;
		SWEAR( head.sz<=sz , head.sz , sz ) ;
		//
		if (some_removed) {
			if (head.prev==Head) {
				head.next = Head ;
			} else {
				::string last_file = _lru_file(head.prev)                  ;
				auto     last      = deserialize<Lru>(IFStream(last_file)) ;
				last.next = Head ;
				serialize(OFStream(last_file),last) ;
			}
		}
		serialize( OFStream(dir_guard(head_file)) , head ) ;
	}

	static void _copy( Fd src_at , ::string const& src_file , Fd dst_at , ::string const& dst_file , bool unlnk_dst , bool mk_read_only ) {
		FileTag tag = FileInfo(src_at,src_file).tag() ;
		if (unlnk_dst) unlnk(dst_at,dst_file)                                         ;
		else           SWEAR( !is_target(dst_at,dst_file) , '@',dst_at,':',dst_file ) ;
		switch (tag) {
			case FileTag::None : break ;
			case FileTag::Reg  :
			case FileTag::Exe  : {
				FileMap     fm  { src_at , src_file } ;
				AutoCloseFd wfd = open_write( dst_at , dst_file , false/*append*/ , tag==FileTag::Exe , mk_read_only ) ;
				for( size_t pos=0 ; pos<fm.sz ;) {
					ssize_t cnt = ::write( wfd , fm.data+pos , fm.sz-pos ) ;
					if (cnt<=0) throw ""s ;
					pos += cnt ;
				}
			}
			break ;
			case FileTag::Lnk : {
				::string target = read_lnk(src_at,src_file) ;
				dir_guard(dst_at,dst_file) ;
				lnk( dst_at , dst_file , target ) ;
			}
			break ;
		DF}
	}
	static void _copy(             ::string const& src_file , Fd dst_at , ::string const& dst_file , bool ud , bool ro ) { _copy( Fd::Cwd , src_file , dst_at  , dst_file , ud , ro ) ; }
	static void _copy( Fd src_at , ::string const& src_file ,             ::string const& dst_file , bool ud , bool ro ) { _copy( src_at  , src_file , Fd::Cwd , dst_file , ud , ro ) ; }

	DirCache::Sz DirCache::_lru_remove(::string const& entry) {
		SWEAR(entry!=Head) ;
		//
		::ifstream here_stream { _lru_file(entry) }            ; if (!here_stream) return 0 ; // nothing to remove
		auto       here        = deserialize<Lru>(here_stream) ;
		if (here.prev==here.next) {
			::string pn_file = _lru_file(here.prev)                ;
			auto     pn      = deserialize<Lru>(IFStream(pn_file)) ;
			//
			pn.next = here.next ;
			pn.prev = here.prev ;
			//
			serialize(OFStream(pn_file),pn) ;
		} else {
			::string   prev_file   = _lru_file(here.prev)                  ;
			::string   next_file   = _lru_file(here.next)                  ;
			auto       prev        = deserialize<Lru>(IFStream(prev_file)) ;
			auto       next        = deserialize<Lru>(IFStream(next_file)) ;
			//
			prev.next = here.next ;
			next.prev = here.prev ;
			//
			serialize(OFStream(prev_file),prev) ;
			serialize(OFStream(next_file),next) ;
		}
		return here.sz ;
	}

	void DirCache::_lru_first( ::string const& entry , Sz sz_ ) {
		SWEAR(entry!=Head) ;
		//
		::string head_file  = _lru_file(Head)                        ;
		auto     head       = deserialize<Lru>(IFStream(head_file))  ;
		::string here_file  = _lru_file(entry)                       ;
		Lru      here       { .next=head.next , .sz=sz_ }            ;
		if (head.next==Head) {
			head.next = entry ;
			head.prev = entry ;
		} else {
			::string first_file = _lru_file(head.next)                   ;
			auto     first      = deserialize<Lru>(IFStream(first_file)) ;
			head .next = entry ;
			first.prev = entry ;
			serialize(OFStream(first_file),first) ;
		}
		serialize(OFStream(head_file),head ) ;
		serialize(OFStream(here_file),here ) ;
	}

	Cache::Match DirCache::match( Job job , Req req ) {
		Trace trace("DirCache::match",job,req) ;
		::string     jn       = _unique_name(job)             ;
		::uset<Node> new_deps ;
		AutoCloseFd  dfd      =  open_read(dir_fd,jn)         ;
		LockedFd     lock     { dfd    , false/*exclusive*/ } ;
		bool         found    = false                         ;
		//
		try {
			for( ::string const& r : lst_dir(dfd) ) {
				::uset<Node> nds      ;
				auto         deps     = deserialize<::vmap_s<DepDigest>>(IFStream(to_string(dir,'/',jn,'/',r,"/deps"))) ;
				bool         critical = false ;
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
		} catch (::string const&) {}                                  // if directory does not exist, it is as it was empty
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
		::string    jn     = _unique_name(job,id) ;
		AutoCloseFd dfd    = open_read(dir_fd,jn) ;
		::vector_s  copied ;
		Trace trace("DirCache::download",job,id,jn) ;
		try {
			JobInfo job_info ;
			{	LockedFd lock { dfd , false/*exclusive*/ } ;                                        // because we read the data , shared is ok
				job_info = { to_string(dir,'/',jn,"/data") } ;
				// update some info
				job_info.start.pre_start.job       = +job   ;                                       // id is not stored in cache
				job_info.start.submit_attrs.reason = reason ;
				//
				for( NodeIdx ti=0 ; ti<job_info.end.end.digest.targets.size() ; ti++ ) {
					auto&           entry = job_info.end.end.digest.targets[ti] ;
					::string const& tn    = entry.first                         ;
					copied.push_back(tn) ;
					nfs_guard.change(tn) ;
					_copy( dfd , to_string(ti) , tn , true/*unlnk_dst*/ , false/*mk_read_only*/ ) ;
					entry.second.sig = FileSig(tn) ;                                                // target digest is not stored in cache
				}
				job_info.end.end.digest.end_date = New ;                                            // date must be after files are copied
			}
			// ensure we take a single lock at a time to avoid deadlocks
			// upload is the only one to take several locks
			{	LockedFd lock2 { dir_fd , true /*exclusive*/ } ;                                    // because we manipulate LRU, need exclusive
				Sz sz_ = _lru_remove(jn) ;
				_lru_first(jn,sz_) ;
				trace("done",sz_) ;
			}
			return job_info ;
		} catch(::string const& e) {
			for( ::string const& f : copied ) unlnk(f) ;                                            // clean up partial job
			trace("failed") ;
			throw e ;
		}
	}

	bool/*ok*/ DirCache::upload( Job job , JobDigest const& digest , NfsGuard& nfs_guard ) { // XXX : defer upload in a dedicated thread
		::string jn = _unique_name(job,repo) ;
		Trace trace("DirCache::upload",job,jn) ;
		//
		JobInfo job_info = job.job_info() ;
		if (!job_info.end.end.proc) {                                     // we need a full report to cache job
			trace("no_ancillary_file") ;
			return false/*ok*/ ;
		}
		// remove useless info
		job_info.start.pre_start.seq_id    = 0  ;                         // no seq_id   since no execution
		job_info.start.start    .small_id  = 0  ;                         // no small_id since no execution
		job_info.start.pre_start.job       = 0  ;                         // job_id may not be the same in the destination repo
		job_info.start.eta                 = {} ;                         // dont care about timing info in cache
		job_info.start.submit_attrs.reason = {} ;                         // cache does not care about original reason
		job_info.start.rsrcs.clear() ;                                    // caching resources is meaningless as they have no impact on content
		for( auto& [tn,td] : job_info.end.end.digest.targets ) {
			SWEAR(!td.pre_exist) ;                                        // cannot be a candidate for upload as this must have failed
			td.sig          = {} ;
			td.extra_tflags = {} ;
		}
		job_info.end.end.digest.end_date = {} ;
		// check deps
		for( auto const& [dn,dd] : job_info.end.end.digest.deps ) if (!dd.is_crc) return false/*ok*/ ;
		//
		mk_dir(dir_fd,jn) ;
		AutoCloseFd dfd = open_read(dir_fd,jn) ;
		//
		// upload is the only one to take several locks and it starts with the global lock
		// this way, we are sure to avoid deadlocks
		LockedFd lock2{ dir_fd , true/*exclusive*/ } ;                    // because we manipulate LRU and because we take several locks, need exclusive
		LockedFd lock { dfd    , true/*exclusive*/ } ;                    // because we write the data , need exclusive
		//
		Sz old_sz = _lru_remove(jn) ;
		Sz new_sz = 0               ;
		unlnk_inside(dfd) ;
		//
		bool made_room = false ;
		try {
			// store meta-data
			::string data_file = to_string(dir,'/',jn,"/data") ;
			::string deps_file = to_string(dir,'/',jn,"/deps") ;
			//
			job_info.write(data_file) ;
			serialize(OFStream(deps_file),job_info.end.end.digest.deps) ; // store deps in a compact format so that matching is fast
			//
			/**/                                       new_sz += FileInfo(data_file           ).sz ;
			/**/                                       new_sz += FileInfo(deps_file           ).sz ;
			for( auto const& [tn,_] : digest.targets ) new_sz += FileInfo(nfs_guard.access(tn)).sz ;
			_mk_room(old_sz,new_sz) ;
			made_room = true ;
			for( NodeIdx ti=0 ; ti<digest.targets.size() ; ti++ ) {
				_copy( digest.targets[ti].first , dfd , to_string(ti) , false/*unlnk_dst*/ , true/*mk_read_only*/ ) ;
				// XXX : ensure file is still valid after copy
			}
		} catch (::string const& e) {
			trace("failed",e) ;
			unlnk_inside(dfd) ;                                           // clean up in case of partial execution
			_mk_room( made_room?new_sz:old_sz , 0 ) ;                     // finally, we did not populate the entry
			return false/*ok*/ ;
		}
		_lru_first(jn,new_sz) ;
		trace("done",new_sz) ;
		return true/*ok*/ ;
	}

}
