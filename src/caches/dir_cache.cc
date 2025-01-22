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

// XXX : implement timeout when locking cache

#include <sys/sendfile.h>

#include "app.hh"

#include "dir_cache.hh"

#if HAS_ZLIB
	#define ZLIB_CONST
	#include <zlib.h>
#endif

using namespace Disk ;

struct DeflateFd : AcFd {
	static constexpr size_t BufSz = 1<<16 ;
	// cxtors & casts
	DeflateFd() = default ;
	#if HAS_ZLIB
		DeflateFd( AcFd&& fd , uint8_t lvl_=0 ) : AcFd{::move(fd)} , lvl{lvl_} {
			SWEAR(lvl<=Z_BEST_COMPRESSION) ;
			if (lvl) {
				int rc = deflateInit(&_zs,lvl) ; SWEAR(rc==Z_OK) ;
				_reset_buf() ;
			}
		}
		~DeflateFd() { flush() ; }
	#else
		DeflateFd( AcFd&& fd , uint8_t lvl=0 ) : AcFd{::move(fd)} { SWEAR(!lvl,lvl) ; }
		~DeflateFd() { _flush() ; }
	#endif
	// services
	void write(::string const& s) {
		#if HAS_ZLIB
			SWEAR(!_flushed) ;
			if (lvl) {
				_zs.next_in  = reinterpret_cast<uint8_t const*>(s.data()) ;
				_zs.avail_in = s.size()                                   ;
				while (_zs.avail_in) {
					if (!_zs.avail_out) {
						AcFd::write({_buf,BufSz}) ;
						total_sz += BufSz ;
						_reset_buf() ;
					}
					deflate(&_zs,Z_NO_FLUSH) ;
				}
				return ;
			}
		#endif
		_flush(s.size()) ;
		if      (s.size()>=BufSz) { AcFd::write(s)                              ; total_sz += s.size() ; }                                                           // large data : send directly
		else if (s.size()       ) { ::memcpy( _buf+_pos , s.data() , s.size() ) ; _pos     += s.size() ; }                                                           // small data : put in _buf
	}
	void send_from( Fd fd_ , size_t sz ) {
		#if HAS_ZLIB
			SWEAR(!_flushed) ;
			if (lvl) {
				while (sz) {
					size_t cnt = ::min(sz,BufSz) ;
					::string s = fd_.read(cnt) ; throw_unless(s.size()==cnt,"missing ",cnt-s.size()," bytes from ",fd) ;
					write(s) ;
					sz -= cnt ;
				}
				return ;
			}
		#endif
		_flush(sz) ;
		if      (sz>=BufSz) { SWEAR(!_pos) ; size_t c = ::sendfile(self,fd_,nullptr,sz) ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; total_sz += sz ; } // large data : send directly
		else if (sz       ) {                size_t c = fd_.read_to({_buf+_pos,sz})     ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; _pos     += c  ; } // small data : put in _buf
	}
	void flush() {
		#if HAS_ZLIB
			if (_flushed) return ;
			_flushed = true ;
			if (lvl) {
				_zs.next_in  = nullptr ;
				_zs.avail_in = 0       ;
				for (;;) {
					int    rc = deflate(&_zs,Z_FINISH)                                ;
					size_t sz = size_t(_zs.next_out-reinterpret_cast<uint8_t*>(_buf)) ;
					AcFd::write({_buf,sz}) ;
					total_sz += sz ;
					switch (rc) {
						case Z_OK         :
						case Z_BUF_ERROR  :                    break  ;
						case Z_STREAM_END : deflateEnd(&_zs) ; return ;
					DF}
					_reset_buf() ;
				}
				return ;
			}
		#endif
		_flush() ;
	}
private :
	#if HAS_ZLIB
		void _reset_buf() {
			_zs.next_out  = reinterpret_cast<uint8_t*>(_buf) ;
			_zs.avail_out = BufSz                            ;
		}
	#endif
	void _flush(size_t room=BufSz) {                                                                                                                                 // flush if not enough room
		#if HAS_ZLIB
			SWEAR(!lvl) ;
		#endif
		if (_pos+room<=BufSz) return ;                                                                                                                               // enough room
		if (!_pos           ) return ;                                                                                                                               // _buf is already empty
		AcFd::write({_buf,_pos}) ;
		total_sz += _pos ;
		_pos      = 0    ;
	}
	// data
public :
	Disk::DiskSz total_sz = 0 ;
	#if HAS_ZLIB
		uint8_t lvl = 0 ;
	#endif
private :
	char   _buf[BufSz] ;
	size_t _pos        = 0 ;
	#if HAS_ZLIB
		z_stream _zs      = {}    ;
		bool     _flushed = false ;
	#endif
} ;

