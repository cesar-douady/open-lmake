// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "zfd.hh"

using namespace Disk ;

DiskSz DeflateFd::s_max_sz( DiskSz sz , Zlvl zlvl ) {
	if (!zlvl) return sz ;
	switch (zlvl.tag) {
		case ZlvlTag::Zlib :
			throw_unless( HAS_ZLIB , "cannot compress without zlib" ) ;
			#if HAS_ZLIB
				static_assert(sizeof(ulong)==sizeof(DiskSz)) ;  // compressBound manages ulong and we need a Sz
				return ::compressBound(sz) ;
			#endif
		break ;
		case ZlvlTag::Zstd :
			throw_unless( HAS_ZSTD , "cannot compress without zstd" ) ;
			#if HAS_ZSTD
				static_assert(sizeof(size_t)==sizeof(DiskSz)) ; // ZSTD_compressBound manages size_t and we need a Sz
				return ::ZSTD_compressBound(sz) ;
			#endif
		break ;
	DF}                                                         // NO_COV
	FAIL() ;
}

DeflateFd::DeflateFd( AcFd&& fd , Zlvl zl ) : AcFd{::move(fd)} , zlvl{zl} {
	if (!zlvl) return ;
	switch (zlvl.tag) {
		case ZlvlTag::Zlib : {
			throw_unless( HAS_ZLIB , "cannot compress without zlib" ) ;
			#if HAS_ZLIB
				zlvl.lvl    = ::min( zlvl.lvl , uint8_t(Z_BEST_COMPRESSION) ) ;
				_zlib_state = {}                                              ;
				int rc = ::deflateInit(&_zlib_state,zlvl.lvl) ; SWEAR_PROD(rc==Z_OK) ;
				_zlib_state.next_in  = ::launder(reinterpret_cast<uint8_t const*>(_buf)) ;
				_zlib_state.avail_in = 0                                                 ;
			#endif
		} break ;
		case ZlvlTag::Zstd :
			throw_unless( HAS_ZSTD , "cannot compress without zstd" ) ;
			#if HAS_ZSTD
				zlvl.lvl    = ::min( zlvl.lvl , uint8_t(::ZSTD_maxCLevel()) ) ;
				_zstd_state = ::ZSTD_createCCtx()                             ; SWEAR_PROD(_zstd_state) ;
				::ZSTD_CCtx_setParameter( _zstd_state , ZSTD_c_compressionLevel , zlvl.lvl ) ;
			#endif
		break ;
	DF}         // NO_COV
}

DeflateFd::~DeflateFd() {
	flush() ;
	if (!zlvl) return ;
	switch (zlvl.tag) {
		case ZlvlTag::Zlib :
			#if HAS_ZLIB
				::deflateEnd(&_zlib_state) ;
			#else
				FAIL_PROD() ;
			#endif
		break ;
		case ZlvlTag::Zstd :
			#if HAS_ZSTD
				::ZSTD_freeCCtx(_zstd_state) ;
			#else
				FAIL_PROD() ;
			#endif
		break ;
	DF}         // NO_COV
}

void DeflateFd::write(::string const& s) {
	if (!s) return ;
	SWEAR(!_flushed) ;
	if (+zlvl) {
		switch (zlvl.tag) {
			case ZlvlTag::Zlib :
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
				#else
					FAIL_PROD() ;
				#endif
			break ;
			case ZlvlTag::Zstd : {
				#if HAS_ZSTD
					::ZSTD_inBuffer  in_buf  { .src=s.data() , .size=s.size()  , .pos=0 } ;
					::ZSTD_outBuffer out_buf { .dst=_buf     , .size=DiskBufSz , .pos=0 } ;
					while (in_buf.pos<in_buf.size) {
						out_buf.pos = _pos ;
						::ZSTD_compressStream2( _zstd_state , &out_buf , &in_buf , ZSTD_e_continue ) ;
						_pos = out_buf.pos ;
						_flush(1/*room*/) ;
					}
				#else
					FAIL_PROD() ;
				#endif
			} break ;
		DF}                                                                                                                                                        // NO_COV
	} else {                                                                                                                                                       // no compression
		if (_flush(s.size())) {                ::memcpy( _buf+_pos , s.data() , s.size() ) ; _pos += s.size() ; }                                                  // small data : put in _buf
		else                  { SWEAR(!_pos) ; AcFd::write(s)                              ; z_sz += s.size() ; }                                                  // large data : send directly
	}
}
void DeflateFd::send_from( Fd fd_ , size_t sz ) {
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
		if (_flush(sz)) {                size_t c=fd_.read_to({_buf+_pos,sz})               ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; _pos+=c  ; } // small : put in _buf
		else            { SWEAR(!_pos) ; size_t c=::sendfile(self,fd_,nullptr/*offset*/,sz) ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; z_sz+=sz ; } // large : send directly
	}
}

