// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// XXX : ensure access rights (including group to which created files belong) can be dealt with by setting adequate permissions/acl on the cache dir
// XXX : make files read-only to avoid contamination

// cache format :
//	- Lru contains the prev (more recent) and next (less recent) pointers and the size of the entry (total size for the head Lru)
//	- global info :
//		- LMAKE/lru : prev is least recently used, next is most recently used, sz is total size
//	- job_dir : <job>/<repo_crc> where :
//		- <job> is made after its name with suffixes replaced by readable suffixes and rule idx by rule crc
//		- <repo_crc> is computed after the repo as indicated in config.repo
//	- each job has :
//		- lru info  in <job_dir>/lru
//		- meta-data in <job_dir>/data (the content of job.ancillary_file())
//		- deps crcs in <job_dir>/deps (in same order as in meta-data)
//		- data in <job_dir/<target_id>
//			- target_id is the index of target as seen in meta-data
//			- may be a regular file or a link

#include <grp.h>

#include "dir_cache.hh"

using namespace Disk ;

namespace Caches {

	struct Lru {
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,prev) ;
			::serdes(s,next) ;
			::serdes(s,sz  ) ;
		}
		// data
		::string     prev = "LMAKE" ;
		::string     next = "LMAKE" ;
		DirCache::Sz sz   = 0       ;  // size of entry, or overall size for head
	} ;

	void DirCache::config(Config::Cache const& config) {
		::map_ss dct = mk_map(config.dct) ;
		//
		Hash::Xxh repo_hash ;
		if (dct.contains("repo")) repo_hash.update(dct.at("repo"))         ; else throw "repo not found"s ;
		if (dct.contains("dir" )) dir       =      dct.at("dir" )          ; else throw "dir not found"s  ;
		if (dct.contains("size")) sz        = atol(dct.at("size").c_str()) ; else throw "size not found"s ;
		//
		if (dct.contains("group")) {
			char*           end = nullptr         ;
			::string const& g   = dct.at("group") ;
			group = strtol( g.c_str() , &end , 0 ) ;
			if (*end!=0) {
				struct group* gs = getgrnam(g.c_str()) ;
				if (!gs) throw to_string("cannot find group ",g) ;
				group = gs->gr_gid ;
			}
		}
		repo   = ::string(repo_hash.digest()) ;
		dir_fd = open_read(dir)     ;
		dir_fd.no_std() ;                                                      // avoid poluting standard descriptors
	}

	static ::string _unique_name(Job job) {
		Rule     rule      = job->rule                              ;
		::string full_name = job.full_name()                        ; SWEAR(Rule(full_name)==rule) ; // only name suffix is considered to make Rule
		size_t   user_sz   = full_name.size() - rule->job_sfx_len() ;
		::string res       = full_name.substr(0,user_sz)            ; res.reserve(res.size()+1+rule->n_static_stems*(2*(3+1))+16) ; // allocate 2x3 digits per stem, this is comfortable
		//
		for( char& c : res ) if (c==Rule::StarMrkr) c = '*' ;
		res.push_back('/') ;
		//
		char* p = &full_name[user_sz+1] ;                                      // start of suffix
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			FileNameIdx pos = to_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = to_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			append_to_string( res , pos , '-' , sz , '+' ) ;
		}
		append_to_string(res,::string(rule->cmd_crc)) ;
		return res ;
	}
	static inline ::string _unique_name( Job job , ::string const& repo ) { return to_string(_unique_name(job),'/',repo) ; }

	void DirCache::_mk_room( Sz old_sz , Sz new_sz ) {
		::string   head_file   = to_string(dir,"/LMAKE/lru")                         ;
		::ifstream head_stream { head_file }                                         ;
		Lru        head        = head_stream ? deserialize<Lru>(head_stream) : Lru() ;
		//
		SWEAR(head.sz>=old_sz) ;
		head.sz -= old_sz ;
		while (head.sz+new_sz>sz) {
			Lru here = deserialize<Lru>(IFStream(to_string(dir,'/',head.prev,"/lru"))) ;
			unlink(dir_fd,head.prev) ;
			SWEAR(head.sz>here.sz) ;
			head.sz   -= here.sz           ;
			head.prev  = ::move(here.prev) ;
		}
		head.sz += new_sz ;
		SWEAR(head.sz<=sz) ;
		//
		if (head.prev!="LMAKE") {
			::string last_file = to_string(dir,'/',head.prev,"/lru")   ;
			Lru      last      = deserialize<Lru>(IFStream(last_file)) ;
			last.next = "LMAKE" ;
			serialize(OFStream(last_file),last) ;
		}
		dir_guard(head_file) ;
		serialize(OFStream(head_file),head) ;
	}

	static DirCache::Sz _copy( Fd src_at , ::string const& src_file , Fd dst_at , ::string const& dst_file , bool unlink_dst ) {
		FileInfo fi{src_at,src_file} ;
		if (unlink_dst) unlink(dst_at,dst_file) ;
		else            SWEAR(!is_target(dst_at,dst_file)) ;
		switch (fi.tag) {
			case FileTag::None :
			return 0 ;
			case FileTag::Reg :
			case FileTag::Exe : {
				FileMap     fm  { src_at , src_file } ;
				AutoCloseFd wfd = open_write(dst_at,dst_file,false/*append*/,fi.tag==FileTag::Exe) ;
				for( size_t pos=0 ; pos<fm.sz ;) {
					ssize_t cnt = ::write(wfd,fm.data+pos,fm.sz-pos) ;
					if (cnt<=0) throw ""s ;
					pos += cnt ;
				}
				return fm.sz ;
			}
			case FileTag::Lnk : {
				::string target = read_lnk(src_at,src_file) ;
				dir_guard(dst_at,dst_file) ;
				lnk(dst_at,dst_file,target) ;
				return target.size() ;
			}
			default : FAIL(fi.tag) ;
		}
	}
	static inline DirCache::Sz _copy(             ::string const& src_file , Fd dst_at , ::string const& dst_file , bool ud ) { return _copy( Fd::Cwd , src_file , dst_at  , dst_file , ud ) ; }
	static inline DirCache::Sz _copy( Fd src_at , ::string const& src_file ,             ::string const& dst_file , bool ud ) { return _copy( src_at  , src_file , Fd::Cwd , dst_file , ud ) ; }
	static inline DirCache::Sz _copy(             ::string const& src_file ,             ::string const& dst_file , bool ud ) { return _copy( Fd::Cwd , src_file , Fd::Cwd , dst_file , ud ) ; }

	DirCache::Sz DirCache::_lru_remove(::string const& entry) {
		::ifstream here_stream = ::ifstream(to_string(dir,'/',entry,"/lru")) ;
		if (!here_stream) throw ""s ;
		Lru here = deserialize<Lru>(here_stream) ;
		//
		::string prev_file = to_string(dir,'/',here.prev,"/lru") ;
		::string next_file = to_string(dir,'/',here.next,"/lru") ;
		Lru prev = deserialize<Lru>(IFStream  (prev_file)) ;
		Lru next = deserialize<Lru>(IFStream  (next_file)) ;
		//
		prev.next = here.next ;
		next.prev = here.prev ;
		//
		serialize(OFStream(prev_file),prev) ;
		serialize(OFStream(next_file),next) ;
		return here.sz ;
	}

	void DirCache::_lru_first( ::string const& entry , Sz sz ) {
		::string head_file  = to_string(dir,"/LMAKE/lru")            ;
		Lru      head       = deserialize<Lru>(IFStream(head_file))  ;
		::string first_file = to_string(dir,'/',head.next,"/lru")    ;
		::string here_file  = to_string(dir,'/',entry    ,"/lru")    ;
		Lru      first      = deserialize<Lru>(IFStream(first_file)) ;
		Lru      here       { .next{head.next} , .sz{sz} }           ;
		//
		head .next = entry ;
		first.prev = entry ;
		//
		serialize(OFStream(head_file ),head ) ;
		serialize(OFStream(first_file),first) ;
		serialize(OFStream(here_file ),here ) ;
	}

	Cache::Match DirCache::match( Job job , Req req ) {
		::string     name     = _unique_name(job)             ;
		::uset<Node> new_deps ;
		LockedFd     lock     { dir_fd , false/*exclusive*/ } ;                // XXX : improve locking by using job-locks
		bool         found    = false                         ;
		//
		try {
			for( ::string const& r : lst_dir(dir_fd,name) ) {
				::uset<Node>  nds  ;
				::vmap_s<Crc> deps = deserialize<::vmap_s<Crc>>(IFStream(to_string(dir,'/',name,'/',r,"/deps"))) ;
				//
				for( auto const& [dn,crc] : deps ) {
					Node d{dn} ;
					if      (!d.done(req)      ) nds.insert(d) ;
					else if (!crc.match(d->crc)) goto Miss ;
				}
				if (nds.empty()) {
					Trace trace("match","hit",job,r) ;
					return { .completed=true , .hit=Yes , .id{r} } ;  // hit
				}
				if (!found) {
					found    = true        ;
					new_deps = ::move(nds) ;                                       // do as if new_deps contains the whole world
				} else {
					for( auto it=new_deps.begin() ; it!=new_deps.end() ;)
						if (nds.contains(*it))                it++  ;
						else                   new_deps.erase(it++) ;              // /!\ be careful with erasing while iterating : increment it before erasing is done at it before increment
				}
			Miss : ;
			}
		} catch (::string const&) {}                                           // if directory does not exist, it is as it was empty
		if (!found) {
			Trace trace("match","miss",job) ;
			return { .completed=true , .hit=No } ;
		}
		// demonstration that new_deps is not empty :
		// - the name of a dep is determined by the content of the previous ones
		// - hence if an entry match the done deps, the first non-done dep is fully determined
		// - hence it is the same for all such entries
		// - and this dep belongs to new_deps
		SWEAR(!new_deps.empty()) ;
		Trace trace("match","deps",job,new_deps) ;
		return { .completed=true , .hit=Maybe , .new_deps{::mk_vector(new_deps)} } ;
	}

	JobDigest DirCache::download( Job job , Id const& id ) {
		Trace trace("download",job,id) ;
		::string    jn     = _unique_name(job,id)            ;
		LockedFd    lock   { dir_fd , true/*exclusive*/ }    ;             // because we manipulate LRU, lock must be exclusive
		AutoCloseFd dfd    = open_read(dir_fd,jn)            ;
		IFStream    stream { to_string(dir,'/',jn,"/data") } ;
		//
		/**/               deserialize<JobRpcReq  >(stream) ;              // unused
		/**/               deserialize<JobRpcReply>(stream) ;              // .
		JobDigest digest = deserialize<JobRpcReq  >(stream).digest ;
		//
		/**/                                                  _copy( dfd , "data"        , job.ancillary_file()     , true/*unlink_dst*/ ) ; // XXX : record job is a hit and do not copy reason
		for( NodeIdx ti=0 ; ti<digest.targets.size() ; ti++ ) _copy( dfd , to_string(ti) , digest.targets[ti].first , true/*unlink_dst*/ ) ;
		//
		_lru_first(jn,_lru_remove(jn)) ;
		return digest ;
	}

	bool DirCache::upload( Job job , JobDigest const& digest ) {
		::string jn     = _unique_name(job,repo)       ;
		Sz       old_sz = 0                            ;
		Sz       new_sz = 0                            ;
		LockedFd lock   { dir_fd , true/*exclusive*/ } ;
		//
		try {
			old_sz = _lru_remove(jn) ;
			unlink_inside(dir_fd,jn) ;
		} catch (::string const&) {
			make_dir(dir_fd,jn) ;
		}
		AutoCloseFd dfd = open_read( dir_fd , jn ) ;
		//
		try {
			::string ancillary_file = job.ancillary_file() ;
			/**/                                       new_sz += FileInfo(ancillary_file).sz ;
			for( auto const& [tn,_] : digest.targets ) new_sz += FileInfo(tn            ).sz ;
			if (new_sz>old_sz) _mk_room(old_sz,new_sz) ;
			/**/                                                  _copy( job.ancillary_file()     , dfd , "data"        , false/*unlink_dst*/ ) ;
			for( NodeIdx ti=0 ; ti<digest.targets.size() ; ti++ ) _copy( digest.targets[ti].first , dfd , to_string(ti) , false/*unlink_dst*/ ) ;
		} catch (::string const&) {
			unlink_inside(dir_fd,jn) ;
			_mk_room(new_sz,0) ;                                               // finally, we did not populate the entry
			return false ;
		}
		//
		::vmap_s<Crc> deps ;
		for( auto const& [jn,dd] : digest.deps ) deps.emplace_back(jn,Node(jn)->crc) ;
		serialize( OFStream(to_string(dir,'/',jn,"/deps")) , deps ) ;
		//
		_lru_first(jn,new_sz) ;
		return true ;
	}

}