struct InflateFd : AcFd {
	static constexpr size_t BufSz = 1<<16 ;
	// cxtors & casts
	InflateFd() = default ;
	#if HAS_ZLIB
		InflateFd( AcFd&& fd , bool lvl_=false ) : AcFd{::move(fd)} , lvl{lvl_} {
			if (lvl) {
				int rc = inflateInit(&_zs) ; SWEAR(rc==Z_OK) ;
				_reset_buf() ;
			}
		}
	#else
		InflateFd( AcFd&& fd , bool lvl=false ) : AcFd{::move(fd)} { SWEAR(!lvl,lvl) ; }
	#endif
	// services
	::string read(size_t sz) {
		::string res ( sz , 0 ) ;
		#if HAS_ZLIB
			if (lvl) {
				_zs.next_out  = reinterpret_cast<uint8_t*>(res.data()) ;
				_zs.avail_out = res.size()                             ;
				while (_zs.avail_out) {
					if (!_zs.avail_in) {
						size_t cnt = AcFd::read_to({_buf,BufSz}) ; throw_unless(cnt,"missing ",_zs.avail_out," bytes from ",self) ;
						_reset_buf(cnt) ;
					}
					inflate(&_zs,Z_NO_FLUSH) ;
				}
				return res ;
			}
		#endif
		size_t   cnt = ::min( sz , _len ) ;
		if (cnt) {                                                                                                           // gather available data from _buf
			::memcpy( res.data() , _buf+_pos , cnt ) ;
			_pos += cnt ;
			_len -= cnt ;
			sz   -= cnt ;
		}
		if (sz) {
			SWEAR(!_len,_len) ;
			if (sz>=BufSz) {                                                                                                 // large data : read directly
				size_t c = AcFd::read_to({&res[cnt],sz}) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
			} else {                                                                                                         // small data : bufferize
				_len = AcFd::read_to({_buf,BufSz}) ;       throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
				::memcpy( &res[cnt] , _buf , sz ) ;
				_pos  = sz ;
				_len -= sz ;
			}
		}
		return res ;
	}
	void send_to( Fd fd_ , size_t sz ) {
		#if HAS_ZLIB
			if (lvl) {
				while (sz) {
					size_t cnt = ::min(sz,BufSz) ;
					::string s = read(cnt) ; SWEAR(s.size()==cnt,s.size(),cnt) ;
					fd_.write(s) ;
					sz -= cnt ;
				}
				return ;
			}
		#endif
		size_t cnt = ::min(sz,_len) ;
		if (cnt) {                                                                                                           // gather available data from _buf
			fd_.write({_buf+_pos,cnt}) ;
			_pos += cnt ;
			_len -= cnt ;
			sz   -= cnt ;
		}
		if (sz) {
			SWEAR(!_len,_len) ;
			if (sz>=BufSz) {                                                                                                 // large data : transfer directly fd to fd
				size_t c = ::sendfile(fd_,self,nullptr,sz) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
			} else {                                                                                                         // small data : bufferize
				_len = AcFd::read_to({_buf,BufSz}) ;         throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
				fd_.write({_buf,sz}) ;
				_pos  = sz ;
				_len -= sz ;
			}
		}
	}
private :
	#if HAS_ZLIB
		void _reset_buf(size_t cnt=0) {
			_zs.next_in  = reinterpret_cast<uint8_t const*>(_buf) ;
			_zs.avail_in = cnt                                    ;
		}
	#endif
	// data
public :
	#if HAS_ZLIB
		bool lvl = false ;
	#endif
private :
	char   _buf[BufSz] ;
	size_t _pos        = 0 ;
	size_t _len        = 0 ;
	#if HAS_ZLIB
		z_stream _zs = {} ;
	#endif
} ;

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

	void DirCache::config(::vmap_ss const& dct) {
		::umap_ss config_map = mk_umap(dct) ;
		//
		if ( auto it=config_map.find("dir") ; it!=config_map.end() ) dir_s = with_slash(it->second) ;
		else                                                         throw "dir not found"s ;
		//
		Hash::Xxh repo_hash ;
		if ( auto it=config_map.find("repo") ; it!=config_map.end() ) repo_hash.update(it->second              ) ;
		else                                                          repo_hash.update(no_slash(*g_repo_root_s)) ;
		repo_s = "repo-"+repo_hash.digest().hex()+'/' ;
		//
		if ( auto it=config_map.find("reliable_dirs") ; it!=config_map.end() ) reliable_dirs = it->second!="False" && it->second!="0" && +it->second ;
		//
		try                     { chk_version(true/*may_init*/,dir_s+AdminDirS) ;                    }
		catch (::string const&) { throw "cache version mismatch, running without "+no_slash(dir_s) ; }
		//
		::string sz_file = cat(dir_s,AdminDirS,"size") ;
		AcFd     sz_fd   { sz_file }                   ;
		throw_unless( +sz_fd , "file ",sz_file," must exist and contain the size of the cache" ) ;
		try                       { sz = from_string_with_unit<size_t>(strip(sz_fd.read())) ; }
		catch (::string const& e) { throw "cannot read "+sz_file+" : "+e ;                    }
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
		::string head_file  = _lru_file(HeadS)                                           ;
		auto     head       = deserialize<Lru>(AcFd(nfs_guard.access(head_file)).read()) ;
		::string here_file  = _lru_file(entry_s)                                         ;
		Lru      here       { .next_s=head.next_s , .sz=sz_ }                            ;
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

	DirCache::Sz DirCache::_reserved_sz( Key key , NfsGuard& nfs_guard ) {
		return deserialize<Sz>(AcFd(nfs_guard.access(cat(dir_s,AdminDirS,"reserved/",key,".sz"))).read()) ;
	}

	Cache::Match DirCache::match( ::string const& job , ::vmap_s<DepDigest> const& repo_deps ) {
		Trace trace("DirCache::match",job) ;
		NfsGuard            nfs_guard    { reliable_dirs }                            ;
		::string            abs_jn_s     = dir_s+job+'/'                              ;
		AcFd                dfd          { nfs_guard.access_dir(abs_jn_s) , Fd::Dir } ;
		LockedFd            lock         { dir_s , false/*exclusive*/ }               ;
		::umap_s<DepDigest> repo_dep_map ;                                                                                                                                // lazy evaluated
		//
		try {
			for( ::string const& r : lst_dir_s(dfd) ) {
				::string deps_file  = abs_jn_s+r+"/deps"                                                         ;
				auto     cache_deps = deserialize<::vmap_s<DepDigest>>(AcFd(nfs_guard.access(deps_file)).read()) ;
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
					return { .completed=true , .hit=Yes , .id{r} } ;                      // hit
				}
				for( NodeIdx i : iota(dvg,cache_deps.size()) ) {
					if ( cache_deps[i].second.dflags[Dflag::Critical] && ! repo_dep_map.contains(cache_deps[i].first) ) {
						cache_deps.resize(i+1) ;
						break ;
					}
				}
				trace("deps",cache_deps) ;
				return { .completed=true , .hit=Maybe , .new_deps{::move(cache_deps)} } ;
			Miss : ;                                                                      // missed this entry, try next one
			}
		} catch (::string const&) {                                                       // if directory does not exist, it is as it was empty
			trace("dir_not_found") ;
		}
		trace("miss") ;
		return { .completed=true , .hit=No } ;
	}

	JobInfo DirCache::download( ::string const& job , Id const& id , JobReason const& reason , NfsGuard& repo_nfs_guard ) {
		NfsGuard                nfs_guard  { reliable_dirs }                              ;
		::string                jn         = job+'/'+id                                   ;
		::string                jn_s       = jn+'/'                                       ;
		AcFd                    dfd        { nfs_guard.access_dir(dir_s+jn_s) , Fd::Dir } ;
		NodeIdx                 n_copied   = 0                                            ;
		JobInfo                 job_info   ;
		JobDigest&              digest     = job_info.end.digest                          ;
		::vmap_s<TargetDigest>& targets    = digest.targets                               ;
		AcFd                    info_fd    ;
		AcFd                    data_fd    ;
		Trace trace("DirCache::download",job,id,jn) ;
		{	LockedFd lock { dir_s , true /*exclusive*/ }    ;                                                                     // because we manipulate LRU, need exclusive
			Sz       sz    = _lru_remove( jn_s , nfs_guard ) ; throw_if( !sz , "no entry ",jn ) ;
			_lru_first( jn_s , sz , nfs_guard ) ;
			info_fd = AcFd( dfd , "info"s ) ; SWEAR(+info_fd) ;                                                                   // _lru_remove worked => everything should be accessible
			data_fd = AcFd( dfd , "data"s ) ; SWEAR(+data_fd) ;                                                                   // .
		}
		try {
			deserialize(info_fd.read(),job_info) ;
			#if !HAS_ZLIB
				throw_if( job_info.start.start.z_lvl , "cannot uncompress without zlib" ) ;
			#endif
			// update some info
			job_info.start.submit_attrs.reason = reason ;
			//
			NodeIdx   n_targets = targets.size()                                            ;
			auto      mode      = [](FileTag t)->int { return t==FileTag::Exe?0777:0666 ; } ;
			InflateFd data_fd   { AcFd(dfd,"data"s) , bool(job_info.start.start.z_lvl) }    ;
			//
			::string     target_szs_str = data_fd.read(n_targets*sizeof(Sz)) ;
			::vector<Sz> target_szs     ( n_targets )                        ; for( NodeIdx ti : iota(n_targets) ) ::memcpy( &target_szs[ti] , &target_szs_str[ti*sizeof(Sz)] , sizeof(Sz) ) ;
			for( NodeIdx ti : iota(n_targets) ) {
				auto&           entry = targets[ti]            ;
				::string const& tn    = entry.first            ;
				FileTag         tag   = entry.second.sig.tag() ;
				n_copied = ti+1 ;
				repo_nfs_guard.change(tn) ;
				unlnk(dir_guard(tn),true/*dir_ok*/) ;
				switch (tag) {
					case FileTag::Lnk   : trace("lnk_to"  ,tn,sz) ; lnk( tn , data_fd.read(target_szs[ti]) )                                       ; break ;
					case FileTag::Empty : trace("empty_to",tn   ) ; AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , mode(tag) )) ; break ;
					case FileTag::Exe   :
					case FileTag::Reg   :
						if ( size_t sz = target_szs[ti] ) {
							trace("write_to",tn,sz) ;
							data_fd.send_to( AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,mode(tag))) , sz ) ;
						} else {                                                                                                  // may be an empty exe
							trace("no_data_to",tn) ;
							::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , mode(tag) ) ;
						}
					break ;
				DN}
				entry.second.sig = FileSig(tn) ;                                                                                  // target digest is not stored in cache
			}
			digest.end_date = New ;                                                                                               // date must be after files are copied
			// ensure we take a single lock at a time to avoid deadlocks
			trace("done") ;
			return job_info ;
		} catch(::string const& e) {
			for( NodeIdx ti : iota(n_copied) ) unlnk(targets[ti].first) ;                                                         // clean up partial job
			trace("failed") ;
			throw e ;
		}
	}

	::pair<AcFd,DirCache::Key> DirCache::reserve( Sz sz , uint8_t z_lvl ) {
		Trace trace("DirCache::reserve",sz) ;
		//
		#if HAS_ZLIB
			static_assert(sizeof(ulong)==sizeof(Sz)) ;             // compressBound manages ulong and we need a Sz
			if (z_lvl) sz = compressBound(sz) ;
		#else
			SWEAR(!z_lvl,z_lvl) ;
		#endif
		NfsGuard nfs_guard { reliable_dirs } ;
		Key      key       = random<Key>()   ; if (!key) key = 1 ; // reserve 0 for no key
		{	LockedFd lock { dir_s , true/*exclusive*/ } ;
			_mk_room( 0 , sz , nfs_guard ) ;
		}
		AcFd         ( nfs_guard.change(cat(dir_s+AdminDirS,"reserved/",key,".sz"  )) , Fd::CreateReadOnly ).write(serialize(sz)) ;
		AcFd data_fd { nfs_guard.change(cat(dir_s+AdminDirS,"reserved/",key,".data")) , Fd::CreateReadOnly } ;
		trace(data_fd,key) ;
		return { ::move(data_fd) , key } ;
	}

	DirCache::Sz/*compressed*/ DirCache::upload( AcFd&& data_fd_ , ::vmap_s<TargetDigest> const& targets , ::vector<FileInfo> const& target_fis , uint8_t z_lvl ) {
		Trace trace("DirCache::upload",data_fd_,targets.size()) ;
		//
		NodeIdx n_targets = targets.size() ;
		DeflateFd data_fd { ::move(data_fd_) , z_lvl } ;
		//
		SWEAR( target_fis.size()==n_targets , target_fis.size() , n_targets ) ;
		::string target_szs_str ( n_targets*sizeof(Sz) , 0 ) ; for( NodeIdx ti : iota(n_targets) ) ::memcpy( &target_szs_str[ti*sizeof(Sz)] , &target_fis[ti].sz , sizeof(Sz) ) ;
		data_fd.write(target_szs_str) ;
		//
		for( NodeIdx ti : iota(n_targets) ) {
			::pair_s<TargetDigest> const& entry = targets[ti]            ;
			::string               const& tn    = entry.first            ;
			FileTag                       tag   = entry.second.sig.tag() ;
			Sz                            sz    = target_fis[ti].sz      ;
			switch (tag) {
				case FileTag::Lnk   : { trace("lnk_from"  ,tn,sz) ; ::string l = read_lnk(tn) ; SWEAR(l.size()==sz) ; data_fd.write(l) ; goto ChkSig ; }
				case FileTag::Empty :   trace("empty_from",tn   ) ;                                                                      break       ;
				case FileTag::Reg   :
				case FileTag::Exe   :
					if (sz) {
						trace("read_from",tn,sz) ;
						data_fd.send_from( {::open(tn.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME)} , sz ) ;
						goto ChkSig ;
					} else {
						trace("empty_from",tn) ;
						break ;
					}
				break ;
			DN}
			continue ;
		ChkSig :                                                                 // ensure cache entry is reliable by checking file *after* copy
			throw_unless( FileSig(tn)==target_fis[ti].sig() , "unstable ",tn ) ;
		}
		data_fd.flush() ;                                                        // update data_fd.sz
		return data_fd.total_sz ;
	}

	bool/*ok*/ DirCache::commit( Key key , ::string const& job , JobInfo&& job_info ) {
		NfsGuard nfs_guard { reliable_dirs } ;
		::string jn_s      = job+'/'+repo_s  ;
		Trace trace("DirCache::commit",job,jn_s) ;
		//
		JobDigest& digest = job_info.end.digest ;
		if (!( +job_info.start || +job_info.end )) {                                                                             // we need a full report to cache job
			trace("no_ancillary_file") ;
			dismiss(key) ;
			return false/*ok*/ ;
		}
		// check deps
		for( auto const& [dn,dd] : digest.deps ) if (!dd.is_crc) {
			trace("not_a_crc_dep",dn,dd) ;
			dismiss(key) ;
			return false/*ok*/ ;
		}
		// defensive programming : remove useless/meaningless info
		job_info.start.eta                   = {}      ;                                                                         // cache does not care
		job_info.start.submit_attrs.cache    = {}      ;                                                                         // no recursive info
		job_info.start.submit_attrs.live_out = false   ;                                                                         // cache does not care
		job_info.start.submit_attrs.reason   = {}      ;                                                                         // .
		job_info.start.pre_start.seq_id      = -1      ;                                                                         // 0 is reserved to mean no start info
		job_info.start.pre_start.job         = 0       ;                                                                         // cache does not care
		job_info.start.pre_start.port        = 0       ;                                                                         // .
		job_info.start.start.addr            = 0       ;                                                                         // cache does not care
		job_info.start.start.cache           = nullptr ;                                                                         // no recursive info
		job_info.start.start.live_out        = false   ;                                                                         // cache does not care
		job_info.start.start.small_id        = 0       ;                                                                         // no small_id since no execution
		job_info.start.start.pre_actions     = {}      ;                                                                         // pre_actions depend on execution context
		job_info.start.rsrcs.clear() ;                                                                                           // caching resources is meaningless as they have no impact on content
		for( auto& [tn,td] : digest.targets ) {
			SWEAR(!td.pre_exist) ;                                                                                               // else cannot be a candidate for upload as this must have failed
			td.sig = td.sig.tag() ;                                                                                              // forget date, just keep tag
		}
		job_info.end.seq_id    = -1 ;                                                                                            // 0 is reserved to mean no start info
		job_info.end.job       = 0  ;                                                                                            // cache does not care
		digest.end_date        = {} ;                                                                                            // .
		digest.upload_key      = 0  ;                                                                                            // no recursive info
		digest.end_attrs.cache = {} ;                                                                                            // .
		//
		// START_OF_VERSIONING
		::string job_info_str = serialize(job_info)    ;
		::string deps_str     = serialize(digest.deps) ;
		// END_OF_VERSIONING
		::string abs_jn_s = dir_s + jn_s ;
		mk_dir_s(nfs_guard.change(abs_jn_s)) ;
		AcFd dfd { nfs_guard.access_dir(abs_jn_s) , Fd::Dir } ;
		//
		// upload is the only one to take several locks and it starts with the global lock
		// this way, we are sure to avoid deadlocks
		Sz new_sz =
			job_info_str.size()
		+	deps_str.size()
		+	FileInfo(nfs_guard.access(cat(dir_s,AdminDirS,"reserved/",key,".data"))).sz
		;
		Sz       old_sz    = _reserved_sz(key,nfs_guard) ;
		bool     made_room = false                       ;
		LockedFd lock      { dir_s , true/*exclusive*/ } ;                                                                       // lock as late as possible
		try {
			old_sz += _lru_remove(jn_s,nfs_guard) ;
			unlnk_inside_s(dfd) ;
			// store meta-data and data
			_mk_room( old_sz , new_sz , nfs_guard ) ;
			made_room = true ;
			// START_OF_VERSIONING
			AcFd(dfd,"info",FdAction::CreateReadOnly).write(job_info_str) ;
			AcFd(dfd,"deps",FdAction::CreateReadOnly).write(deps_str    ) ;                                                      // store deps in a compact format so that matching is fast
			int rc = ::renameat( Fd::Cwd,nfs_guard.change(cat(dir_s,AdminDirS,"reserved/",key,".data")).c_str() , dfd,"data" ) ;
			// END_OF_VERSIONING
			if (rc<0) throw "cannot move data from tmp to final destination"s ;
			unlnk( nfs_guard.change(cat(dir_s,AdminDirS,"reserved/",key,".sz")) , false/*dir_ok*/ , true/*abs_ok*/ ) ;
			_lru_first( jn_s , new_sz , nfs_guard ) ;
		} catch (::string const& e) {
			trace("failed",e) ;
			unlnk_inside_s(dfd) ;                                                                                                // clean up in case of partial execution
			_dismiss( key , made_room?new_sz:old_sz , nfs_guard ) ;                                                              // finally, we did not populate the entry
			return false/*ok*/ ;
		}
		trace("done",new_sz) ;
		return true/*ok*/ ;
	}

	void DirCache::dismiss( Key key ) {
		Trace trace("DirCache::dismiss",key) ;
		//
		NfsGuard nfs_guard { reliable_dirs }             ;
		LockedFd lock      { dir_s , true/*exclusive*/ } ;          // lock as late as possible
		_dismiss( key , _reserved_sz(key,nfs_guard) , nfs_guard ) ;
	}

	void DirCache::_dismiss( Key key , Sz sz , NfsGuard& nfs_guard ) {
		_mk_room( sz , 0 , nfs_guard ) ; //!                                   dir_ok   abs_ok
		unlnk( nfs_guard.change(cat(dir_s,AdminDirS,"reserved/",key,".sz"  )) , false , true ) ;
		unlnk( nfs_guard.change(cat(dir_s,AdminDirS,"reserved/",key,".data")) , false , true ) ;
	}

}
