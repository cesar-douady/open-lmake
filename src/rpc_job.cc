// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h> // unshare

#include "disk.hh"
#include "hash.hh"
#include "msg.hh"
#include "time.hh"
#include "trace.hh"
#include "caches/dir_cache.hh" // PER_CACHE : add include line for each cache method

#include "rpc_job.hh"

#if HAS_ZSTD
	#include <zstd.h>
#endif
#if HAS_ZLIB
	#define ZLIB_CONST
	#include <zlib.h>
#endif

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

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

::string do_file_actions( ::vector_s&/*out*/ unlnks , bool&/*out*/ incremental , ::vmap_s<FileAction>&& pre_actions , NfsGuard* nfs_guard ) {
	::uset_s                  keep_dirs       ;
	::string                  msg             ;
	::string                  trash           ;
	::uset_s                  existing_dirs_s ;
	::umap<UniqKey,UniqEntry> uniq_tab        ;
	//
	auto dir_exists = [&](::string const& f) {
		for( ::string d_s=dir_name_s(f) ; +d_s ; d_s = dir_name_s(d_s) )
			if (!existing_dirs_s.insert(d_s).second) break ;
	} ;
	//
	Trace trace("do_file_actions") ;
	unlnks.reserve(unlnks.size()+pre_actions.size()) ;                                 // most actions are unlinks
	for( auto const& [f,a] : pre_actions ) {                                           // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                    // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::Unlink         :
			case FileActionTag::UnlinkWarning  :
			case FileActionTag::UnlinkPolluted :
			case FileActionTag::None           : {
				if (nfs_guard) nfs_guard->access(f) ;
				FileStat fs ;
				if (::lstat(f.c_str(),&fs)!=0) {                                       // file does not exist, nothing to do
					trace(a.tag,"no_file",f) ;
					continue ;
				}
				FileSig sig        { fs } ;
				bool    quarantine ;
				if (!sig) {
					if ( a.tag==FileActionTag::None && sig.tag()==FileTag::Dir ) {     // if None, we dont want to generate the file, so dir is ok (and there may be sub-files to generate)
						trace(a.tag,"dir",f) ;
						continue ;
					}
					trace(a.tag,"awkward",f) ;
					quarantine = true ;
				} else {
					quarantine =
						sig!=a.sig
					&&	sig.tag()!=Crc::Empty
					&&	( a.crc==Crc::None || !a.crc.valid() || !a.crc.match(Crc(f)) ) // only compute crc if file has been modified
					;
					dir_exists(f) ;                                                    // if a file exists, its dir necessarily exists
				}
				if (quarantine) {
					if (nfs_guard) nfs_guard->update(f) ;
					::string qf = QuarantineDirS+f ;
					if (::rename( f.c_str() , dir_guard(qf).c_str() )!=0) {
						unlnk( qf , {.dir_ok=true} ) ;                                                                         // try to unlink, in case it is a dir
						if (::rename( f.c_str() , qf.c_str() )!=0) throw "cannot quarantine "+f ;                              // and retry
					}
					msg <<"quarantined " << mk_file(f) <<'\n' ;
				} else {
					SWEAR(is_lcl(f)) ;
					if (!unlnk(f,{.nfs_guard=nfs_guard})) throw "cannot unlink "+f ;
					if ( a.tag==FileActionTag::None && !a.tflags[Tflag::NoWarning] ) msg <<"unlinked "<<mk_file(f)<<'\n' ;     // if a file has been unlinked, its dir necessarily exists
				}
				trace(a.tag,STR(quarantine),f) ;
				if (+sig) unlnks.push_back(f) ;
			} break ;
			case FileActionTag::Uniquify : {
				FileStat fs ;
				if (::lstat(f.c_str(),&fs)!=0                  ) { trace(a.tag,"no_file"    ,f) ; continue ;           }       // file does not exist, nothing to do
				if (a.tflags[Tflag::Target]                    ) { trace(a.tag,"incremental",f) ; incremental = true ; }
				dir_exists(f) ;                                                                                                // if file exists, certainly its dir exists as well
				if (   fs.st_nlink<=1                          ) { trace(a.tag,"single"     ,f) ; continue ;           }       // file is already unique (or unlinked in parallel), nothing to do
				if (!( fs.st_mode & (S_IWUSR|S_IWGRP|S_IWOTH) )) { trace(a.tag,"read-only"  ,f) ; continue ;           }       // if file is read-only, assume it is immutable
				if (!  S_ISREG(fs.st_mode)                     ) { trace(a.tag,"awkward"    ,f) ; continue ;           }       // do not handle awkward files and symlinks are immutable
				UniqEntry& e = uniq_tab[{fs.st_dev,fs.st_ino}] ;                                                               // accumulate all links per file identified by dev/inode
				if (!e.files) {      e.n_lnks= fs.st_nlink ;  e.sz= fs.st_size ;  e.mode= fs.st_mode ;  e.mtim= fs.st_mtim ; }
				else          SWEAR( e.n_lnks==fs.st_nlink && e.sz==fs.st_size && e.mode==fs.st_mode && e.mtim==fs.st_mtim ) ; // check consistency
				e.files.push_back(f) ;
				e.no_warning &= a.tflags[Tflag::NoWarning] ;
			} break ;
			case FileActionTag::Mkdir : {
				::string f_s = with_slash(f) ;
				if (!existing_dirs_s.contains(f_s)) mk_dir_s(f_s,{.nfs_guard=nfs_guard}) ;
			} break ;
			case FileActionTag::Rmdir :
				if (!keep_dirs.contains(f))
					try {
						rmdir_s(with_slash(f),nfs_guard) ;
					} catch (::string const&) {                                                                                // if a dir cannot rmdir'ed, no need to try those uphill
						keep_dirs.insert(f) ;
						for( ::string d_s=dir_name_s(f) ; +d_s ; d_s=dir_name_s(d_s) )
							if (!keep_dirs.insert(no_slash(d_s)).second) break ;
					}
			break ;
		DF}                                                                                                                    // NO_COV
	}
	for( auto const& [_,e] : uniq_tab ) {
		SWEAR( e.files.size()<=e.n_lnks , e.n_lnks,e.files ) ;                                                                 // check consistency
		if (e.n_lnks==e.files.size()) { trace("all_lnks",e.files) ; continue ; }                                               // we have all the links, nothing to do
		trace("uniquify",e.n_lnks,e.files) ;
		//
		const char* err = nullptr/*garbage*/ ;
		{	const char* f0   = e.files[0].c_str()                                 ;
			AcFd        rfd  = ::open    ( f0 , O_RDONLY|O_NOFOLLOW             ) ; if (!rfd   ) { err = "cannot open for reading" ; goto Bad ; }
			int         urc  = ::unlink  ( f0                                   ) ; if (urc !=0) { err = "cannot unlink"           ; goto Bad ; }
			AcFd        wfd  = ::open    ( f0 , O_WRONLY|O_CREAT , e.mode       ) ; if (!wfd   ) { err = "cannot open for writing" ; goto Bad ; }
			int         sfrc = ::sendfile( wfd , rfd , nullptr/*offset*/ , e.sz ) ; if (sfrc<0 ) { err = "cannot copy"             ; goto Bad ; }
			for( size_t i : iota(1,e.files.size()) ) {
				if (::unlink(   e.files[i].c_str())!=0) { err = "cannot unlink" ; goto Bad ; }
				if (::link  (f0,e.files[i].c_str())!=0) { err = "cannot link"   ; goto Bad ; }
			}
			struct ::timespec times[2] = { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , e.mtim } ;
			::futimens(wfd,times) ;                                                                                            // maintain original date
			if (!e.no_warning) {
				/**/                               msg <<"uniquified"  ;
				if (e.files.size()>1)              msg <<" as a group" ;
				/**/                               msg <<" :"          ;
				for( ::string const& f : e.files ) msg <<' '<< f       ;
				/**/                               msg <<'\n'          ;
			}
		}
		continue ;
	Bad :                                                                                                                      // NO_COV defensive programming
		throw cat(err," while uniquifying ",e.files) ;                                                                         // NO_COV .
	}
	trace("done",localize(msg)) ;
	return msg ;
}

