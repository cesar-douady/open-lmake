// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h>        // unshare
#include <sys/mount.h>    // mount
#include <sys/sendfile.h>

#include "disk.hh"
#include "hash.hh"
#include "trace.hh"
#include "caches/dir_cache.hh" // PER_CACHE : add include line for each cache method

#include "rpc_job.hh"

#if HAS_ZSTD
	#include <zstd.h>
#elif HAS_ZLIB
	#define ZLIB_CONST
	#include <zlib.h>
#endif

using namespace Disk ;
using namespace Hash ;

//
// FileAction
//

struct UniqKey {
	// accesses
	bool operator==(UniqKey const&) const = default ;
	// services
	size_t hash() const { return dev+ino ; }
	// data
	dev_t dev = 0 ;
	ino_t ino = 0 ;
} ;

struct UniqEntry {
	size_t          n_lnks     = 0/*garbage*/ ;
	off_t           sz         = 0/*garbage*/ ;
	mode_t          mode       = 0/*garbage*/ ;
	struct timespec mtim       = {}           ;
	bool            no_warning = true         ;
	::vector_s      files      ;
} ;

bool operator==( struct timespec const& a , struct timespec const& b ) {
	return a.tv_sec==b.tv_sec && a.tv_nsec==b.tv_nsec ;
}

::string& operator+=( ::string& os , FileAction const& fa ) {           // START_OF_NO_COV
	/**/                                os << "FileAction(" << fa.tag ;
	if (fa.tag<=FileActionTag::HasFile) os <<','<< fa.sig             ;
	return                              os <<')'                      ;
}                                                                       // END_OF_NO_COV

::string do_file_actions( ::vector_s* /*out*/ unlnks , ::vmap_s<FileAction>&& pre_actions , NfsGuard& nfs_guard ) {
	::uset_s                  keep_dirs       ;
	::string                  msg             ;
	::string                  trash           ;
	::uset_s                  existing_dirs_s ;
	::umap<UniqKey,UniqEntry> uniq_tab        ;
	//
	auto dir_exists = [&](::string const& f)->void {
		for( ::string d_s=dir_name_s(f) ; +d_s ; d_s = dir_name_s(d_s) )
			if (!existing_dirs_s.insert(d_s).second) break ;
	} ;
	//
	Trace trace("do_file_actions") ;
	if (unlnks) unlnks->reserve(unlnks->size()+pre_actions.size()) ;                                                       // most actions are unlinks
	for( auto const& [f,a] : pre_actions ) {                                                                               // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                                        // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::Unlink         :
			case FileActionTag::UnlinkWarning  :
			case FileActionTag::UnlinkPolluted :
			case FileActionTag::None           : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) { trace(a.tag,"no_file",f) ; continue ; }                                                        // file does not exist, nothing to do
				dir_exists(f) ;                                                                                            // if a file exists, its dir necessarily exists
				bool quarantine = sig!=a.sig && (a.crc==Crc::None||!a.crc.valid()||!a.crc.match(Crc(f))) ;                 // only compute crc if file has been modified
				if (quarantine) {
					if (::rename( nfs_guard.rename(f).c_str() , dir_guard(QuarantineDirS+f).c_str() )<0) throw "cannot quarantine "+f ;
					msg <<"quarantined " << mk_file(f) <<'\n' ;
				} else {
					SWEAR(is_lcl(f)) ;
					if (!unlnk(nfs_guard.change(f))) throw "cannot unlink "+f ;
					if ( a.tag==FileActionTag::None && !a.no_warning ) msg <<"unlinked " << mk_file(f) <<'\n' ;            // if a file has been unlinked, its dir necessarily exists
				}
				trace(a.tag,STR(quarantine),f) ;
				if (unlnks) unlnks->push_back(f) ;
			} break ;
			case FileActionTag::Uniquify : {
				struct stat s ;
				if (   ::stat(f.c_str(),&s)<0                 ) { trace(a.tag,"no_file"  ,f) ; continue ; }                // file does not exist, nothing to do
				dir_exists(f) ;                                                                                            // if file exists, certainly its dir exists as well
				if (   s.st_nlink==1                          ) { trace(a.tag,"single"   ,f) ; continue ; }                // file is already unique, nothing to do
				if (!( s.st_mode & (S_IWUSR|S_IWGRP|S_IWOTH) )) { trace(a.tag,"read-only",f) ; continue ; }                // if file is read-only, assume it is immutable
				if (!  S_ISREG(s.st_mode)                     ) { trace(a.tag,"awkward"  ,f) ; continue ; }                // do not handle awkward files and symlinks are immutable
				UniqEntry& e = uniq_tab[{s.st_dev,s.st_ino}] ;                                                             // accumulate all links per file identified by dev/inode
				if (!e.files) {      e.n_lnks= s.st_nlink ;  e.sz= s.st_size ;  e.mode= s.st_mode ;  e.mtim= s.st_mtim ; }
				else          SWEAR( e.n_lnks==s.st_nlink && e.sz==s.st_size && e.mode==s.st_mode && e.mtim==s.st_mtim ) ; // check consistency
				e.files.push_back(f) ;
				e.no_warning &= a.no_warning ;
			} break ;
			case FileActionTag::Mkdir : {
				::string f_s = with_slash(f) ;
				if (!existing_dirs_s.contains(f_s)) mk_dir_s(f_s,nfs_guard) ;
			} break ;
			case FileActionTag::Rmdir :
				if (!keep_dirs.contains(f))
					try {
						rmdir_s(with_slash(nfs_guard.change(f))) ;
					} catch (::string const&) {                                                                            // if a dir cannot rmdir'ed, no need to try those uphill
						keep_dirs.insert(f) ;
						for( ::string d_s=dir_name_s(f) ; +d_s ; d_s=dir_name_s(d_s) )
							if (!keep_dirs.insert(no_slash(d_s)).second) break ;
					}
			break ;
		DF}                                                                                                                // NO_COV
	}
	for( auto const& [_,e] : uniq_tab ) {
		SWEAR(e.files.size()<=e.n_lnks,e.n_lnks,e.files) ;                                                                 // check consistency
		if (e.n_lnks==e.files.size()) { trace("all_lnks",e.files) ; continue ; }                                           // we have all the links, nothing to do
		trace("uniquify",e.n_lnks,e.files) ;
		//
		const char* err = nullptr/*garbage*/ ;
		{	const char* f0   = e.files[0].c_str()                           ;
			AcFd        rfd  = ::open    ( f0 , O_RDONLY|O_NOFOLLOW       ) ; if (!rfd  ) { err = "cannot open for reading" ; goto Bad ; }
			int         urc  = ::unlink  ( f0                             ) ; if (urc <0) { err = "cannot unlink"           ; goto Bad ; }
			AcFd        wfd  = ::open    ( f0 , O_WRONLY|O_CREAT , e.mode ) ; if (!wfd  ) { err = "cannot open for writing" ; goto Bad ; }
			int         sfrc = ::sendfile( wfd , rfd , nullptr , e.sz     ) ; if (sfrc<0) { err = "cannot copy"             ; goto Bad ; }
			for( size_t i : iota(1,e.files.size()) ) {
				const char* f = e.files[i].c_str() ;
				int urc = ::unlink(      f ) ; if (urc!=0) { err = "cannot unlink" ; goto Bad ; }
				int lrc = ::link  ( f0 , f ) ; if (lrc!=0) { err = "cannot link"   ; goto Bad ; }
			}
			struct ::timespec times[2] = { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , e.mtim } ;
			::futimens(wfd,times) ;                                                                                        // maintain original date
			if (!e.no_warning) {
				/**/                               msg <<"uniquified"  ;
				if (e.files.size()>1)              msg <<" as a group" ;
				/**/                               msg <<" :"          ;
				for( ::string const& f : e.files ) msg <<' '<< f       ;
				/**/                               msg <<'\n'          ;
			}
		}
		continue ;
	Bad :                                                                                                                  // NO_COV defensive programming
		throw cat(err," while uniquifying ",e.files) ;                                                                     // NO_COV .
	}
	trace("done",localize(msg)) ;
	return msg ;
}

//
// JobReason
//

