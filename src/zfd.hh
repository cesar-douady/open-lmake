// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "fd.hh"

#if HAS_ZSTD
	#include <zstd.h>
#endif
#if HAS_ZLIB
	#define ZLIB_CONST
	#include <zlib.h>
#endif

// START_OF_VERSIONING REPO CACHE
enum class ZlvlTag : uint8_t {
	None
,	Zlib
,	Zstd
// aliases
,	Dflt =
		#if HAS_STD
			Zstd
		#elif HAS_ZLIB
			Zlib
		#else
			None
		#endif
} ;
// END_OF_VERSIONING

struct Zlvl {
	friend ::string& operator+=( ::string& , Zlvl ) ;
	bool operator+() const { return +tag && lvl ; }
	ZlvlTag tag = {} ;
	uint8_t lvl = 0  ;
} ;

struct DeflateFd : AcFd {
	static Disk::DiskSz s_max_sz( Disk::DiskSz sz , Zlvl zlvl={} ) ;
	// cxtors & casts
	DeflateFd() = default ;
	DeflateFd( AcFd&& , Zlvl={} ) ;
	~DeflateFd() ;
	// services
	void write(::string const&) ;
	void send_from( Fd , size_t ) ;
	void flush() ;
private :
	bool/*room_ok*/ _flush(size_t room=Disk::DiskBufSz) ; // flush if not enough room
	// data
public :
	Disk::DiskSz z_sz = 0 ;                               // total compressed size
	Zlvl         zlvl ;
private :
	char   _buf[Disk::DiskBufSz] ;
	size_t _pos                  = 0     ;
	bool   _flushed              = false ;
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
	InflateFd( AcFd&& , Zlvl={} ) ;
	~InflateFd() ;
	// services
	::string read(size_t) ;
	void receive_to( Fd , size_t ) ;
	// data
	Zlvl zlvl ;
private :
	char   _buf[Disk::DiskBufSz] ;
	size_t _pos                  = 0 ;
	size_t _len                  = 0 ;
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