//
// JobReason
//

::string& operator+=( ::string& os , JobReason const& jr ) { // START_OF_NO_COV
	/**/                               os <<"JR("<< jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os <<','<< jr.node  ;
	return                             os <<')'            ;
}                                                            // END_OF_NO_COV

void JobReason::chk() const {
	if (tag<JobReasonTag::HasNode) throw_unless( !node , "bad node" ) ;
}

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
// JobRpcReq
//

void JobRpcReq::cache_cleanup() {
	seq_id = -1 ;                 // execution dependnt, 0 is reserved to mean no info but we want a stable info
	job    = 0  ;                 // base dependent
}

void JobRpcReq::chk(bool for_cache) const {
	if (for_cache) {
		throw_unless( seq_id==SeqId(-1) , "bad seq_id",' ',seq_id ) ;
		throw_unless( !job              , "bad job"    ) ;
	}
}

//
// Zlvl
//

::string& operator+=( ::string& os , Zlvl zl ) { // START_OF_NO_COV
	/**/     os <<      zl.tag ;
	if (+zl) os <<':'<< zl.lvl ;
	return   os                ;
}                                                // END_OF_NO_COV

//
// Cache
//

namespace Caches {

	struct DeflateFd : AcFd {
		static Cache::Sz s_max_sz( Cache::Sz sz , Zlvl zlvl={} ) {
			if (!zlvl) return sz ;
			switch (zlvl.tag) {
				case ZlvlTag::Zlib :
					throw_unless( HAS_ZLIB , "cannot compress without zlib" ) ;
					#if HAS_ZLIB
						static_assert(sizeof(ulong)==sizeof(Cache::Sz)) ;                                                     // compressBound manages ulong and we need a Sz
						return ::compressBound(sz) ;
					#endif
				break ;
				case ZlvlTag::Zstd :
					throw_unless( HAS_ZSTD , "cannot compress without zstd" ) ;
					#if HAS_ZSTD
						static_assert(sizeof(size_t)==sizeof(Cache::Sz)) ;                                                    // ZSTD_compressBound manages size_t and we need a Sz
						return ::ZSTD_compressBound(sz) ;
					#endif
				break ;
			DF}
			FAIL() ;
		}
		// cxtors & casts
		DeflateFd() = default ;
		DeflateFd( AcFd&& fd , Zlvl zl={} ) : AcFd{::move(fd)} , zlvl{zl} {
			if (!zlvl) return ;
			switch (zlvl.tag) {
				case ZlvlTag::Zlib : {
					throw_unless( HAS_ZLIB , "cannot compress without zlib" ) ;
					#if HAS_ZLIB
						zlvl.lvl    = ::min( zlvl.lvl , uint8_t(Z_BEST_COMPRESSION) ) ;
						_zlib_state = {}                                              ;
						int rc = ::deflateInit(&_zlib_state,zlvl.lvl) ; SWEAR(rc==Z_OK) ;
						_zlib_state.next_in  = ::launder(reinterpret_cast<uint8_t const*>(_buf)) ;
						_zlib_state.avail_in = 0                                                 ;
					#endif
				} break ;
				case ZlvlTag::Zstd :
					throw_unless( HAS_ZSTD , "cannot compress without zstd" ) ;
					#if HAS_ZSTD
						zlvl.lvl    = ::min( zlvl.lvl , uint8_t(::ZSTD_maxCLevel()) ) ;
						_zstd_state = ::ZSTD_createCCtx()                             ; SWEAR(_zstd_state) ;
						::ZSTD_CCtx_setParameter( _zstd_state , ZSTD_c_compressionLevel , zlvl.lvl ) ;
					#endif
				break ;
			DF}
		}
		~DeflateFd() {
			flush() ;
			if (!zlvl) return ;
			switch (zlvl.tag) {
				case ZlvlTag::Zlib :
					SWEAR(HAS_ZLIB) ;
					#if HAS_ZLIB
						::deflateEnd(&_zlib_state) ;
					#endif
				break ;
				case ZlvlTag::Zstd :
					SWEAR(HAS_ZSTD) ;
					#if HAS_ZSTD
						::ZSTD_freeCCtx(_zstd_state) ;
					#endif
				break ;
			DF}
		}
		// services
		void write(::string const& s) {
			if (!s) return ;
			SWEAR(!_flushed) ;
			if (+zlvl) {
				switch (zlvl.tag) {
					case ZlvlTag::Zlib :
						SWEAR(HAS_ZLIB) ;
						#if HAS_ZLIB
							_zlib_state.next_in  = ::launder(reinterpret_cast<uint8_t const*>(s.data())) ;
							_zlib_state.avail_in = s.size()                                              ;
							while (_zlib_state.avail_in) {
								_zlib_state.next_out  = ::launder(reinterpret_cast<uint8_t*>( _buf + _pos )) ;
								_zlib_state.avail_out = DiskBufSz - _pos                                     ;
								::deflate(&_zlib_state,Z_NO_FLUSH) ;
								_pos = DiskBufSz - _zlib_state.avail_out ;
								_flush(1/*room*/) ;
							}
						#endif
					break ;
					case ZlvlTag::Zstd : {
						SWEAR(HAS_ZSTD) ;
						#if HAS_ZSTD
							::ZSTD_inBuffer  in_buf  { .src=s.data() , .size=s.size()  , .pos=0 } ;
							::ZSTD_outBuffer out_buf { .dst=_buf     , .size=DiskBufSz , .pos=0 } ;
							while (in_buf.pos<in_buf.size) {
								out_buf.pos = _pos ;
								::ZSTD_compressStream2( _zstd_state , &out_buf , &in_buf , ZSTD_e_continue ) ;
								_pos = out_buf.pos ;
								_flush(1/*room*/) ;
							}
						#endif
					} break ;
				DF}
			} else {                                                                                                          // no compression
				if (_flush(s.size())) {                ::memcpy( _buf+_pos , s.data() , s.size() ) ; _pos     += s.size() ; } // small data : put in _buf
				else                  { SWEAR(!_pos) ; AcFd::write(s)                              ; total_sz += s.size() ; } // large data : send directly
			}
		}
		void send_from( Fd fd_ , size_t sz ) {
			if (!sz) return ;
			if (+zlvl) {
				SWEAR(!_flushed) ;
				while (sz) {
					size_t   cnt = ::min( sz , DiskBufSz ) ;
					::string s   = fd_.read(cnt)           ; throw_unless( s.size()==cnt , "missing ",cnt-s.size()," bytes from ",fd ) ;
					write(s) ;
					sz -= cnt ;
				}
			} else {
				if (_flush(sz)) {                size_t c=fd_.read_to({_buf+_pos,sz})               ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; _pos    +=c  ; } // small : put in _buf
				else            { SWEAR(!_pos) ; size_t c=::sendfile(self,fd_,nullptr/*offset*/,sz) ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; total_sz+=sz ; } // large : send directly
			}
		}
		void flush() {
			if (_flushed) return ;
			_flushed = true ;
			if (+zlvl) {
				switch (zlvl.tag) {
					case ZlvlTag::Zlib :
						SWEAR(HAS_ZLIB) ;
						#if HAS_ZLIB
							_zlib_state.next_in  = nullptr ;
							_zlib_state.avail_in = 0       ;
							for (;;) {
								_zlib_state.next_out  = ::launder(reinterpret_cast<uint8_t*>(_buf+_pos)) ;
								_zlib_state.avail_out = DiskBufSz - _pos                                 ;
								int rc = ::deflate(&_zlib_state,Z_FINISH) ;
								_pos = DiskBufSz - _zlib_state.avail_out ;
								if (rc==Z_BUF_ERROR) throw cat("cannot flush ",self) ;
								_flush() ;
								if (rc==Z_STREAM_END) return ;
							}
						#endif
					break ;
					case ZlvlTag::Zstd : {
						SWEAR(HAS_ZSTD) ;
						#if HAS_ZSTD
							::ZSTD_inBuffer  in_buf  { .src=nullptr , .size=0         , .pos=0 } ;
							::ZSTD_outBuffer out_buf { .dst=_buf    , .size=DiskBufSz , .pos=0 } ;
							for (;;) {
								out_buf.pos = _pos ;
								size_t rc = ::ZSTD_compressStream2( _zstd_state , &out_buf , &in_buf , ZSTD_e_end ) ;
								_pos = out_buf.pos ;
								if (::ZSTD_isError(rc)) throw cat("cannot flush ",self) ;
								_flush() ;
								if (!rc) return ;
							}
						#endif
					} break ;
				DF}
			}
			_flush() ;
		}
	private :
		bool/*room_ok*/ _flush(size_t room=DiskBufSz) {        // flush if not enough room
			if (_pos+room<=DiskBufSz) return true/*room_ok*/ ; // enough room
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
		Zlvl         zlvl     ;
	private :
		char   _buf[DiskBufSz] ;
		size_t _pos            = 0     ;
		bool   _flushed        = false ;
		#if HAS_ZLIB && HAS_ZSTD
			union {
				::z_stream   _zlib_state = {} ;
				::ZSTD_CCtx* _zstd_state ;
			} ;
		#elif HAS_ZLIB
			::z_stream   _zlib_state = {} ;
		#elif HAS_ZSTD
			::ZSTD_CCtx* _zstd_state = nullptr ;
		#endif
	} ;