::string& operator+=( ::string& os , JobReason const& jr ) { // START_OF_NO_COV
	os << "JobReason(" << jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os << ',' << jr.node ;
	return os << ')' ;
}                                                            // END_OF_NO_COV

//
// MsgStderr
//

::string& operator+=( ::string& os , MsgStderr const& ms ) { // START_OF_NO_COV
	return os <<'('<< ms.msg <<','<< ms.stderr <<')' ;
}                                                            // END_OF_NO_COV

//
// DepInfo
//

::string& operator+=( ::string& os , DepInfo const& di ) {           // START_OF_NO_COV
	switch (di.kind()) {
		case DepInfoKind::Crc  : return os <<'('<< di.crc () <<')' ;
		case DepInfoKind::Sig  : return os <<'('<< di.sig () <<')' ;
		case DepInfoKind::Info : return os <<'('<< di.info() <<')' ;
	DF}                                                              // NO_COV
}                                                                    // END_OF_NO_COV

//
// Cache
//

namespace Caches {

	struct DeflateFd : AcFd {
		static Cache::Sz s_max_sz( Cache::Sz sz , uint8_t lvl=0 ) {
			#if HAS_ZSTD
				static_assert(sizeof(size_t)==sizeof(Cache::Sz)) ; // ZSTD_compressBound manages size_t and we need a Sz
				if (lvl) return ::ZSTD_compressBound(sz) ;
			#elif HAS_ZLIB
				static_assert(sizeof(ulong)==sizeof(Cache::Sz)) ;  // compressBound manages ulong and we need a Sz
				if (lvl) return ::compressBound(sz) ;
			#else
				SWEAR(!lvl,lvl) ;
			#endif
			return sz ;
		}
		// cxtors & casts
		DeflateFd() = default ;
		#if HAS_ZSTD
			DeflateFd( AcFd&& fd , uint8_t lvl_=0 ) : AcFd{::move(fd)} , lvl{::min(lvl_,uint8_t(::ZSTD_maxCLevel()))} {
				if (lvl) {
					_zs = ::ZSTD_createCCtx() ; SWEAR(_zs) ;
					::ZSTD_CCtx_setParameter( _zs , ZSTD_c_compressionLevel , lvl );
				}
			}
			~DeflateFd() {
				flush() ;
				if (lvl) ::ZSTD_freeCCtx(_zs) ;
			}
		#elif HAS_ZLIB
			DeflateFd( AcFd&& fd , uint8_t lvl_=0 ) : AcFd{::move(fd)} , lvl{::min(lvl_,uint8_t(Z_BEST_COMPRESSION))} {
				if (lvl) {
					int rc = deflateInit(&_zs,lvl) ; SWEAR(rc==Z_OK) ;
					_zs.next_in  = ::launder(reinterpret_cast<uint8_t const*>(_buf)) ;
					_zs.avail_in = 0                                                 ;
				}
			}
			~DeflateFd() {
				flush() ;
				deflateEnd(&_zs) ;
			}
		#else
			DeflateFd( AcFd&& fd , uint8_t lvl=0 ) : AcFd{::move(fd)} { SWEAR(!lvl,lvl) ; }
			~DeflateFd() { flush() ; }
		#endif
		// services
		void write(::string const& s) {
			if (!s) return ;
			SWEAR(!_flushed) ;
			#if HAS_ZSTD
				if (lvl) {
					::ZSTD_inBuffer  in_buf  { .src=s.data() , .size=s.size()  , .pos=0 } ;
					::ZSTD_outBuffer out_buf { .dst=_buf     , .size=DiskBufSz , .pos=0 } ;
					while (in_buf.pos<in_buf.size) {
						out_buf.pos = _pos ;
						::ZSTD_compressStream2( _zs , &out_buf , &in_buf , ZSTD_e_continue ) ;
						_pos = out_buf.pos ;
						_flush(1/*room*/) ;
					}
					return ;
				}
			#elif HAS_ZLIB
				if (lvl) {
					_zs.next_in  = ::launder(reinterpret_cast<uint8_t const*>(s.data())) ;
					_zs.avail_in = s.size()                                              ;
					while (_zs.avail_in) {
						_zs.next_out  = ::launder(reinterpret_cast<uint8_t*>( _buf + _pos )) ;
						_zs.avail_out = DiskBufSz - _pos                                     ;
						deflate(&_zs,Z_NO_FLUSH) ;
						_pos = DiskBufSz - _zs.avail_out ;
						_flush(1/*room*/) ;
					}
					return ;
				}
			#endif
			// no compression
			if (_flush(s.size())) {                ::memcpy( _buf+_pos , s.data() , s.size() ) ; _pos     += s.size() ; }                                            // small data : put in _buf
			else                  { SWEAR(!_pos) ; AcFd::write(s)                              ; total_sz += s.size() ; }                                            // large data : send directly
		}
		void send_from( Fd fd_ , size_t sz ) {
			if (!sz) return ;
			#if HAS_ZSTD || HAS_ZLIB
				SWEAR(!_flushed) ;
				if (lvl) {
					while (sz) {
						size_t cnt = ::min(sz,DiskBufSz) ;
						::string s = fd_.read(cnt) ; throw_unless(s.size()==cnt,"missing ",cnt-s.size()," bytes from ",fd) ;
						write(s) ;
						sz -= cnt ;
					}
					return ;
				}
			#endif
			_flush(sz) ;
			if (_flush(sz)) {                size_t c = fd_.read_to({_buf+_pos,sz})     ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; _pos     += c  ; } // small data : put in _buf
			else            { SWEAR(!_pos) ; size_t c = ::sendfile(self,fd_,nullptr,sz) ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; total_sz += sz ; } // large data : send directly
		}
		void flush() {
			if (_flushed) return ;
			_flushed = true ;
			#if HAS_ZSTD
				if (lvl) {
					::ZSTD_inBuffer  in_buf  { .src=nullptr , .size=0         , .pos=0 } ;
					::ZSTD_outBuffer out_buf { .dst=_buf    , .size=DiskBufSz , .pos=0 } ;
					for (;;) {
						out_buf.pos = _pos ;
						size_t rc = ::ZSTD_compressStream2( _zs , &out_buf , &in_buf , ZSTD_e_end ) ;
						_pos = out_buf.pos ;
						if (::ZSTD_isError(rc)) throw cat("cannot flush ",self) ;
						_flush() ;
						if (!rc) return ;
					}
				}
			#elif HAS_ZLIB
				if (lvl) {
					_zs.next_in  = nullptr ;
					_zs.avail_in = 0       ;
					for (;;) {
						_zs.next_out  = ::launder(reinterpret_cast<uint8_t*>( _buf + _pos )) ;
						_zs.avail_out = DiskBufSz - _pos                                     ;
						int rc = deflate(&_zs,Z_FINISH) ;
						_pos = DiskBufSz - _zs.avail_out ;
						if (rc==Z_BUF_ERROR) throw cat("cannot flush ",self) ;
						_flush() ;
						if (rc==Z_STREAM_END) return ;
					}
				}
			#endif
			_flush() ;
		}
	private :
		bool/*room_ok*/ _flush(size_t room=DiskBufSz) {                                                                                                              // flush if not enough room
			if (_pos+room<=DiskBufSz) return true/*room_ok*/ ;                                                                                                       // enough room
			if (_pos) {
				AcFd::write({_buf,_pos}) ;
				total_sz += _pos ;
				_pos      = 0    ;
			}
			return room<=DiskBufSz ;
		}
		// data
	public :
		Disk::DiskSz total_sz = 0 ;
		#if HAS_ZLIB
			uint8_t lvl = 0 ;
		#endif
	private :
		char   _buf[DiskBufSz] ;
		size_t _pos            = 0     ;
		bool   _flushed        = false ;
		#if HAS_ZSTD
			::ZSTD_CCtx* _zs = nullptr ;
		#elif HAS_ZLIB
			z_stream     _zs = {}      ;
		#endif
	} ;

