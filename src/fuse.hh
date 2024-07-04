// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/mount.h>

#include "utils.hh" // defines HAS_FUSE

#if HAS_FUSE

	#define FUSE_USE_VERSION 31
	#include <fuse.h>

#endif

#include "thread.hh"

namespace Fuse {

	#if !HAS_FUSE

		struct Mount {
			// statics
			Mount() = default ;
			Mount( ::string const& /*dst_s*/ , ::string const& /*src_s*/ ) { FAIL() ; }
			// services
			void open() { FAIL() ; }
			// data
			::string dst ;
			::string src ;
		} ;

	#else

		struct Mount {
			// statics
		private :
			static void _s_loop( ::stop_token stop , Mount* self ) {
				t_thread_key = 'L' ;
				self->_loop(stop) ;
			}
			// cxtors & casts
		public :
			Mount() = default ;
			//
			Mount( ::string const& dst_s_ , ::string const& src_s_ ) : dst_s{dst_s_} , src_s{src_s_} {
::cerr<<t_thread_key<<" "<<"Mount::Mount1 :"<<dst_s<<":"<<endl;
				open() ;
::cerr<<t_thread_key<<" "<<"Mount::Mount2 :"<<src_s<<":"<<endl;
				_thread = ::jthread( _s_loop , this ) ;
			}
			~Mount() { ::cerr<<"~Mount "<<::umount(no_slash(dst_s).c_str())<<endl ; }
			// services
			void open() ;
		private :
			void _loop(::stop_token) ;
			// data
		public :
			::string dst_s ;
			::string src_s ;
		private :
			::jthread    _thread ;
			struct fuse* _fuse   = nullptr ;
		} ;

	#endif

}