void DeflateFd::flush() {
	if (_flushed) return ;
	_flushed = true ;
	if (+zlvl) {
		switch (zlvl.tag) {
			case ZlvlTag::Zlib :
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
				#else
					FAIL_PROD() ;
				#endif
			break ;
			case ZlvlTag::Zstd : {
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
				#else
					FAIL_PROD() ;
				#endif
			} break ;
		DF}           // NO_COV
	}
	_flush() ;
}

bool/*room_ok*/ DeflateFd::_flush(size_t room) {
	if (_pos+room<=DiskBufSz) return true/*room_ok*/ ; // enough room
	if (_pos) {                                        // if returning false (not enugh room), at least ensure nothing is left in buffer so that direct write is possible
		AcFd::write({_buf,_pos}) ;
		z_sz += _pos ;
		_pos  = 0    ;
	}
	return room<=DiskBufSz ;
}

InflateFd::InflateFd( AcFd&& fd , Zlvl zl ) : AcFd{::move(fd)} , zlvl{zl} {
	if (!zlvl) return ;
	switch (zlvl.tag) {
		case ZlvlTag::Zlib : {
			#if HAS_ZLIB
				int rc = ::inflateInit(&_zlib_state) ; SWEAR_PROD(rc==Z_OK,self) ;
			#else
				throw "cannot compress without zlib"s ;
			#endif
		} break ;
		case ZlvlTag::Zstd :
			#if HAS_ZSTD
				_zstd_state = ::ZSTD_createDCtx() ; SWEAR_PROD(_zstd_state,self) ;
			#else
				throw "cannot compress without zstd"s ;
			#endif
		break ;
	DF}         // NO_COV
}

InflateFd::~InflateFd() {
	if (!zlvl) return ;
	switch (zlvl.tag) {
		case ZlvlTag::Zlib : {
			#if HAS_ZLIB
				int rc = ::inflateEnd(&_zlib_state) ; SWEAR_PROD( rc==Z_OK , rc,self ) ;
			#else
				FAIL_PROD(zlvl) ;
			#endif
		} break ;
		case ZlvlTag::Zstd : {
			#if HAS_ZSTD
				size_t rc = ::ZSTD_freeDCtx(_zstd_state) ; SWEAR_PROD( !::ZSTD_isError(rc) , rc,self ) ;
			#else
				FAIL_PROD(zlvl) ;
			#endif
		} break ;
	DF}           // NO_COV
}

::string InflateFd::read(size_t sz) {
	if (!sz) return {} ;
	::string res ( sz , 0 ) ;
	if (+zlvl)
		switch (zlvl.tag) {
			case ZlvlTag::Zlib :
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
				#else
					FAIL_PROD(zlvl) ;
				#endif
			break ;
			case ZlvlTag::Zstd : {
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
						size_t rc = ::ZSTD_decompressStream( _zstd_state , &out_buf , &in_buf ) ; SWEAR_PROD(!::ZSTD_isError(rc)) ;
						_pos = in_buf.pos               ;
						_len = in_buf.size - in_buf.pos ;
					}
				#else
					FAIL_PROD(zlvl) ;
				#endif
			} break ;
		DF}                                                                                                                // NO_COV
	else {
		size_t cnt = ::min( sz , _len ) ;
		if (cnt) {                                                                                                         // gather available data from _buf
			::memcpy( res.data() , _buf+_pos , cnt ) ;
			_pos += cnt ;
			_len -= cnt ;
			sz   -= cnt ;
		}
		if (sz) {
			SWEAR( !_len , _len ) ;
			if (sz>=DiskBufSz) {                                                                                           // large data : read directly
				size_t c = AcFd::read_to({&res[cnt],sz}) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
			} else {                                                                                                       // small data : bufferize
				_len = AcFd::read_to({_buf,DiskBufSz}) ;   throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
				::memcpy( &res[cnt] , _buf , sz ) ;
				_pos  = sz ;
				_len -= sz ;
			}
		}
	}
	return res ;
}

void InflateFd::receive_to( Fd fd_ , size_t sz ) {
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