	struct InflateFd : AcFd {
		// cxtors & casts
		InflateFd() = default ;
		#if HAS_ZSTD
			InflateFd( AcFd&& fd , bool lvl_=false ) : AcFd{::move(fd)} , lvl{lvl_} {
				if (lvl) { _zs = ::ZSTD_createDCtx() ; SWEAR(_zs,self) ; }
			}
			~InflateFd() {
				if (lvl) { size_t rc = ::ZSTD_freeDCtx(_zs) ; SWEAR(!::ZSTD_isError(rc),rc,self) ; }
			}
		#elif HAS_ZLIB
			InflateFd( AcFd&& fd , bool lvl_=false ) : AcFd{::move(fd)} , lvl{lvl_} {
				if (lvl) { int rc = inflateInit(&_zs) ; SWEAR(rc==Z_OK,self) ; }
			}
			~InflateFd() {
				if (lvl) { int rc = inflateEnd(&_zs) ; SWEAR(rc==Z_OK,rc,self) ; }
			}
		#else
			InflateFd( AcFd&& fd , bool lvl=false ) : AcFd{::move(fd)} {
				SWEAR(!lvl,lvl) ;
			}
			~InflateFd() {}
		#endif
		// services
		::string read(size_t sz) {
			if (!sz) return {} ;
			::string res ( sz , 0 ) ;
			#if HAS_ZSTD
				if (lvl) {
					::ZSTD_inBuffer  in_buf  { .src=_buf       , .size=0  , .pos=0 } ;
					::ZSTD_outBuffer out_buf { .dst=res.data() , .size=sz , .pos=0 } ;
					while (out_buf.pos<sz) {
						if (!_len) {
							_len = AcFd::read_to({_buf,DiskBufSz}) ; throw_unless(_len>0,"missing ",sz-out_buf.pos," bytes from ",self) ;
							_pos = 0                               ;
						}
						in_buf.pos  = _pos      ;
						in_buf.size = _pos+_len ;
						size_t rc = ::ZSTD_decompressStream( _zs , &out_buf , &in_buf ) ; SWEAR(!::ZSTD_isError(rc)) ;
						_pos = in_buf.pos               ;
						_len = in_buf.size - in_buf.pos ;
					}
					return res ;
				}
			#elif HAS_ZLIB
				if (lvl) {
					_zs.next_out  = ::launder(reinterpret_cast<uint8_t*>(res.data())) ;
					_zs.avail_out = res.size()                                        ;
					while (_zs.avail_out) {
						if (!_len) {
							_len = AcFd::read_to({_buf,DiskBufSz}) ; throw_unless(_len>0,"missing ",_zs.avail_out," bytes from ",self) ;
							_pos = 0                               ;
						}
						_zs.next_in  = ::launder(reinterpret_cast<uint8_t const*>( _buf + _pos )) ;
						_zs.avail_in = _len                                                       ;
						inflate(&_zs,Z_NO_FLUSH) ;
						_pos = ::launder(reinterpret_cast<char const*>(_zs.next_in)) - _buf ;
						_len = _zs.avail_in                                                 ;
					}
					return res ;
				}
			#endif
			size_t cnt = ::min( sz , _len ) ;
			if (cnt) {                                                                                                           // gather available data from _buf
				::memcpy( res.data() , _buf+_pos , cnt ) ;
				_pos += cnt ;
				_len -= cnt ;
				sz   -= cnt ;
			}
			if (sz) {
				SWEAR(!_len,_len) ;
				if (sz>=DiskBufSz) {                                                                                             // large data : read directly
					size_t c = AcFd::read_to({&res[cnt],sz}) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
				} else {                                                                                                         // small data : bufferize
					_len = AcFd::read_to({_buf,DiskBufSz}) ;   throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
					::memcpy( &res[cnt] , _buf , sz ) ;
					_pos  = sz ;
					_len -= sz ;
				}
			}
			return res ;
		}
		void receive_to( Fd fd_ , size_t sz ) {
			#if HAS_ZSSTD || HAS_ZLIB
				if (lvl) {
					while (sz) {
						size_t   cnt = ::min(sz,DiskBufSz)                           ;
						::string s   = read(cnt) ; SWEAR(s.size()==cnt,s.size(),cnt) ;
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
				if (sz>=DiskBufSz) {                                                                                             // large data : transfer directly fd to fd
					size_t c = ::sendfile(fd_,self,nullptr,sz) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
				} else {                                                                                                         // small data : bufferize
					_len = AcFd::read_to({_buf,DiskBufSz}) ;     throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
					fd_.write({_buf,sz}) ;
					_pos  = sz ;
					_len -= sz ;
				}
			}
		}
		// data
		#if HAS_STD || HAS_ZLIB
			bool lvl = false ;
		#endif
	private :
		char   _buf[DiskBufSz] ;
		size_t _pos            = 0 ;
		size_t _len            = 0 ;
		#if HAS_ZSTD
			::ZSTD_DCtx* _zs = nullptr ;
		#elif HAS_ZLIB
			z_stream     _zs = {}      ;
		#endif
	} ;

	::vector<Cache*> Cache::s_tab ;

	Cache* Cache::s_new(Tag tag) {
		switch (tag) {
			case Tag::None : return nullptr      ; // base class Cache actually caches nothing
			case Tag::Dir  : return new DirCache ; // PER_CACHE : add a case for each cache method
		DF}                                        // NO_COV
	}

	void Cache::s_config( CacheIdx idx , Tag tag , ::vmap_ss const& dct ) {
		Cache* cache = s_new(tag) ;
		cache->config(dct) ;
		Cache*& c = grow(s_tab,idx) ;
		SWEAR(!c) ;
		c = cache ;
	}

	JobInfo Cache::download( ::string const& match_key , NfsGuard& repo_nfs_guard ) {
		Trace trace("Cache::download",match_key) ;
		//
		::pair<JobInfo,AcFd>    info_fd  = sub_download(match_key)     ;
		JobInfo               & job_info = info_fd.first               ;
		::vmap_s<TargetDigest>& targets  = job_info.end.digest.targets ;
		NodeIdx                 n_copied = 0                           ;
		try {
			#if !HAS_ZLIB
				throw_if( job_info.start.start.z_lvl , "cannot uncompress without zlib" ) ;
			#endif
			//
			NodeIdx   n_targets = targets.size()                                              ;
			auto      mode      = [](FileTag t)->int { return t==FileTag::Exe?0777:0666 ; }   ;
			InflateFd data_fd   { ::move(info_fd.second) , bool(job_info.start.start.z_lvl) } ;
			//
			::string     target_szs_str = data_fd.read(n_targets*sizeof(Sz)) ;
			::vector<Sz> target_szs     ( n_targets )                        ; for( NodeIdx ti : iota(n_targets) ) ::memcpy( &target_szs[ti] , &target_szs_str[ti*sizeof(Sz)] , sizeof(Sz) ) ;
			for( NodeIdx ti : iota(n_targets) ) {
				auto&           entry = targets[ti]            ;
				::string const& tn    = entry.first            ;
				FileTag         tag   = entry.second.sig.tag() ;
				Sz              sz    = target_szs[ti]         ;
				n_copied = ti+1 ;
				repo_nfs_guard.change(tn) ;
				unlnk(dir_guard(tn),true/*dir_ok*/) ;
				switch (tag) {
					case FileTag::Lnk   : trace("lnk_to"  ,tn,sz) ; lnk( tn , data_fd.read(target_szs[ti]) )                                       ; break ;
					case FileTag::Empty : trace("empty_to",tn   ) ; AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , mode(tag) )) ; break ;
					case FileTag::Exe   :
					case FileTag::Reg   :
						if (sz) { trace("write_to"  ,tn,sz) ; data_fd.receive_to( AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,mode(tag) )) , sz ) ; }
						else    { trace("no_data_to",tn   ) ;                     AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,mode(tag) ))        ; } // may be an empty exe
					break ;
				DN}
				entry.second.sig = FileSig(tn) ;                          // target digest is not stored in cache
			}
			job_info.end.end_date = New ;                                 // date must be after files are copied
			// ensure we take a single lock at a time to avoid deadlocks
			trace("done") ;
			return job_info ;
		} catch(::string const& e) {
			trace("failed",e,n_copied,targets) ;
			for( NodeIdx ti : iota(n_copied) ) unlnk(targets[ti].first) ; // clean up partial job
			throw e ;
		}
	}

	uint64_t/*upload_key*/ Cache::upload( ::vmap_s<TargetDigest> const& targets , ::vector<FileInfo> const& target_fis , uint8_t z_lvl ) {
		Trace trace("DirCache::upload",targets.size(),z_lvl) ;
		//
		Sz                                  tgts_sz  = 0                                  ; { for( FileInfo fi : target_fis ) tgts_sz += fi.sz ;}
		Sz                                  z_max_sz = DeflateFd::s_max_sz(tgts_sz,z_lvl) ;
		::pair<uint64_t/*upload_key*/,AcFd> key_fd   = sub_upload(z_max_sz)               ;
		//
		try {
			NodeIdx n_targets = targets.size()                  ;
			DeflateFd data_fd { ::move(key_fd.second) , z_lvl } ;
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
		} catch (::string const&) {
			dismiss(key_fd.first) ;
			trace("failed") ;
			return {} ;
		}
		trace("done",tgts_sz,z_max_sz) ;
		return key_fd.first ;
	}

	bool/*ok*/ Cache::commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		Trace trace("Cache::commit",upload_key,job) ;
		//
		if (!( +job_info.start && +job_info.end )) { // we need a full report to cache job
			trace("no_ancillary_file") ;
			dismiss(upload_key) ;
			return false/*ok*/ ;
		}
		//
		job_info.update_digest() ;                   // ensure cache has latest crc available
		// check deps
		for( auto const& [dn,dd] : job_info.end.digest.deps ) if (!dd.is_crc) {
			trace("not_a_crc_dep",dn,dd) ;
			dismiss(upload_key) ;
			return false/*ok*/ ;
		}
		// defensive programming : remove useless/meaningless info
		job_info.cache_cleanup() ;
		//
		return sub_commit( upload_key , job , ::move(job_info) ) ;
	}

}

