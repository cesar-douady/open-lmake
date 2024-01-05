// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "fd.hh"
#include "thread.hh"

namespace Codec {

	struct CodecClosure {
		friend ::ostream& operator<<( ::ostream& , CodecClosure const& ) ;
		// cxtors & casts
		#define S ::string
		CodecClosure() = default ;
		CodecClosure( bool e , S&& code , S&& f , S&& c              , Fd rfd ) : encode{e} ,               reply_fd{rfd} , txt{::move(code)} , file{::move(f)} , ctx{::move(c)} { SWEAR(!e) ; }
		CodecClosure( bool e , S&& val  , S&& f , S&& c , uint8_t ml , Fd rfd ) : encode{e} , min_len{ml} , reply_fd{rfd} , txt{::move(val )} , file{::move(f)} , ctx{::move(c)} { SWEAR( e) ; }
		#undef S
		// data
		bool     encode  = false/*garbage*/ ;
		uint8_t  min_len = 0    /*garbage*/ ;
		Fd       reply_fd ;
		::string txt      ;
		::string file     ;
		::string ctx      ;
	} ;

	using Code = uint32_t ; //SimpleVector<CodecIdx,char> ;
	using Val  = uint32_t ; //SimpleVector<CodecIdx,char> ;

}

namespace Engine {
	extern QueueThread<Codec::CodecClosure> g_codec_queue ;
}
