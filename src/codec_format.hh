// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "hash.hh"

namespace Codec {

	//
	// for use without server
	// declared inline so as to be include-only lib
	//

	// START_OF_VERSIONING
	inline ::string _mk_codec_line( ::string const& ctx , ::string const& code , ::string const& val ) {
		return cat(                                                                                      // format : " <ctx> <code> <val>" exactly
			' ',mk_printable<' '>(code)
		,	' ',mk_printable<' '>(ctx )
		,	' ',mk_printable     (val )
		) ;
	}
	// END_OF_VERSIONING

	inline bool/*ok*/ _parse_codec_line( ::string&/*out*/ ctx , ::string&/*out*/ code , ::string&/*out*/ val , ::string const& line ) {
		// /!\ format must stay in sync with Record::report_sync_direct
		size_t pos = 0 ; //!                                                 ok
		/**/                                    if (line[pos++]!=' ') return false ;
		code = parse_printable<' '>(line,pos) ; if (line[pos++]!=' ') return false ;
		ctx  = parse_printable<' '>(line,pos) ; if (line[pos++]!=' ') return false ;
		val  = parse_printable     (line,pos) ; if (line[pos  ]!=0  ) return false ;
		return true/*ok*/ ;
	}

	inline ::string decode( ::string const& file , ::string const& ctx , ::string const& code ) {
		LockedFd lock { file , false/*exclusive*/ } ;                                             // in case lmake and lencode/ldecode outside lmake run simultaneously
		for( ::string const& line : AcFd(file,true/*err_ok*/).read_lines() ) {
			::string ctx_  ;
			::string code_ ;
			::string val   ;
			if ( !_parse_codec_line( /*out*/ctx_ , /*out*/code_ , /*out*/val , line ) ) continue   ;
			if ( ctx_==ctx && code_==code                                             ) return val ;
		}
		throw cat("cannot decode with file=",file," context=",ctx," code=",code) ;
	}

	inline ::string encode( ::string const& file , ::string const& ctx , ::string const& val ) {
		LockedFd lock { file , false/*exclusive*/ } ;                                            // in case lmake and lencode/ldecode outside lmake run simultaneously
		for( ::string const& line : AcFd(file,true/*err_ok*/).read_lines() ) {
			::string ctx_ ;
			::string code ;
			::string val_ ;
			if ( !_parse_codec_line( /*out*/ctx_ , /*out*/code , /*out*/val_ , line ) ) continue    ;
			if ( ctx_==ctx && val_==val                                               ) return code ;
		}
		return Hash::Crc(New,val).hex() ;                                                        // code not found, generate a code that will not clash
	}

}