//
// JobSpace
//

::string& operator+=( ::string& os , JobSpace::ViewDescr const& vd ) { // START_OF_NO_COV
	/**/             os <<"ViewDescr("<< vd.phys ;
	if (+vd.copy_up) os <<"CU:"<< vd.copy_up     ;
	return           os <<')'                    ;
}                                                                      // END_OF_NO_COV

::string& operator+=( ::string& os , JobSpace const& js ) {            // START_OF_NO_COV
	First first ;
	/**/                  os <<"JobSpace("                           ;
	if (+js.chroot_dir_s) os <<first("",",")<<"C:"<< js.chroot_dir_s ;
	if (+js.lmake_view_s) os <<first("",",")<<"R:"<< js.lmake_view_s ;
	if (+js.repo_view_s ) os <<first("",",")<<"R:"<< js.repo_view_s  ;
	if (+js.tmp_view_s  ) os <<first("",",")<<"T:"<< js.tmp_view_s   ;
	if (+js.views       ) os <<first("",",")<<"V:"<< js.views        ;
	return                os <<')'                                   ;
}                                                                      // END_OF_NO_COV

	static void _chroot(::string const& dir_s) { Trace trace("_chroot",dir_s) ; if (::chroot(no_slash(dir_s).c_str())!=0) throw "cannot chroot to "+no_slash(dir_s)+" : "+::strerror(errno) ; }
	static void _chdir (::string const& dir_s) { Trace trace("_chdir" ,dir_s) ; if (::chdir (no_slash(dir_s).c_str())!=0) throw "cannot chdir to " +no_slash(dir_s)+" : "+::strerror(errno) ; }

	static void _mount_bind( ::string const& dst , ::string const& src ) {                                                    // src and dst may be files or dirs
		Trace trace("_mount_bind",dst,src) ;
		if (::mount( no_slash(src).c_str() , no_slash(dst).c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0)
			throw "cannot bind mount "+src+" onto "+dst+" : "+::strerror(errno) ;                                             // NO_COV defensive programming
	}

static void _mount_overlay( ::string const& dst_s , ::vector_s const& srcs_s , ::string const& work_s ) {
	SWEAR(+srcs_s) ;
	SWEAR(srcs_s.size()>1,dst_s,srcs_s,work_s) ;                                                 // use bind mount in that case
	//
	Trace trace("_mount_overlay",dst_s,srcs_s,work_s) ;
	for( size_t i : iota(1,srcs_s.size()) )
		if (srcs_s[i].find(':')!=Npos)
			throw cat("cannot overlay mount ",dst_s," to ",srcs_s,"with embedded columns (:)") ; // NO_COV defensive programming
	mk_dir_s(work_s) ;
	//
	::string                                data  = "userxattr"                      ;
	/**/                                    data += ",upperdir="+no_slash(srcs_s[0]) ;
	/**/                                    data += ",lowerdir="+no_slash(srcs_s[1]) ;
	for( size_t i : iota(2,srcs_s.size()) ) data += ':'         +no_slash(srcs_s[i]) ;
	/**/                                    data += ",workdir=" +no_slash(work_s   ) ;
	if (::mount( nullptr ,  no_slash(dst_s).c_str() , "overlay" , 0 , data.c_str() )!=0)
		throw "cannot overlay mount "+dst_s+" to "+data+" : "+::strerror(errno) ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AcFd fd { file , Fd::Write } ;
	if (!fd) throw "cannot open "+file+" for writing : "+::strerror(errno) ;
	ssize_t cnt = ::write( fd , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw cat("cannot write atomically ",data.size()," bytes to ",file," : ",::strerror(errno)        ) ;
	if (size_t(cnt)<data.size()) throw cat("cannot write atomically ",data.size()," bytes to ",file," : only ",cnt," bytes written") ;
}

bool JobSpace::_is_lcl_tmp(::string const& f) const {
	if (is_lcl(f)  ) return true                      ;
	if (+tmp_view_s) return f.starts_with(tmp_view_s) ;
	/**/             return false                     ;
} ;

bool/*dst_ok*/ JobSpace::_create( ::vmap_s<MountAction>& deps , ::string const& dst , ::string const& src ) const {
	if (!_is_lcl_tmp(dst)) return false/*dst_ok*/ ;
	bool dst_ok = true ;
	if (is_dir_name(dst)) {
		mk_dir_s(dst) ;
		deps.emplace_back(no_slash(dst),MountAction::Access) ;
	} else if (+FileInfo(dst).tag()) {
		deps.emplace_back(dst,MountAction::Access) ;
	} else if (+src) {
		/**/                        deps.emplace_back(src,MountAction::Read ) ;
		if ((dst_ok=+cpy(dst,src))) deps.emplace_back(dst,MountAction::Write) ;
		else                        dst_ok = false ;
	} else {
		AcFd fd { dir_guard(dst) , Fd::Write } ;
		if ((dst_ok=+fd)) deps.emplace_back(dst,MountAction::Write) ;
	}
	return dst_ok ;
}

template<bool IsFile,class... A> static bool/*match*/ _handle( ::string& v/*inout*/ , size_t& d /*inout*/, const char* key , A const&... args ) {
	size_t len   = ::strlen(key)    ;
	bool   brace = v[d+1/*$*/]=='{' ;
	size_t start = d+1/*$*/+brace   ;
	if ( ::string_view(&v[start],len)!=key    ) return false/*match*/ ;
	if (  brace && v[start+len]!='}'          ) return false/*match*/ ;
	if ( !brace && is_word_char(v[start+len]) ) return false/*match*/ ;
	::string pfx =
		IsFile ? v.substr(0,d) + no_slash(cat(args...))
		:        v.substr(0,d) +          cat(args...)
	;
	d = pfx.size()                                                    ;
	v = cat( ::move(pfx) , ::string_view(v).substr(start+len+brace) ) ;
	return true/*match*/ ;
}
void JobSpace::update_env(
	::map_ss        &/*inout*/ env
,	::string   const&          phy_lmake_root_s
,	::string   const&          phy_repo_root_s
,	::string   const&          phy_tmp_dir_s
,	::string   const&          sub_repo_s
,	SeqId                      seq_id
,	SmallId                    small_id
) const {
	bool has_tmp_dir = +phy_tmp_dir_s ;
	::string const& lmake_root_s = lmake_view_s | phy_lmake_root_s ;
	::string const& repo_root_s  = repo_view_s  | phy_repo_root_s  ;
	::string const& tmp_dir_s    = tmp_view_s   | phy_tmp_dir_s    ;
	//
	if (has_tmp_dir) env["TMPDIR"] = no_slash(tmp_dir_s) ;
	else             env.erase("TMPDIR") ;
	if (PY_LD_LIBRARY_PATH[0]!=0) {
		auto [it,inserted] = env.try_emplace("LD_LIBRARY_PATH",PY_LD_LIBRARY_PATH) ;
		if (!inserted) it->second <<':'<< PY_LD_LIBRARY_PATH ;
	}
	for( auto& [k,v] : env ) {
		for( size_t d=0 ;; d++ ) {
			d = v.find('$',d) ;
			if (d==Npos) break ;
			switch (v[d+1/*$*/]) {
				//                                 IsFile inout inout
				case 'L' :                  _handle<true >( v  , d  , "LMAKE_ROOT"             , lmake_root_s               )   ; break ;
				case 'P' :                  _handle<true >( v  , d  , "PHYSICAL_LMAKE_ROOT"    , phy_lmake_root_s           )
				||                          _handle<true >( v  , d  , "PHYSICAL_REPO_ROOT"     , phy_repo_root_s ,sub_repo_s)
				||         ( has_tmp_dir && _handle<true >( v  , d  , "PHYSICAL_TMPDIR"        , phy_tmp_dir_s              ) )
				||                          _handle<true >( v  , d  , "PHYSICAL_TOP_REPO_ROOT" , phy_repo_root_s            )   ; break ;
				case 'R' :                  _handle<true >( v  , d  , "REPO_ROOT"              , repo_root_s     ,sub_repo_s)   ; break ;
				case 'S' :                  _handle<false>( v  , d  , "SEQUENCE_ID"            , seq_id                     )
				||                          _handle<false>( v  , d  , "SMALL_ID"               , small_id                   )   ; break ;
				case 'T' : ( has_tmp_dir && _handle<true >( v  , d  , "TMPDIR"                 , tmp_dir_s                  ) )
				||                          _handle<true >( v  , d  , "TOP_REPO_ROOT"          , repo_root_s                )   ; break ;
			DN}
		}
	}
}

bool JobSpace::enter(
	::vmap_s<MountAction>&/*out*/ report
,	::string             &/*out*/ top_repo_root_s
,	::string   const&             phy_lmake_root_s
,	::string   const&             phy_repo_root_s
,	::string   const&             phy_tmp_dir_s
,	::string   const&             cwd_s
,	::string   const&             work_dir_s
,	::vector_s const&             src_dirs_s
) {
	Trace trace("JobSpace::enter",self,phy_repo_root_s,phy_tmp_dir_s,cwd_s,work_dir_s,src_dirs_s) ;
	//
	if (!self) {
		top_repo_root_s = phy_repo_root_s ;
		trace("not_done",top_repo_root_s) ;
		return false/*entered*/ ;
	}
	//
	int uid = ::getuid() ;                                                          // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                          // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw "cannot create namespace : "s+::strerror(errno) ;
	//
	size_t   uphill_lvl = 0 ;
	::string highest_s  ;
	for( ::string const& d_s : src_dirs_s ) if (!is_abs_s(d_s))
		if ( size_t ul=uphill_lvl_s(d_s) ; ul>uphill_lvl ) {
			uphill_lvl = ul  ;
			highest_s  = d_s ;
		}
	//
	::string phy_super_repo_root_s ;                                                // dir englobing all relative source dirs
	::string super_repo_view_s     ;                                                // .
	::string top_repo_view_s       ;
	if (+repo_view_s) {
		if (!( repo_view_s.ends_with(cwd_s) && repo_view_s.size()>cwd_s.size()+1 )) // ensure repo_view_s has at least one more level than cwd_s
			throw
				"cannot map local repository dir to "+no_slash(repo_view_s)+" appearing as "+no_slash(cwd_s)+" in top-level repository, "
				"consider setting <rule>.repo_view="+mk_py_str("/repo/"+no_slash(cwd_s))
			;
		phy_super_repo_root_s = phy_repo_root_s ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) phy_super_repo_root_s = dir_name_s(phy_super_repo_root_s) ;
		super_repo_view_s     = repo_view_s     ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) super_repo_view_s     = dir_name_s(super_repo_view_s    ) ;
		SWEAR(phy_super_repo_root_s!="/",phy_repo_root_s,uphill_lvl) ;              // this should have been checked earlier
		if (!super_repo_view_s)
			throw
				"cannot map repository dir to "+no_slash(repo_view_s)+" with relative source dir "+no_slash(highest_s)
			+	", "
			+	"consider setting <rule>.repo_view="+mk_py_str("/repo/"+no_slash(phy_repo_root_s.substr(phy_super_repo_root_s.size())+cwd_s))
			;
		if (substr_view(repo_view_s,super_repo_view_s.size())!=substr_view(phy_repo_root_s,phy_super_repo_root_s.size()))
			throw cat(
				"last ",uphill_lvl," components do not match between physical root dir and root view"
			,	", "
			,	"consider setting <rule>.repo_view=",mk_py_str(cat("/repo/",no_slash(phy_repo_root_s.substr(phy_super_repo_root_s.size())+cwd_s)))
			) ;
		top_repo_view_s = repo_view_s.substr(0,repo_view_s.size()-cwd_s.size()) ;
	}
	// XXX! : handle cases where dir is not top level
	if ( +lmake_view_s      && lmake_view_s     .rfind('/',lmake_view_s     .size()-2)!=0 ) throw "non top-level lmake_view "+no_slash(lmake_view_s     )+" not yet implemented" ;
	if ( +super_repo_view_s && super_repo_view_s.rfind('/',super_repo_view_s.size()-2)!=0 ) throw "non top-level repo_view " +no_slash(super_repo_view_s)+" not yet implemented" ;
	if ( +tmp_view_s        && tmp_view_s       .rfind('/',tmp_view_s       .size()-2)!=0 ) throw "non top-level tmp_view "  +no_slash(tmp_view_s       )+" not yet implemented" ;
	//
	::string chroot_dir        = chroot_dir_s                                                          ; if (+chroot_dir) chroot_dir.pop_back() ; // cannot use no_slash to properly manage the '/' case
	bool     must_create_lmake = +lmake_view_s      && !is_dir(chroot_dir+no_slash(lmake_view_s     )) ;
	bool     must_create_repo  = +super_repo_view_s && !is_dir(chroot_dir+no_slash(super_repo_view_s)) ;
	bool     must_create_tmp   = +tmp_view_s        && !is_dir(chroot_dir+no_slash(tmp_view_s       )) ;
	//
	if (must_create_tmp) SWEAR(+phy_tmp_dir_s) ;
	trace("create",STR(must_create_lmake),STR(must_create_repo),STR(must_create_tmp)) ;
	//
	if ( must_create_repo || must_create_tmp || +views )
		try { unlnk_inside_s(work_dir_s) ; } catch (::string const& e) {} // if we need a work dir, we must clean it first as it is not cleaned upon exit (ignore errors as dir may not exist)
	if ( must_create_lmake || must_create_repo || must_create_tmp ) {     // we cannot mount directly in chroot_dir
		if (!work_dir_s)
			throw                                                         // START_OF_NO_COV defensive programming
				"need a work dir to"s
			+	(	must_create_lmake ? " create lmake view"
				:	must_create_repo  ? " create repo view"
				:	must_create_tmp   ? " create tmp view"
				:	                    " ???"
				)
			;                                                             // END_OF_NO_COV
		::vector_s top_lvls    = lst_dir_s(chroot_dir_s|"/") ;
		::string   work_root   = work_dir_s+"root"           ;
		::string   work_root_s = work_root+'/'               ;
		mk_dir_s      (work_root_s) ;
		unlnk_inside_s(work_root_s) ;
		trace("top_lvls",work_root_s,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = (chroot_dir_s|"/"s) + f ;
			::string private_f = work_root_s         + f ;
			switch (FileInfo(src_f).tag()) {                                                                                   // exclude weird files
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : AcFd    (        private_f    ,Fd::Write      ) ; _mount_bind(private_f,src_f) ; break ; // create file
				case FileTag::Dir   : mk_dir_s(with_slash(private_f)                ) ; _mount_bind(private_f,src_f) ; break ; // create dir
				case FileTag::Lnk   : lnk     (           private_f ,read_lnk(src_f)) ;                                break ; // copy symlink
			DN}
		}
		if (must_create_lmake) mk_dir_s(work_root+lmake_view_s     ) ;
		if (must_create_repo ) mk_dir_s(work_root+super_repo_view_s) ;
		if (must_create_tmp  ) mk_dir_s(work_root+tmp_view_s       ) ;
		chroot_dir = ::move(work_root) ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                  ) ;                                                        // necessary to be allowed to write the gid_map (if desirable)
	_atomic_write( "/proc/self/uid_map"   , cat(uid,' ',uid," 1\n") ) ;
	_atomic_write( "/proc/self/gid_map"   , cat(gid,' ',gid," 1\n") ) ;
	//
	top_repo_root_s = top_repo_view_s | phy_repo_root_s ;
	if (+lmake_view_s) _mount_bind( chroot_dir+lmake_view_s      , phy_lmake_root_s      ) ;
	if (+repo_view_s ) _mount_bind( chroot_dir+super_repo_view_s , phy_super_repo_root_s ) ;
	if (+tmp_view_s  ) _mount_bind( chroot_dir+tmp_view_s        , phy_tmp_dir_s         ) ;
	//
	if      (+chroot_dir ) _chroot(chroot_dir)     ;
	if      (+repo_view_s) _chdir(repo_view_s    ) ;
	else if (+chroot_dir ) _chdir(phy_repo_root_s) ;
	//
	size_t work_idx = 0 ;
	for( auto const& [view,descr] : views ) if (+descr) {                                                                      // empty descr does not represent a view
		::string   abs_view = mk_abs(view,top_repo_root_s) ;
		::vector_s abs_phys ;                                abs_phys.reserve(descr.phys.size()) ;
		for( ::string const& phy : descr.phys ) abs_phys.push_back(mk_abs(phy,top_repo_root_s)) ;
		/**/                                    _create(report,view) ;
		for( ::string const& phy : descr.phys ) _create(report,phy ) ;
		if (is_dir_name(view)) {
			for( ::string const& cu : descr.copy_up ) {
				::string dst = descr.phys[0]+cu ;
				if (is_dir_name(cu))
					_create(report,dst) ;
				else
					for( size_t i : iota(1,descr.phys.size()) )
						if (_create(report,dst,descr.phys[i]+cu)) break ;
			}
		}
		size_t          sz     = descr.phys.size() ;
		::string const& upper  = descr.phys[0]     ;
		::string        work_s ;
		if (sz==1) {
			_mount_bind( abs_view , abs_phys[0] ) ;
		} else {
			work_s = is_lcl(upper) ? cat(work_dir_s,"work_",work_idx++,'/') : cat(no_slash(upper),".work/") ;                  // if not in the repo, it must be in tmp
			mk_dir_s(work_s) ;
			_mount_overlay( abs_view , abs_phys , mk_abs(work_s,top_repo_root_s) ) ;
		}
		if (+tmp_view_s) {
			if (view  .starts_with(tmp_view_s)) no_unlnk.insert(view  ) ;
			if (work_s.starts_with(tmp_view_s)) no_unlnk.insert(work_s) ;
		}
	}
	trace("done",report,top_repo_root_s) ;
	return true/*entered*/ ;
}