	struct InflateFd : AcFd {
		// cxtors & casts
		InflateFd() = default ;
		InflateFd( AcFd&& fd , Zlvl zl={} ) : AcFd{::move(fd)} , zlvl{zl} {
			if (!zlvl) return ;
			switch (zlvl.tag) {
				case ZlvlTag::Zlib : {
					throw_unless( HAS_ZLIB , "cannot compress without zlib" ) ;
					#if HAS_ZLIB
						int rc = ::inflateInit(&_zlib_state) ; SWEAR(rc==Z_OK,self) ;
					#endif
				} break ;
				case ZlvlTag::Zstd :
					throw_unless( HAS_ZSTD , "cannot compress without zstd" ) ;
					#if HAS_ZSTD
						_zstd_state = ::ZSTD_createDCtx() ; SWEAR(_zstd_state,self) ;
					#endif
				break ;
			DF}
		}
		~InflateFd() {
			if (!zlvl) return ;
			switch (zlvl.tag) {
				case ZlvlTag::Zlib : {
					SWEAR( HAS_ZLIB , zlvl ) ;
					#if HAS_ZLIB
						int rc = ::inflateEnd(&_zlib_state) ; SWEAR( rc==Z_OK , rc,self ) ;
					#endif
				} break ;
				case ZlvlTag::Zstd : {
					SWEAR( HAS_ZSTD , zlvl ) ;
					#if HAS_ZSTD
						size_t rc = ::ZSTD_freeDCtx(_zstd_state) ; SWEAR( !::ZSTD_isError(rc) , rc,self ) ;
					#endif
				} break ;
			DF}
		}
		// services
		::string read(size_t sz) {
			if (!sz) return {} ;
			::string res ( sz , 0 ) ;
			if (+zlvl)
				switch (zlvl.tag) {
					case ZlvlTag::Zlib :
						SWEAR( HAS_ZLIB , zlvl ) ;
						#if HAS_ZLIB
							_zlib_state.next_out  = ::launder(reinterpret_cast<uint8_t*>(res.data())) ;
							_zlib_state.avail_out = res.size()                                        ;
							while (_zlib_state.avail_out) {
								if (!_len) {
									_len = AcFd::read_to({_buf,DiskBufSz}) ; throw_unless(_len>0,"missing ",_zlib_state.avail_out," bytes from ",self) ;
									_pos = 0                               ;
								}
								_zlib_state.next_in  = ::launder(reinterpret_cast<uint8_t const*>( _buf + _pos )) ;
								_zlib_state.avail_in = _len                                                       ;
								::inflate(&_zlib_state,Z_NO_FLUSH) ;
								_pos = ::launder(reinterpret_cast<char const*>(_zlib_state.next_in)) - _buf ;
								_len = _zlib_state.avail_in                                                 ;
							}
						#endif
					break ;
					case ZlvlTag::Zstd : {
						SWEAR( HAS_ZSTD , zlvl ) ;
						#if HAS_ZSTD
							::ZSTD_inBuffer  in_buf  { .src=_buf       , .size=0  , .pos=0 } ;
							::ZSTD_outBuffer out_buf { .dst=res.data() , .size=sz , .pos=0 } ;
							while (out_buf.pos<sz) {
								if (!_len) {
									_len = AcFd::read_to({_buf,DiskBufSz}) ; throw_unless(_len>0,"missing ",sz-out_buf.pos," bytes from ",self) ;
									_pos = 0                               ;
								}
								in_buf.pos  = _pos      ;
								in_buf.size = _pos+_len ;
								size_t rc = ::ZSTD_decompressStream( _zstd_state , &out_buf , &in_buf ) ; SWEAR(!::ZSTD_isError(rc)) ;
								_pos = in_buf.pos               ;
								_len = in_buf.size - in_buf.pos ;
							}
						#endif
					} break ;
				DF}
			else {
				size_t cnt = ::min( sz , _len ) ;
				if (cnt) {                                                                                                           // gather available data from _buf
					::memcpy( res.data() , _buf+_pos , cnt ) ;
					_pos += cnt ;
					_len -= cnt ;
					sz   -= cnt ;
				}
				if (sz) {
					SWEAR( !_len , _len ) ;
					if (sz>=DiskBufSz) {                                                                                             // large data : read directly
						size_t c = AcFd::read_to({&res[cnt],sz}) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
					} else {                                                                                                         // small data : bufferize
						_len = AcFd::read_to({_buf,DiskBufSz}) ;   throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
						::memcpy( &res[cnt] , _buf , sz ) ;
						_pos  = sz ;
						_len -= sz ;
					}
				}
			}
			return res ;
		}
		void receive_to( Fd fd_ , size_t sz ) {
			if (+zlvl) {
				while (sz) {
					size_t   cnt = ::min( sz , DiskBufSz ) ;
					::string s   = read(cnt)               ; SWEAR(s.size()==cnt,s.size(),cnt) ;
					fd_.write(s) ;
					sz -= cnt ;
				}
			} else {
				size_t cnt = ::min( sz , _len ) ;
				if (cnt) {                                                                                                           // gather available data from _buf
					fd_.write({_buf+_pos,cnt}) ;
					_pos += cnt ;
					_len -= cnt ;
					sz   -= cnt ;
				}
				if (sz) {
					SWEAR( !_len , _len ) ;
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
		}
		// data
		Zlvl zlvl ;
	private :
		char   _buf[DiskBufSz] ;
		size_t _pos            = 0 ;
		size_t _len            = 0 ;
		#if HAS_ZLIB && HAS_ZSTD
			union {
				::z_stream   _zlib_state = {} ;
				::ZSTD_DCtx* _zstd_state ;
			} ;
		#elif HAS_ZLIB
			::z_stream _zlib_state = {} ;
		#elif HAS_ZSTD
			::ZSTD_DCtx* _zstd_state = nullptr ;
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
		Cache*& cache = grow(s_tab,idx) ; SWEAR(!cache) ;
		if (+tag) {
			Cache* c = s_new(tag) ;
			c->config( dct , true/*may_init*/ ) ;
			cache = c ;                           // only record cache once we are sure config is ok
		}
	}

	JobInfo Cache::download( ::string const& job , ::string const& key , bool no_incremental , NfsGuard* repo_nfs_guard ) {
		Trace trace("Cache::download",key) ;
		//
		::pair<JobInfo,AcFd>    info_fd  = sub_download( job , key ) ;
		JobInfo               & job_info = info_fd.first             ;
		Zlvl                    zlvl     = job_info.start.start.zlvl ;
		JobEndRpcReq          & end      = job_info.end              ;
		JobDigest<>           & digest   = end.digest                ; throw_if( digest.incremental && no_incremental , "cached job was incremental" ) ;
		::vmap_s<TargetDigest>& targets  = digest.targets            ;
		NodeIdx                 n_copied = 0                         ;
		//
		try {
			#if !HAS_ZLIB
				throw_if( zlvl.tag==ZlvlTag::Zlib , "cannot uncompress without zlib" ) ;
			#endif
			#if !HAS_ZSTD
				throw_if( zlvl.tag==ZlvlTag::Zstd , "cannot uncompress without zstd" ) ;
			#endif
			//
			InflateFd data_fd { ::move(info_fd.second) , zlvl } ;
			Hdr       hdr     = IMsgBuf().receive<Hdr>(data_fd) ; SWEAR( hdr.target_szs.size()==targets.size() , hdr.target_szs.size(),targets.size() ) ;
			//
			for( NodeIdx ti : iota(targets.size()) ) {
				auto&           entry = targets[ti]            ;
				::string const& tn    = entry.first            ;
				FileTag         tag   = entry.second.sig.tag() ;
				Sz              sz    = hdr.target_szs[ti]     ;
				n_copied = ti+1 ;                                                                         // this is a protection, so record n_copied *before* action occurs
				if (repo_nfs_guard    )       repo_nfs_guard->change(tn) ;
				if (tag==FileTag::None) try { unlnk( tn                  ) ; } catch (::string const&) {} // if we do not want the target, avoid unlinking potentially existing sub-files
				else                          unlnk( tn , {.dir_ok=true} ) ;
				switch (tag) {
					case FileTag::None  :                                                                                               break ;
					case FileTag::Lnk   : trace("lnk_to"  ,tn,sz) ; sym_lnk( tn , data_fd.read(hdr.target_szs[ti]) )                  ; break ;
					case FileTag::Empty : trace("empty_to",tn   ) ; AcFd   ( tn , {O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW,0666/*mod*/} ) ; break ;
					case FileTag::Exe   :
					case FileTag::Reg   : {
						AcFd fd { tn , { O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW , mode_t(tag==FileTag::Exe?0777:0666) } } ;
						if (sz) { trace("write_to"  ,tn,sz) ; data_fd.receive_to( fd , sz ) ; }
						else      trace("no_data_to",tn   ) ;                                             // empty exe are Exe, not Empty
					} break ;
				DN}
				entry.second.sig = FileSig(tn) ;                                                          // target digest is not stored in cache
			}
			end.end_date = New ;                                                                          // date must be after files are copied
			// ensure we take a single lock at a time to avoid deadlocks
			trace("done") ;
			return job_info ;
		} catch(::string const& e) {
			trace("failed",e,n_copied) ;
			for( NodeIdx ti : iota(n_copied) ) unlnk(targets[ti].first) ;                                 // clean up partial job
			throw ;
		}
	}

	uint64_t/*upload_key*/ Cache::upload( ::vmap_s<TargetDigest> const& targets , ::vector<FileInfo> const& target_fis , Zlvl zlvl ) {
		Trace trace("DirCache::upload",targets.size(),zlvl) ;
		SWEAR( targets.size()==target_fis.size() , targets.size(),target_fis.size() ) ;
		//
		Sz  tgts_sz  = 0 ;
		Hdr hdr      ;     hdr.target_szs.reserve(target_fis.size()) ;
		for( FileInfo fi : target_fis ) {
			tgts_sz += fi.sz ;
			hdr.target_szs.push_back(fi.sz) ;
		}
		//
		Sz                                  z_max_sz = DeflateFd::s_max_sz(tgts_sz,zlvl) ;
		::pair<uint64_t/*upload_key*/,AcFd> key_fd   = sub_upload(z_max_sz)              ;
		//
		try {
			DeflateFd data_fd { ::move(key_fd.second) , zlvl } ;
			OMsgBuf().send( data_fd , hdr ) ;
			//
			for( NodeIdx ti : iota(targets.size()) ) {
				::pair_s<TargetDigest> const& entry = targets[ti]            ;
				::string               const& tn    = entry.first            ;
				FileTag                       tag   = entry.second.sig.tag() ;
				Sz                            sz    = target_fis[ti].sz      ;
				switch (tag) {
					case FileTag::Lnk   : { trace("lnk_from"  ,tn,sz) ; ::string l = read_lnk(tn) ; throw_unless(l.size()==sz,"cannot readlink ",tn) ; data_fd.write(l) ; goto ChkSig ; }
					case FileTag::Empty :   trace("empty_from",tn   ) ;                                                                                                   break       ;
					case FileTag::Reg   :
					case FileTag::Exe   :
						if (sz) {
							trace("read_from",tn,sz) ;
							data_fd.send_from( AcFd(tn,{.flags=O_RDONLY|O_NOFOLLOW}) , sz ) ;
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
		} catch (::string const& e) {
			dismiss(key_fd.first) ;
			trace("failed") ;
			throw ;
		}
		trace("done",tgts_sz,z_max_sz) ;
		return key_fd.first ;
	}

	void Cache::commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		Trace trace("Cache::commit",upload_key,job) ;
		//
		if (!( +job_info.start && +job_info.end )) { // we need a full report to cache job
			trace("no_ancillary_file") ;
			dismiss(upload_key) ;
			throw "no ancillary file"s ;
		}
		//
		job_info.update_digest() ;                   // ensure cache has latest crc available
		// check deps
		for( auto const& [dn,dd] : job_info.end.digest.deps )
			if (!dd.is_crc) {
				trace("not_a_crc_dep",dn,dd) ;
				dismiss(upload_key) ;
				throw "not a crc dep"s ;
			}
		job_info.cache_cleanup() ;                   // defensive programming : remove useless/meaningless info
		//
		sub_commit( upload_key , job , ::move(job_info) ) ;
	}

}

//
// JobSpace
//

::string& operator+=( ::string& os , JobSpace::ViewDescr const& vd ) { // START_OF_NO_COV
	/**/             os <<"ViewDescr("<< vd.phys ;
	if (+vd.copy_up) os <<",CU:"<< vd.copy_up    ;
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

	static void _chroot(::string const& dir_s) { Trace trace("_chroot",dir_s) ; if (::chroot(dir_s.c_str())!=0) throw cat("cannot chroot to ",no_slash(dir_s)," : ",StrErr()) ; }
	static void _chdir (::string const& dir_s) { Trace trace("_chdir" ,dir_s) ; if (::chdir (dir_s.c_str())!=0) throw cat("cannot chdir to " ,no_slash(dir_s)," : ",StrErr()) ; }

	static void _mount_bind( ::string const& dst , ::string const& src ) { // src and dst may be files or dirs
		Trace trace("_mount_bind",dst,src) ;
		throw_unless( ::mount( src.c_str() , dst.c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )==0 , "cannot bind mount ",src," onto ",dst," : ",StrErr() ) ;
	}

void JobSpace::chk() const {
	throw_unless( !chroot_dir_s || (chroot_dir_s.front()=='/'&&chroot_dir_s.back()=='/'                       ) , "bad chroot_dir" ) ;
	throw_unless( !lmake_view_s || (lmake_view_s.front()=='/'&&lmake_view_s.back()=='/'                       ) , "bad lmake_view" ) ;
	throw_unless( !repo_view_s  || (repo_view_s .front()=='/'&&repo_view_s .back()=='/'&&is_canon(repo_view_s)) , "bad repo_view"  ) ;
	throw_unless( !tmp_view_s   || (tmp_view_s  .front()=='/'&&tmp_view_s  .back()=='/'&&is_canon(tmp_view_s )) , "bad tmp_view"   ) ;
	for( auto const& [view,phys] : views ) {
		/**/                                     throw_unless( is_canon(view) , "bad views"        ) ;
		for( ::string const& p  : phys.phys    ) throw_unless( is_canon(p )   , "bad view phys"    ) ;
		for( ::string const& cu : phys.copy_up ) throw_unless( is_canon(cu)   , "bad view copy_up" ) ;
	}
}

static void _mount_overlay( ::string const& dst_s , ::vector_s const& srcs_s , ::string const& work_s ) {
	SWEAR( +srcs_s                               ) ;
	SWEAR( srcs_s.size()>1 , dst_s,srcs_s,work_s ) ;                                     // use bind mount in that case
	//
	Trace trace("_mount_overlay",dst_s,srcs_s,work_s) ;
	for( size_t i : iota(1,srcs_s.size()) )
		if (srcs_s[i].find(':')!=Npos)
			throw cat("cannot overlay mount ",dst_s," to ",srcs_s,"with embedded ':'") ; // NO_COV defensive programming
	mk_dir_s(work_s) ;
	//
	::string                                data  = "userxattr"                      ;
	/**/                                    data += ",upperdir="+no_slash(srcs_s[0]) ;
	/**/                                    data += ",lowerdir="+no_slash(srcs_s[1]) ;
	for( size_t i : iota(2,srcs_s.size()) ) data += ':'         +no_slash(srcs_s[i]) ;
	/**/                                    data += ",workdir=" +no_slash(work_s   ) ;
	throw_unless( ::mount( nullptr ,  dst_s.c_str() , "overlay" , 0 , data.c_str() )==0 , "cannot overlay mount ",dst_s," to ",data," : ",StrErr()) ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AcFd    fd  { file , {.flags=O_WRONLY|O_TRUNC,.err_ok=false} } ;
	ssize_t cnt = ::write( fd , data.c_str() , data.size() )       ;
	throw_unless( cnt>=0                   , "cannot write atomically ",data.size()," bytes to ",file," : ",StrErr()                  ) ;
	throw_unless( size_t(cnt)>=data.size() , "cannot write atomically ",data.size()," bytes to ",file," : only ",cnt," bytes written" ) ;
}

bool JobSpace::_is_lcl_tmp(::string const& f) const {
	if (is_lcl_s(f)) return true                      ;
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
		AcFd fd { dst , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.err_ok=true} } ;
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
		if (!inserted) {
			if (+it->second) it->second << ':'                ;
			/**/             it->second << PY_LD_LIBRARY_PATH ;
		}
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
,	::string   const&             phy_tmp_dir_s    , bool keep_tmp
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
	if (+tmp_view_s) throw_unless( +phy_tmp_dir_s , "no physical dir for tmp view ",no_slash(tmp_view_s) ) ;
	::string const& tmp_dir_s = tmp_view_s | phy_tmp_dir_s ;
	SWEAR( !work_dir_s.starts_with(tmp_dir_s) , tmp_dir_s,work_dir_s ) ;              // else we have to detect if some view descr has size>1 in addition to view lying in tmp
	if (!keep_tmp) {
		bool mount_in_tmp = false ;
		for( auto const& [view,descr] : views )
			if ( +descr && view.starts_with(tmp_dir_s) ) {                            // empty descr does not represent a view
				mount_in_tmp = true ;
				break ;
			}
		//
		if (mount_in_tmp) {
			// if a dir (or a file) is mounted in tmp dir, we cannot directly clean it up as we should umount it beforehand to avoid unlinking the mounted info
			// but umount is privileged, so what we do instead is forking
			// in parent, we are outside the namespace where the mount is not seen and we can clean tmp dir safely
			// in child, we carry the whole job
			if ( pid_t child_pid=::fork() ; child_pid!=0 ) {                          // in parent
				int wstatus ;
				if ( ::waitpid(child_pid,&wstatus,0/*options*/)!=child_pid ) FAIL() ;
				unlnk( phy_tmp_dir_s , {.dir_ok=true,.abs_ok=true} ) ;                // unlkink when child is done
				if      (WIFEXITED  (wstatus)) ::_exit(    WEXITSTATUS(wstatus)) ;    // all the cleanup stuff is done by the child, so we want to do nothing here
				else if (WIFSIGNALED(wstatus)) ::_exit(128+WTERMSIG   (wstatus)) ;
				else                           ::_exit(255                     ) ;
			}
		} else {
			_tmp_dir_s = tmp_dir_s ;                                                  // unlink upon exit
		}
	}
	//
	int uid = ::getuid() ;                                                            // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                            // .
	//
	throw_unless( ::unshare(CLONE_NEWUSER|CLONE_NEWNS)==0 , "cannot create namespace : ",StrErr() ) ;
	//
	size_t   uphill_lvl = 0 ;
	::string highest_s  ;
	for( ::string const& d_s : src_dirs_s ) if (!is_abs_s(d_s))
		if ( size_t ul=uphill_lvl_s(d_s) ; ul>uphill_lvl ) {
			uphill_lvl = ul  ;
			highest_s  = d_s ;
		}
	//
	::string phy_super_repo_root_s ;                                                  // dir englobing all relative source dirs
	::string super_repo_view_s     ;                                                  // .
	::string top_repo_view_s       ;
	if (+repo_view_s) {
		if (!( repo_view_s.ends_with(cwd_s) && repo_view_s.size()>cwd_s.size()+1 ))   // ensure repo_view_s has at least one more level than cwd_s
			throw cat(
				"cannot map local repository dir to ",no_slash(repo_view_s)," appearing as ",no_slash(cwd_s)," in top-level repository, "
			,	"consider setting <rule>.repo_view=",mk_py_str("/repo/"+no_slash(cwd_s))
			) ;
		phy_super_repo_root_s = phy_repo_root_s ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) phy_super_repo_root_s = dir_name_s(phy_super_repo_root_s) ;
		super_repo_view_s     = repo_view_s     ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) super_repo_view_s     = dir_name_s(super_repo_view_s    ) ;
		SWEAR( phy_super_repo_root_s!="/" , phy_repo_root_s,uphill_lvl ) ;            // this should have been checked earlier
		if (!super_repo_view_s)
			throw cat(
				"cannot map repository dir to ",no_slash(repo_view_s)," with relative source dir ",no_slash(highest_s)
			,	", "
			,	"consider setting <rule>.repo_view=",mk_py_str("/repo/"+no_slash(phy_repo_root_s.substr(phy_super_repo_root_s.size())+cwd_s))
			) ;
		if (substr_view(repo_view_s,super_repo_view_s.size())!=substr_view(phy_repo_root_s,phy_super_repo_root_s.size()))
			throw cat(
				"last ",uphill_lvl," components do not match between physical root dir and root view"
			,	", "
			,	"consider setting <rule>.repo_view=",mk_py_str(cat("/repo/",no_slash(phy_repo_root_s.substr(phy_super_repo_root_s.size())+cwd_s)))
			) ;
		top_repo_view_s = repo_view_s.substr(0,repo_view_s.size()-cwd_s.size()) ;
	}
	// XXX! : handle cases where dir is not top level
	if ( +lmake_view_s      && lmake_view_s     .rfind('/',lmake_view_s     .size()-2)!=0 ) throw cat("non top-level lmake_view ",no_slash(lmake_view_s     )," not yet implemented") ;
	if ( +super_repo_view_s && super_repo_view_s.rfind('/',super_repo_view_s.size()-2)!=0 ) throw cat("non top-level repo_view " ,no_slash(super_repo_view_s)," not yet implemented") ;
	if ( +tmp_view_s        && tmp_view_s       .rfind('/',tmp_view_s       .size()-2)!=0 ) throw cat("non top-level tmp_view "  ,no_slash(tmp_view_s       )," not yet implemented") ;
	//
	::string chroot_dir        = chroot_dir_s                                                  ; if (+chroot_dir) chroot_dir.pop_back() ; // cannot use no_slash to properly manage the '/' case
	bool     must_create_lmake = +lmake_view_s      && !is_dir_s(chroot_dir+lmake_view_s     ) ;
	bool     must_create_repo  = +super_repo_view_s && !is_dir_s(chroot_dir+super_repo_view_s) ;
	bool     must_create_tmp   = +tmp_view_s        && !is_dir_s(chroot_dir+tmp_view_s       ) ;
	//
	if (must_create_tmp) SWEAR(+phy_tmp_dir_s) ;
	trace("create",STR(must_create_lmake),STR(must_create_repo),STR(must_create_tmp)) ;
	//
	if ( must_create_repo || must_create_tmp || +views )
		try { unlnk_inside_s(work_dir_s) ; } catch (::string const&) {} // if we need a work dir, we must clean it first as it is not cleaned upon exit (ignore errors as dir may not exist)
	if ( must_create_lmake || must_create_repo || must_create_tmp ) {   // we cannot mount directly in chroot_dir
		if (!work_dir_s)
			throw cat(                                                  // START_OF_NO_COV defensive programming
				"need a work dir to"
			,		must_create_lmake ? " create lmake view"
				:	must_create_repo  ? " create repo view"
				:	must_create_tmp   ? " create tmp view"
				:	                    " ???"
			) ;                                                         // END_OF_NO_COV
		::vector_s top_lvls    = lst_dir_s(chroot_dir_s|"/") ;
		::string   work_root   = work_dir_s+"root"           ;
		::string   work_root_s = work_root+'/'               ;
		mk_dir_empty_s(work_root_s) ;
		trace("top_lvls",work_root_s,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = (chroot_dir_s|"/"s) + f ;
			::string private_f = work_root_s         + f ;
			switch (FileInfo(src_f).tag()) {                                                                                                                           // exclude weird files
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : AcFd    (           private_f,{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.err_ok=true}) ; _mount_bind(private_f,src_f) ; break ; // create file
				case FileTag::Dir   : mk_dir_s(with_slash(private_f)                                                        ) ; _mount_bind(private_f,src_f) ; break ; // create dir
				case FileTag::Lnk   : sym_lnk (           private_f,read_lnk(src_f)                                         ) ;                                break ; // copy symlink
			DN}
		}
		if (must_create_lmake) mk_dir_s(work_root+lmake_view_s     ) ;
		if (must_create_repo ) mk_dir_s(work_root+super_repo_view_s) ;
		if (must_create_tmp  ) mk_dir_s(work_root+tmp_view_s       ) ;
		chroot_dir = ::move(work_root) ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                  ) ;                                                       // necessary to be allowed to write the gid_map (if desirable)
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
	for( auto const& [view,descr] : views ) {
		throw_unless( +view , "cannot map a view to the entire repo" ) ;
		if (!descr) continue ;                                                                                                // empty descr does not represent a view
		//
		::string   abs_view = mk_glb(view,top_repo_root_s) ;
		::vector_s abs_phys ;                                abs_phys.reserve(descr.phys.size()) ; for( ::string const& phy : descr.phys ) abs_phys.push_back(mk_glb_s(phy,top_repo_root_s)) ;
		//
		/**/                                    _create(report,view) ;
		for( ::string const& phy : descr.phys ) _create(report,phy ) ;
		if (is_dir_name(view))
			for( ::string const& cu : descr.copy_up ) {
				::string dst = descr.phys[0]+cu ;
				if (is_dir_name(cu))
					_create(report,dst) ;
				else
					for( size_t i : iota(1,descr.phys.size()) )
						if (_create(report,dst,descr.phys[i]+cu)) break ;
			}
		if (+_tmp_dir_s) swear_prod( !abs_view.starts_with(_tmp_dir_s) , _tmp_dir_s,abs_view ) ;                              // ensure we do not clean up mounted dir upon exit
		if (descr.phys.size()==1) {
			_mount_bind( abs_view , abs_phys[0] ) ;
		} else {
			::string const& upper  = descr.phys[0]                                                                          ;
			::string        work_s = is_lcl(upper) ? cat(work_dir_s,"work_",work_idx++,'/') : cat(no_slash(upper),".work/") ; // if not in the repo, it must be in tmp
			mk_dir_s(work_s) ;
			_mount_overlay( abs_view , abs_phys , mk_glb_s(work_s,top_repo_root_s) ) ;
		}
	}
	trace("done",report,top_repo_root_s) ;
	return true/*entered*/ ;
}

void JobSpace::exit() {
	if (+_tmp_dir_s)
		try { unlnk(_tmp_dir_s,{.dir_ok=true,.abs_ok=true}) ; } catch (::string const&) {} // best effort
}

// XXX! : implement recursive views
// for now, phys cannot englobe or lie within a view, but when it is to be implemented, it is here
::vmap_s<::vector_s> JobSpace::flat_phys() const {
	::vmap_s<::vector_s> res ; res.reserve(views.size()) ;
	for( auto const& [view,descr] : views ) res.emplace_back(view,descr.phys) ;
	return res ;
}

void JobSpace::mk_canon(::string const& phy_repo_root_s) {
	auto do_top = [&]( ::string& dir_s , bool slash_ok , ::string const& key ) {
		if ( !dir_s                                       ) return ;
		if ( !is_canon(dir_s)                             ) dir_s = Disk::mk_canon(dir_s) ;
		if ( slash_ok && dir_s=="/"                       ) return ;
		if (             dir_s=="/"                       ) throw cat(key," cannot be /"                                          ) ;
		if ( !is_abs_s(dir_s)                             ) throw cat(key," must be absolute : ",no_slash(dir_s)                  ) ;
		if ( phy_repo_root_s.starts_with(dir_s          ) ) throw cat("repository cannot lie within ",key,' ',no_slash(dir_s)     ) ;
		if ( dir_s          .starts_with(phy_repo_root_s) ) throw cat(key,' ',no_slash(dir_s)," cannot be local to the repository") ;
	} ;
	//                   slash_ok
	do_top( chroot_dir_s , true  , "chroot dir" ) ;
	do_top( lmake_view_s , false , "lmake view" ) ;
	do_top( repo_view_s  , false , "repo view"  ) ;
	do_top( tmp_view_s   , false , "tmp view"   ) ;
	if ( +lmake_view_s && +repo_view_s ) {
		if (lmake_view_s.starts_with(repo_view_s )) throw cat("lmake view ",no_slash(lmake_view_s)," cannot lie within repo view " ,no_slash(repo_view_s )) ;
 		if (repo_view_s .starts_with(lmake_view_s)) throw cat("repo view " ,no_slash(repo_view_s )," cannot lie within lmake view ",no_slash(lmake_view_s)) ;
	}
	if ( +lmake_view_s && +tmp_view_s ) {
		if (lmake_view_s.starts_with(tmp_view_s  )) throw cat("lmake view ",no_slash(lmake_view_s)," cannot lie within tmp view "  ,no_slash(tmp_view_s  )) ;
		if (tmp_view_s  .starts_with(lmake_view_s)) throw cat("tmp view "  ,no_slash(tmp_view_s  )," cannot lie within lmake view ",no_slash(lmake_view_s)) ;
	}
	if ( +repo_view_s && +tmp_view_s ) {
 		if (repo_view_s .starts_with(tmp_view_s  )) throw cat("repo view " ,no_slash(repo_view_s )," cannot lie within tmp view "  ,no_slash(tmp_view_s  )) ;
		if (tmp_view_s  .starts_with(repo_view_s )) throw cat("tmp view "  ,no_slash(tmp_view_s  )," cannot lie within repo view " ,no_slash(repo_view_s )) ;
	}
	//
	::string const& job_repo_root_s = repo_view_s | phy_repo_root_s ;
	auto do_path = [&](::string& path) {
		if      (!is_canon(path)                  ) path = Disk::mk_canon(path)         ;
		if      (path.starts_with("../")          ) path = mk_glb(path,job_repo_root_s) ;
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
		/**/                             if ( !is_dir_view && descr.phys.size()!=1                                     ) throw cat("cannot map non-dir ",no_slash(view)," to an overlay") ;
		for( auto const& [v,_] : views ) if ( &v!=&view && view.starts_with(v) && (v.back()=='/'||view[v.size()]=='/') ) throw cat("cannot map "        ,no_slash(view)," within ",v    ) ;
		bool lcl_view = _is_lcl_tmp(view) ;
		for( ::string& phy : descr.phys ) {
			do_path(phy) ;
			if ( !lcl_view && _is_lcl_tmp(phy)     ) throw cat("cannot map external view ",no_slash(view)," to local or tmp ",no_slash(phy)) ;
			if (  is_dir_view && !is_dir_name(phy) ) throw cat("cannot map dir "          ,no_slash(view)," to file "        ,no_slash(phy)) ;
			if ( !is_dir_view &&  is_dir_name(phy) ) throw cat("cannot map file "         ,no_slash(view)," to dir "         ,no_slash(phy)) ;
			if (+phy) {
				for( auto const& [v,_] : views ) {                                                                                 // XXX! : suppress this check when recursive maps are implemented
					if ( phy.starts_with(v  ) && (v  .back()=='/'||phy[v  .size()]=='/') ) throw cat("cannot map ",no_slash(view)," to ",no_slash(phy)," within "    ,no_slash(v)) ;
					if ( v  .starts_with(phy) && (phy.back()=='/'||v  [phy.size()]=='/') ) throw cat("cannot map ",no_slash(view)," to ",no_slash(phy)," containing ",no_slash(v)) ;
				}
			} else {
				for( auto const& [v,_] : views )                                                                                   // XXX! : suppress this check when recursive maps are implemented
					if (!is_abs(v)) throw cat("cannot map ",no_slash(view)," to full repository with ",no_slash(v)," being map") ;
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

void JobStartRpcReq::cache_cleanup() {
	JobRpcReq::cache_cleanup() ;
	port = 0 ;                   // execution dependent
}

void JobStartRpcReq::chk(bool for_cache) const {
	JobRpcReq::chk(for_cache) ;
	if (for_cache) {
		throw_unless( !port , "bad port" ) ;
	}
}

//
// JobDigest
//

template<> void JobDigest<>::cache_cleanup() {
	upload_key     = {} ;                      // no recursive info
	cache_idx      = 0  ;                      // .
	refresh_codecs = {} ;                      // execution dependent
	for( auto& [_,td] : targets ) {
		SWEAR(!td.pre_exist) ;                 // else cannot be a candidate for upload
		td.sig = td.sig.tag() ;                // forget date, just keep tag
	}
	for( auto& [_,dd] : deps )
		dd.hot = false ;                       // execution dependent
}

//
// JobEndRpcReq
//

::string& operator+=( ::string& os , ExecTraceEntry const& ete ) {                       // START_OF_NO_COV
	return os <<"ExecTraceEntry("<< ete.date <<','<< ete.step() <<','<< ete.file <<')' ;
}                                                                                        // END_OF_NO_COV

::string& operator+=( ::string& os , TargetDigest const& td ) {  // START_OF_NO_COV
	First first ;
	/**/                  os <<"TargetDigest("                 ;
	if ( td.pre_exist   ) os <<first("",",")<< "pre_exist"     ;
	if ( td.written     ) os <<first("",",")<< "written"       ;
	if (+td.tflags      ) os <<first("",",")<< td.tflags       ;
	if (+td.extra_tflags) os <<first("",",")<< td.extra_tflags ;
	if (+td.crc         ) os <<first("",",")<< td.crc          ;
	if (+td.sig         ) os <<first("",",")<< td.sig          ;
	return                os <<')'                             ;
}                                                                // END_OF_NO_COV

::string& operator+=( ::string& os , JobEndRpcReq const& jerr ) {                                                                            // START_OF_NO_COV
	return os << "JobEndRpcReq(" << jerr.seq_id <<','<< jerr.job <<','<< jerr.digest <<','<< jerr.phy_tmp_dir_s <<','<< jerr.dyn_env <<')' ;
}                                                                                                                                            // END_OF_NO_COV

void JobEndRpcReq::cache_cleanup() {
	JobRpcReq::cache_cleanup() ;
	digest.cache_cleanup() ;
	phy_tmp_dir_s = {} ;     // execution dependent
}

void JobEndRpcReq::chk(bool for_cache) const {
	JobRpcReq::chk(for_cache) ;
	digest.    chk(for_cache) ;
	/**/             throw_unless( !phy_tmp_dir_s || (phy_tmp_dir_s.front()=='/'&&phy_tmp_dir_s.back()=='/'&&is_canon(phy_tmp_dir_s)) , "bad phy_tmp_dir"       ) ;
	/**/             throw_unless( end_date<=New                                                                                      , "bad end_date"          ) ;
	if (+msg_stderr) throw_unless( digest.has_msg_stderr                                                                              , "incoherent msg/stderr" ) ;
}

//
// JobStartRpcReply
//

::string& operator+=( ::string& os , JobStartRpcReply const& jsrr ) {         // START_OF_NO_COV
	/**/                           os << "JobStartRpcReply("<<jsrr.rule     ;
	/**/                           os <<','  << to_hex(jsrr.addr)           ;
	/**/                           os <<','  << jsrr.autodep_env            ;
	if (+jsrr.job_space          ) os <<','  << jsrr.job_space              ;
	if ( jsrr.keep_tmp           ) os <<','  << "keep"                      ;
	if (+jsrr.ddate_prec         ) os <<','  << jsrr.ddate_prec             ;
	/**/                           os <<','  << mk_printable(cat(jsrr.env)) ; // env may contain the non-printable PassMrkr value
	/**/                           os <<','  << jsrr.interpreter            ;
	/**/                           os <<','  << jsrr.kill_sigs              ;
	if (jsrr.live_out            ) os <<','  << "live_out"                  ;
	if (jsrr.nice                ) os <<','  << "nice:"<<jsrr.nice          ;
	if (jsrr.stderr_ok           ) os <<','  << "stderr_ok"                 ;
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
		if      (v!=PassMrkr)                                                         cmd_env[k] = ::move(v ) ;
		else if (has_env(k) ) { ::string ev=get_env(k) ; dyn_env.emplace_back(k,ev) ; cmd_env[k] = ::move(ev) ; } // if special illegal value, use value from environment (typically from slurm)
	//
	autodep_env.repo_root_s = job_space.repo_view_s | phy_repo_root_s ;
	autodep_env.tmp_dir_s   = job_space.tmp_view_s  | phy_tmp_dir_s   ;
	if (+phy_tmp_dir_s) {
		_tmp_dir_s = autodep_env.tmp_dir_s ;                                             // for use in exit (autodep.tmp_dir_s may be moved)
		try                       { mk_dir_empty_s( phy_tmp_dir_s , {.abs_ok=true} ) ; }
		catch (::string const& e) { throw "cannot create tmp dir : "+e ;               }
	} else {
		if (+job_space.tmp_view_s) throw cat("cannot map tmp dir ",job_space.tmp_view_s," to nowhere") ;
	}
	//
	::string phy_work_dir_s = cat(PrivateAdminDirS,"work/",small_id,'/')                                                                                                                ;
	bool entered = job_space.enter(
		/*out*/actions
	,	/*out*/top_repo_root_s
	,	       phy_lmake_root_s
	,	       phy_repo_root_s
	,	       phy_tmp_dir_s          , keep_tmp
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
		static constexpr uint32_t FirstPid = 300                                 ;       // apparently, pid's wrap around back to 300
		static constexpr uint64_t NPids    = MAX_PID - FirstPid                  ;       // number of available pid's
		static constexpr uint64_t DeltaPid = (1640531527*NPids) >> n_bits(NPids) ;       // use golden number to ensure best spacing (see above), 1640531527 = (2-(1+sqrt(5))/2)<<32
		first_pid = FirstPid + ((small_id*DeltaPid)>>(32-n_bits(NPids)))%NPids ;         // DeltaPid on 64 bits to avoid rare overflow in multiplication
	}
	trace("done",actions,cmd_env,dyn_env,first_pid,top_repo_root_s) ;
	return entered ;
}

void JobStartRpcReply::exit() {
	job_space.exit() ;
}

void JobStartRpcReply::cache_cleanup() {
	autodep_env.fast_report_pipe = {}      ; // .
	cache                        = nullptr ; // no recursive info
	cache_idx                    = {}      ; // .
	key                          = {}      ; // .
	live_out                     = false   ; // execution dependent
	nice                         = -1      ; // .
	pre_actions                  = {}      ; // .
}

void JobStartRpcReply::chk(bool for_cache) const {
	autodep_env.chk(for_cache) ;
	job_space  .chk(         ) ;
	/**/                                      throw_unless( method<All<AutodepMethod>      , "bad autoded_method" ) ;
	/**/                                      throw_unless( network_delay>=Delay()         , "bad networkd_delay" ) ;
	for( auto const& [f,_] : pre_actions    ) throw_unless( is_canon(f)                    , "bad file_action"    ) ;
	for( auto const& [t,_] : static_matches ) throw_unless( is_canon(t)                    , "bad target"         ) ;
	//                                                                     ext_ok empty_ok
	/**/                                      throw_unless( is_canon(stdin ,true ,true   ) , "bad stdin"          ) ;
	/**/                                      throw_unless( is_canon(stdout,false,true   ) , "bad stdout"         ) ;
	/**/                                      throw_unless( timeout>=Delay()               , "bad timeout"        ) ;
	if (for_cache) {
		throw_unless( !cache             , "bad cache"       ) ;
		throw_unless( !cache_idx         , "bad cache_idx"   ) ;
		throw_unless( !key               , "bad key"         ) ;
		throw_unless( !live_out          , "bad live_out"    ) ;
		throw_unless(  nice==uint8_t(-1) , "bad nice"        ) ;
		throw_unless( !pre_actions       , "bad pre_actions" ) ;
	}
}

//
// JobMngtRpcReq
//

::string& operator+=( ::string& os , JobMngtRpcReq const& jmrr ) {                                                // START_OF_NO_COV
	/**/               os << "JobMngtRpcReq(" << jmrr.proc <<','<< jmrr.seq_id <<','<< jmrr.job <<','<< jmrr.fd ;
	if (+jmrr.fd     ) os <<','<< jmrr.fd                                                                       ;
	if (+jmrr.targets) os <<','<< jmrr.targets                                                                  ;
	if (+jmrr.deps   ) os <<','<< jmrr.deps                                                                     ;
	if (+jmrr.txt    ) os <<','<< jmrr.txt                                                                      ;
	return             os <<')'                                                                                 ;
}                                                                                                                 // END_OF_NO_COV

//
// JobMngtRpcReply
//

::string& operator+=( ::string& os , JobMngtRpcReply const& jmrr ) {                     // START_OF_NO_COV
	/**/                     os << "JobMngtRpcReply(" << jmrr.proc <<','<< jmrr.seq_id ;
	if (+jmrr.fd           ) os <<','<< jmrr.fd                                        ;
	if (+jmrr.verbose_infos) os <<','<< jmrr.verbose_infos                             ;
	if (+jmrr.txt          ) os <<','<< jmrr.txt                                       ;
	/**/                     os <<','<< jmrr.ok                                        ;
	return                   os <<')'                                                  ;
}                                                                                        // END_OF_NO_COV

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

void SubmitAttrs::cache_cleanup() {
	reason    = {}    ;             // execution dependent
	pressure  = {}    ;             // .
	cache_idx = {}    ;             // no recursive info
	live_out  = false ;             // execution dependent
	nice      = -1    ;             // .
}

void SubmitAttrs::chk(bool for_cache) const {
	throw_unless(  used_backend<All<BackendTag> , "bad backend tag" ) ;
	if (for_cache) {
		throw_unless( !reason            , "bad reason"    ) ;
		throw_unless( !pressure          , "bad pressure"  ) ;
		throw_unless( !cache_idx         , "bad cache_idx" ) ;
		throw_unless( !live_out          , "bad live_out"  ) ;
		throw_unless(  nice==uint8_t(-1) , "bad nice"      ) ;
	} else {
		reason.chk() ;
	}
}

//
// JobInfoStart
//

::string& operator+=( ::string& os , JobInfoStart const& jis ) {                                                       // START_OF_NO_COV
	return os << "JobInfoStart(" << jis.submit_attrs <<','<< jis.rsrcs <<','<< jis.pre_start <<','<< jis.start <<')' ;
}                                                                                                                      // END_OF_NO_COV

void JobInfoStart::cache_cleanup() {
	submit_attrs.cache_cleanup() ;
	pre_start   .cache_cleanup() ;
	start       .cache_cleanup() ;
	eta = {} ;                     // execution dependent
}

void JobInfoStart::chk(bool for_cache) const {
	submit_attrs.chk(for_cache) ;
	pre_start   .chk(for_cache) ;
	start       .chk(for_cache) ;
	if (for_cache) {
		throw_unless( !eta , "bad eta" ) ;
	}
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
		::string_view jis      = job_info               ;
		deserialize( jis , need[JobInfoKind::Start] ? start : ::ref(JobInfoStart()) ) ; need &= ~JobInfoKind::Start ; if (!need) return ; // skip if not needed
		deserialize( jis , need[JobInfoKind::End  ] ? end   : ::ref(JobEndRpcReq()) ) ; need &= ~JobInfoKind::End   ; if (!need) return ; // .
		deserialize( jis , dep_crcs                                                 ) ;
	} catch (...) {}                                                                                                                      // fill what we have
}

void JobInfo::update_digest() {
	Trace trace("update_digest",dep_crcs.size()) ;
	if (!dep_crcs) return ;                                                                                                                               // nothing to update
	SWEAR( dep_crcs.size()==end.digest.deps.size() , dep_crcs.size(),end.digest.deps.size() ) ;
	for( size_t i : iota(end.digest.deps.size()) )
		if ( dep_crcs[i].first.valid() || !end.digest.deps[i].second.accesses ) end.digest.deps[i].second.set_crc(dep_crcs[i].first,dep_crcs[i].second) ;
	dep_crcs.clear() ;                                                                                                                                    // now useless as info is recorded in digest
}

void JobInfo::cache_cleanup() {
	start.cache_cleanup() ;
	end  .cache_cleanup() ;
}

void JobInfo::chk(bool for_cache) const {
	start.chk(for_cache) ;
	end  .chk(for_cache) ;
	for( size_t i : iota(start.submit_attrs.deps.size()) ) throw_unless( start.submit_attrs.deps[i].first==end.digest.deps[i].first , "incoherent deps" ) ;
	if (for_cache) {
		throw_unless( !dep_crcs , "bad dep_crcs" ) ;
	} else {
		throw_unless( !dep_crcs || dep_crcs.size()==end.digest.deps.size() , "incoherent deps" ) ;
	}
}
