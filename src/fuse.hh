// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// XXX : fuse autodep method is under construction

#pragma once

#include <sys/mount.h>

#include "utils.hh" // defines HAS_FUSE

#if HAS_FUSE

	#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3,12)
	#include <fuse_lowlevel.h>

#endif

#include "disk.hh"
#include "fd.hh"
#include "thread.hh"

namespace Fuse {

	#if !HAS_FUSE

		struct Mount {
			// statics
			Mount() = default ;
			Mount( ::string const& /*dst_s*/ , ::string const& /*src_s*/ ) { FAIL() ; }
			// services
			void open () { FAIL() ; }
			void close() { FAIL() ; }
			// data
			::string dst ;
			::string src ;
		} ;

	#else

		struct Mount {
			using RefCnt = Uint<NBits<Fd>> ;
			struct FdEntry {
				AutoCloseFd fd      ;
				RefCnt      ref_cnt ;
			} ;
			struct FdTab : private ::umap<fuse_ino_t,FdEntry> {
				using Base = ::umap<fuse_ino_t,FdEntry> ;
				using Base::begin ;
				using Base::end   ;
				using Base::clear ;
				// services
				Fd at(fuse_ino_t ino) {
					if (ino==FUSE_ROOT_ID) return root ;
					auto it = find(ino) ;
					if (it==end()) return {}            ;
					else           return it->second.fd ;
				}
				::pair<AutoCloseFd&,bool/*inserted*/> inc_ref(fuse_ino_t ino) {
					if (ino==FUSE_ROOT_ID) return { root , false/*inserted*/ } ;
					//
					auto it_inserted = try_emplace( ino , Fd() , 1 ) ;
					if (!it_inserted.second) it_inserted.first->second.ref_cnt ++ ;
					return { it_inserted.first->second.fd , it_inserted.second } ;
				}
				void dec_ref( fuse_ino_t ino , RefCnt n=1 ) {
					SWEAR(n) ;
					if (ino==FUSE_ROOT_ID) return ;
					//
					auto it = find(ino) ;
					SWEAR( it->second.ref_cnt>=n , ino , it->second.ref_cnt , n ) ;
					//
					if (it->second.ref_cnt>n) it->second.ref_cnt -= n ;
					else                      Base::erase(it) ;
				}
				// data
				AutoCloseFd root ;
			} ;
			// statics
		private :
			static void _s_loop( ::stop_token stop , Mount* self ) { self->_loop(stop) ; }
			// cxtors & casts
		public :
			Mount() = default ;
			//
			Mount ( ::string const& dst_s_ , ::string const& src_s_ ) : dst_s{Disk::mk_abs(dst_s_,Disk::cwd_s())} , src_s{src_s_} { open () ; }
			~Mount(                                                                                                             ) { close() ; }
			// services
			void open () ;
			void close() ;
			//
			::fuse_entry_param mk_fuse_entry_param(                  fuse_ino_t parent , const char* name , int flags=O_PATH|O_NOFOLLOW , mode_t  =0 , Fd* /*out*/=nullptr ) ;
			void               reply_entry        ( fuse_req_t req , fuse_ino_t parent , const char* name , int flags=O_PATH|O_NOFOLLOW , mode_t m=0                       ) {
				::fuse_entry_param res  = mk_fuse_entry_param( parent , name , flags , m ) ;
				::fuse_reply_entry( req , &res ) ;
			}
		private :
			void _loop(::stop_token) ;
			// data
		public :
			::string dst_s ;
			::string src_s ;
			FdTab    fds   ;
		private :
			::jthread            _thread ;
			struct fuse_session* _fuse   = nullptr ;
		} ;

	#endif

}