// XXX! : implement recursive views
// for now, phys cannot englobe or lie within a view, but when it is to be implemented, it is here
::vmap_s<::vector_s> JobSpace::flat_phys() const {
	::vmap_s<::vector_s> res ; res.reserve(views.size()) ;
	for( auto const& [view,descr] : views ) res.emplace_back(view,descr.phys) ;
	return res ;
}

void JobSpace::mk_canon(::string const& phy_repo_root_s) {
	auto do_top = [&]( ::string& dir_s , bool slash_ok , ::string const& key )->void {
		if ( !dir_s                                       ) return ;
		if ( !is_canon(dir_s)                             ) dir_s = Disk::mk_canon(dir_s) ;
		if ( slash_ok && dir_s=="/"                       ) return ;
		if (             dir_s=="/"                       ) throw cat(key," cannot be /"                                          ) ;
		if ( !is_abs(dir_s)                               ) throw cat(key," must be absolute : ",no_slash(dir_s)                  ) ;
		if ( phy_repo_root_s.starts_with(dir_s          ) ) throw cat("repository cannot lie within ",key,' ',no_slash(dir_s)     ) ;
		if ( dir_s          .starts_with(phy_repo_root_s) ) throw cat(key,' ',no_slash(dir_s)," cannot be local to the repository") ;
	} ;
	//                   slash_ok
	do_top( chroot_dir_s , true  , "chroot dir" ) ;
	do_top( lmake_view_s , false , "lmake view" ) ;
	do_top( repo_view_s  , false , "repo view"  ) ;
	do_top( tmp_view_s   , false , "tmp view"   ) ;
	if ( +lmake_view_s && +repo_view_s ) {
		if (lmake_view_s.starts_with(repo_view_s )) throw "lmake view "+no_slash(lmake_view_s)+" cannot lie within repo view " +no_slash(repo_view_s ) ;
 		if (repo_view_s .starts_with(lmake_view_s)) throw "repo view " +no_slash(repo_view_s )+" cannot lie within lmake view "+no_slash(lmake_view_s) ;
	}
	if ( +lmake_view_s && +tmp_view_s ) {
		if (lmake_view_s.starts_with(tmp_view_s  )) throw "lmake view "+no_slash(lmake_view_s)+" cannot lie within tmp view "  +no_slash(tmp_view_s  ) ;
		if (tmp_view_s  .starts_with(lmake_view_s)) throw "tmp view "  +no_slash(tmp_view_s  )+" cannot lie within lmake view "+no_slash(lmake_view_s) ;
	}
	if ( +repo_view_s && +tmp_view_s ) {
 		if (repo_view_s .starts_with(tmp_view_s  )) throw "repo view " +no_slash(repo_view_s )+" cannot lie within tmp view "  +no_slash(tmp_view_s  ) ;
		if (tmp_view_s  .starts_with(repo_view_s )) throw "tmp view "  +no_slash(tmp_view_s  )+" cannot lie within repo view " +no_slash(repo_view_s ) ;
	}
	//
	::string const& job_repo_root_s = repo_view_s | phy_repo_root_s ;
	auto do_path = [&](::string& path)->void {
		if      (!is_canon(path)                  ) path = Disk::mk_canon(path)         ;
		if      (path.starts_with("../")          ) path = mk_abs(path,job_repo_root_s) ;
		else if (path.starts_with(job_repo_root_s)) path.erase(0,job_repo_root_s.size()) ;
	} ;
	for( auto& [view,_] : views ) {
		do_path(view) ;
		if (!view                            ) throw "cannot map the whole repository"s                  ;
		if (job_repo_root_s.starts_with(view)) throw "repository cannot lie within view "+no_slash(view) ;
	}
	//
	for( auto& [view,descr] : views ) {
		bool is_dir_view = is_dir_name(view)  ;
		/**/                             if ( !is_dir_view && descr.phys.size()!=1                                     ) throw "cannot map non-dir " +no_slash(view)+" to an overlay" ;
		for( auto const& [v,_] : views ) if ( &v!=&view && view.starts_with(v) && (v.back()=='/'||view[v.size()]=='/') ) throw "cannot map "+no_slash(view)+" within "+v              ;
		bool lcl_view = _is_lcl_tmp(view) ;
		for( ::string& phy : descr.phys ) {
			do_path(phy) ;
			if ( !lcl_view && _is_lcl_tmp(phy)     ) throw "cannot map external view "+no_slash(view)+" to local or tmp "+no_slash(phy) ;
			if (  is_dir_view && !is_dir_name(phy) ) throw "cannot map dir "          +no_slash(view)+" to file "        +no_slash(phy) ;
			if ( !is_dir_view &&  is_dir_name(phy) ) throw "cannot map file "         +no_slash(view)+" to dir "         +no_slash(phy) ;
			if (+phy) {
				for( auto const& [v,_] : views ) {                                                                            // XXX! : suppress this check when recursive maps are implemented
					if ( phy.starts_with(v  ) && (v  .back()=='/'||phy[v  .size()]=='/') ) throw "cannot map "+no_slash(view)+" to "+no_slash(phy)+" within "    +no_slash(v) ;
					if ( v  .starts_with(phy) && (phy.back()=='/'||v  [phy.size()]=='/') ) throw "cannot map "+no_slash(view)+" to "+no_slash(phy)+" containing "+no_slash(v) ;
				}
			} else {
				for( auto const& [v,_] : views )                                                                              // XXX! : suppress this check when recursive maps are implemented
					if (!is_abs(v)) throw "cannot map "+no_slash(view)+" to full repository with "+no_slash(v)+" being map" ;
			}
		}
	}
}

//
// JobStartRpcReq
//

::string& operator+=( ::string& os , JobStartRpcReq const& jsrr ) {                                           // START_OF_NO_COV
	return os << "JobStartRpcReq(" << jsrr.seq_id <<','<< jsrr.job <<','<< jsrr.port <<','<< jsrr.msg <<')' ;
}                                                                                                             // END_OF_NO_COV

//
// JobEndRpcReq
//

::string& operator+=( ::string& os , ExecTraceEntry const& ete ) {                       // START_OF_NO_COV
	return os <<"ExecTraceEntry("<< ete.date <<','<< ete.step() <<','<< ete.file <<')' ;
}                                                                                        // END_OF_NO_COV

::string& operator+=( ::string& os , TargetDigest const& td ) { // START_OF_NO_COV
	const char* sep = "" ;
	/**/                    os << "TargetDigest("      ;
	if ( td.pre_exist   ) { os <<      "pre_exist"     ; sep = "," ; }
	if (+td.tflags      ) { os <<sep<< td.tflags       ; sep = "," ; }
	if (+td.extra_tflags) { os <<sep<< td.extra_tflags ; sep = "," ; }
	if (+td.crc         ) { os <<sep<< td.crc          ; sep = "," ; }
	if (+td.sig         )   os <<sep<< td.sig          ;
	return                  os <<')'                   ;
}                                                               // END_OF_NO_COV

::string& operator+=( ::string& os , JobEndRpcReq const& jerr ) {                                                                            // START_OF_NO_COV
	return os << "JobEndRpcReq(" << jerr.seq_id <<','<< jerr.job <<','<< jerr.digest <<','<< jerr.phy_tmp_dir_s <<','<< jerr.dyn_env <<')' ;
}                                                                                                                                            // END_OF_NO_COV

void JobEndRpcReq::cache_cleanup() {
	seq_id                     = SeqId(-1) ; // 0 is reserved to mean no info
	job                        = 0         ; // cache does not care
	digest.upload_key          = {}        ; // no recursive info
	phy_tmp_dir_s              = {}        ; // execution dependent
	for( auto& [_,td] : digest.targets ) {
		SWEAR(!td.pre_exist) ;               // else cannot be a candidate for upload as this must have failed
		td.sig = td.sig.tag() ;              // forget date, just keep tag
	}
}

//
// JobStartRpcReply
//

::string& operator+=( ::string& os , MatchFlags const& mf ) {                                                                                          // START_OF_NO_COV
	/**/             os << "MatchFlags(" ;
	switch (mf.is_target) {
		case Yes   : os << "target" ; if (+mf.tflags()           ) os<<','<<mf.tflags() ; if (+mf.extra_tflags()) os<<','<<mf.extra_tflags() ; break ;
		case No    : os << "dep,"   ; if (mf.dflags()!=DflagsDflt) os<<','<<mf.dflags() ; if (+mf.extra_dflags()) os<<','<<mf.extra_dflags() ; break ;
		case Maybe :                                                                                                                           break ;
	DF}                                                                                                                                                // NO_COV
	return           os << ')' ;
}                                                                                                                                                      // END_OF_NO_COV

::string& operator+=( ::string& os , JobStartRpcReply const& jsrr ) {         // START_OF_NO_COV
	/**/                           os << "JobStartRpcReply("                ;
	/**/                           os <<','  << to_hex(jsrr.addr)           ;
	/**/                           os <<','  << jsrr.autodep_env            ;
	if (+jsrr.job_space          ) os <<','  << jsrr.job_space              ;
	if ( jsrr.keep_tmp           ) os <<','  << "keep"                      ;
	if (+jsrr.ddate_prec         ) os <<','  << jsrr.ddate_prec             ;
	/**/                           os <<','  << mk_printable(cat(jsrr.env)) ; // env may contain the non-printable EnvPassMrkr value
	/**/                           os <<','  << jsrr.interpreter            ;
	/**/                           os <<','  << jsrr.kill_sigs              ;
	if (jsrr.live_out            ) os <<','  << "live_out"                  ;
	if (jsrr.allow_stderr        ) os <<','  << "allow_stderr"              ;
	/**/                           os <<','  << jsrr.method                 ;
	if (+jsrr.network_delay      ) os <<','  << jsrr.network_delay          ;
	if (+jsrr.pre_actions        ) os <<','  << jsrr.pre_actions            ;
	/**/                           os <<','  << jsrr.small_id               ;
	if (+jsrr.star_matches       ) os <<','  << jsrr.star_matches           ;
	if (+jsrr.deps               ) os <<'<'  << jsrr.deps                   ;
	if (+jsrr.static_matches     ) os <<'>'  << jsrr.static_matches         ;
	if (+jsrr.stdin              ) os <<'<'  << jsrr.stdin                  ;
	if (+jsrr.stdout             ) os <<'>'  << jsrr.stdout                 ;
	if (+jsrr.timeout            ) os <<','  << jsrr.timeout                ;
	if (+jsrr.cache_idx          ) os <<','  << jsrr.cache_idx              ;
	/**/                           os <<','  << jsrr.cmd                    ; // last as it is most probably multi-line
	return                         os <<')'                                 ;
}                                                                             // END_OF_NO_COV

bool/*entered*/ JobStartRpcReply::enter(
		::vmap_s<MountAction>&/*out*/ actions
	,	::map_ss             &/*out*/ cmd_env
	,	::vmap_ss            &/*out*/ dyn_env
	,	pid_t                &/*out*/ first_pid
	,	::string             &/*out*/ top_repo_root_s
	,	::string        const&        phy_lmake_root_s
	,	::string        const&        phy_repo_root_s
	,	::string        const&        phy_tmp_dir_s
	,	SeqId                         seq_id
) {
	Trace trace("JobStartRpcReply::enter",phy_lmake_root_s,phy_repo_root_s,phy_tmp_dir_s,seq_id) ;
	//
	for( auto& [k,v] : env )
		if      (v!=EnvPassMrkr)                                                         cmd_env[k] = ::move(v ) ;
		else if (has_env(k)    ) { ::string ev=get_env(k) ; dyn_env.emplace_back(k,ev) ; cmd_env[k] = ::move(ev) ; } // if special illegal value, use value from environment (typically from slurm)
	//
	autodep_env.repo_root_s = job_space.repo_view_s | phy_repo_root_s ;
	autodep_env.tmp_dir_s   = job_space.tmp_view_s  | phy_tmp_dir_s   ;
	if (+phy_tmp_dir_s) {
		_tmp_dir_s = autodep_env.tmp_dir_s ;                                       // for use in exit (autodep.tmp_dir_s may be moved)
		try {
			unlnk_inside_s(phy_tmp_dir_s,true/*abs_ok*/) ;                         // ensure tmp dir is clean
		} catch (::string const&) {
			try                       { mk_dir_s(phy_tmp_dir_s) ;            }     // ensure tmp dir exists
			catch (::string const& e) { throw "cannot create tmp dir : "+e ; }
		}
	} else {
		if (+job_space.tmp_view_s) throw "cannot map tmp dir "+job_space.tmp_view_s+" to nowhere" ;
	}
	//
	::string phy_work_dir_s = cat(PrivateAdminDirS,"work/",small_id,'/')                                                                                                                ;
	bool entered = job_space.enter(
		/*out*/actions
	,	/*out*/top_repo_root_s
	,	       phy_lmake_root_s
	,	       phy_repo_root_s
	,	       phy_tmp_dir_s
	,	       autodep_env.sub_repo_s
	,	       phy_work_dir_s
	,	       autodep_env.src_dirs_s
	) ;
	if (entered) {
		// find a good starting pid
		// the goal is to minimize risks of pid conflicts between jobs in case pid is used to generate unique file names as temporary file instead of using TMPDIR, which is quite common
		// to do that we spread pid's among the availale range by setting the first pid used by jos as apart from each other as possible
		// call phi the golden number and NPids the number of available pids
		// spreading is maximized by using phi*NPids as an elementary spacing and id (small_id) as an index modulo NPids
		// this way there is a conflict between job 1 and job 2 when (id2-id1)*phi is near an integer
		// because phi is the irrational which is as far from rationals as possible, and id's are as small as possible, this probability is minimized
		// note that this is over-quality : any more or less random number would do the job : motivation is mathematical beauty rather than practical efficiency
		static constexpr uint32_t FirstPid = 300                                 ; // apparently, pid's wrap around back to 300
		static constexpr uint64_t NPids    = MAX_PID - FirstPid                  ; // number of available pid's
		static constexpr uint64_t DeltaPid = (1640531527*NPids) >> n_bits(NPids) ; // use golden number to ensure best spacing (see above), 1640531527 = (2-(1+sqrt(5))/2)<<32
		first_pid = FirstPid + ((small_id*DeltaPid)>>(32-n_bits(NPids)))%NPids ;   // DeltaPid on 64 bits to avoid rare overflow in multiplication
	}
	trace("done",actions,cmd_env,dyn_env,first_pid,top_repo_root_s) ;
	return entered ;
}

static void _unlnk_inside_s( ::string const& dir_s , ::uset_s const& no_unlnk ) {
	try {
		for( ::string const& f : lst_dir_s(dir_s,dir_s) ) {
			if (no_unlnk.contains(f)  ) continue ;          // dont unlink file
			if (::unlink(f.c_str())==0) continue ;          // ok
			if (errno!=EISDIR         ) continue ;          // ignore errors as we have nothing much to do with them
			::string d_s = with_slash(f) ;
			if (no_unlnk.contains(d_s)) continue ;          // dont unlink dir
			//
			_unlnk_inside_s(d_s,no_unlnk) ;
			//
			::rmdir(f.c_str()) ;
		}
	} catch (::string const&) {}                            // ignore errors as we have nothing much to do with them
}

void JobStartRpcReply::exit() {
	// work dir cannot be cleaned up as we may have chroot'ed inside
	Trace trace("JobStartRpcReply::exit",STR(keep_tmp),_tmp_dir_s,job_space.no_unlnk) ;
	if ( !keep_tmp && +_tmp_dir_s ) _unlnk_inside_s( _tmp_dir_s , job_space.no_unlnk ) ;
	job_space.exit() ;
}

//
// JobMngtRpcReq
//

::string& operator+=( ::string& os , JobMngtRpcReq const& jmrr ) {                                                                // START_OF_NO_COV
	/**/                               os << "JobMngtRpcReq(" << jmrr.proc <<','<< jmrr.seq_id <<','<< jmrr.job <<','<< jmrr.fd ;
	switch (jmrr.proc) {
		case JobMngtProc::LiveOut    : os <<','<< jmrr.txt.size() ;                             break ;
		case JobMngtProc::ChkDeps    :
		case JobMngtProc::DepVerbose : os <<','<< jmrr.deps       ;                             break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.min_len    ;                             [[fallthrough]] ;
		case JobMngtProc::Decode     : os <<','<< jmrr.ctx <<','<< jmrr.file <<','<< jmrr.txt ; break ;
		default                      :                                                          break ;
	}
	return                             os <<')' ;
}                                                                                                                                 // END_OF_NO_COV

//
// JobMngtRpcReply
//

::string& operator+=( ::string& os , JobMngtRpcReply const& jmrr ) {           // START_OF_NO_COV
	/**/                               os << "JobMngtRpcReply(" << jmrr.proc ;
	switch (jmrr.proc) {
		case JobMngtProc::ChkDeps    : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<<                  jmrr.ok ; break ;
		case JobMngtProc::DepVerbose : os <<','<< jmrr.fd <<','<< jmrr.dep_infos                            ; break ;
		case JobMngtProc::Decode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
	DN}
	return                             os << ')' ;
}                                                                              // END_OF_NO_COV

//
// SubmitAttrs
//

::string& operator+=( ::string& os , SubmitAttrs const& sa ) {   // START_OF_NO_COV
	First first ;
	/**/                  os << "SubmitAttrs("                 ;
	if (+sa.used_backend) os <<first("",",")<< sa.used_backend ;
	if ( sa.live_out    ) os <<first("",",")<< "live_out"      ;
	if (+sa.pressure    ) os <<first("",",")<< sa.pressure     ;
	if (+sa.deps        ) os <<first("",",")<< sa.deps         ;
	if (+sa.reason      ) os <<first("",",")<< sa.reason       ;
	return                os <<')'                             ;
}                                                                // END_OF_NO_COV

//
// JobInfoStart
//

::string& operator+=( ::string& os , JobInfoStart const& jis ) {                                                       // START_OF_NO_COV
	return os << "JobInfoStart(" << jis.submit_attrs <<','<< jis.rsrcs <<','<< jis.pre_start <<','<< jis.start <<')' ;
}                                                                                                                      // END_OF_NO_COV

void JobInfoStart::cache_cleanup() {
	eta                      = {}        ; // cache does not care
	submit_attrs.cache_idx   = 0         ; // no recursive info
	submit_attrs.live_out    = false     ; // cache does not care
	submit_attrs.reason      = {}        ; // .
	pre_start.seq_id         = SeqId(-1) ; // 0 is reserved to mean no info
	pre_start.job            = 0         ; // cache does not care
	pre_start.port           = 0         ; // .
	start.addr               = 0         ; // .
	start.cache              = nullptr   ; // no recursive info
	start.live_out           = false     ; // cache does not care
	start.small_id           = 0         ; // execution dependent
	start.pre_actions        = {}        ; // .
	rsrcs                    = {}        ; // caching resources is meaningless as they have no impact on content
}

//
// JobInfo
//

void JobInfo::fill_from(::string const& file_name , JobInfoKinds need ) {
	Trace trace("fill_from",file_name,need) ;
	need &= ~JobInfoKind::None ;                                                                                                          // this is not a need, but it is practical to allow it
	if (!need) return ;                                                                                                                   // fast path : dont read file_name
	try {
		::string      job_info = AcFd(file_name).read() ;
		::string_view jis      = job_info              ;
		deserialize( jis , need[JobInfoKind::Start] ? start : ::ref(JobInfoStart()) ) ; need &= ~JobInfoKind::Start ; if (!need) return ; // skip if not needed
		deserialize( jis , need[JobInfoKind::End  ] ? end   : ::ref(JobEndRpcReq()) ) ; need &= ~JobInfoKind::End   ; if (!need) return ; // .
		deserialize( jis , dep_crcs                                                 ) ;
	} catch (...) {}                                                                                                                      // fill what we have
}

void JobInfo::update_digest() {
	if (!dep_crcs) return ;                                                   // nothing to update
	SWEAR( dep_crcs.size()==end.digest.deps.size() ) ;
	for( size_t i : iota(end.digest.deps.size()) )
		if (dep_crcs[i].valid()) end.digest.deps[i].second.crc(dep_crcs[i]) ;
	dep_crcs.clear() ;                                                        // now useless as info is recorded in digest
}

//
// codec
//

namespace Codec {

	::string mk_decode_node( ::string const& file , ::string const& ctx , ::string const& code ) {
		return CodecPfx+mk_printable<'/'>(file)+'/'+mk_printable<'/'>(ctx)+"/decode-"+mk_printable(code) ;
	}

	::string mk_encode_node( ::string const& file , ::string const& ctx , ::string const& val ) {
		return CodecPfx+mk_printable<'/'>(file)+'/'+mk_printable<'/'>(ctx)+"/encode-"+Xxh(val).digest().hex() ;
	}

	::string mk_file(::string const& node) {
		return parse_printable<'/'>(node,::ref(sizeof(CodecPfx)-1)) ; // account for terminating null in CodecPfx
	}

}
