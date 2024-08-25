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

#include "record.hh"

namespace Fuse {

	#if !HAS_FUSE

		struct Mount {
			// statics
			static void  s_autodep_env (AutodepEnv&) { FAIL() ; }
			static void  s_close_report(           ) { FAIL() ; }
			static bool& s_enable      (           ) { FAIL() ; }
			// cxtors & casts
			Mount() = default ;
			Mount( ::string const& /*dst_s*/ , ::string const& /*src_s*/ , ::string const& /*pfx_s*/ , bool /*report_writes*/ ) { FAIL() ; }
			// services
			void open() { FAIL() ; }
			// data
			::string dst ;
			::string src ;
		} ;

	#else

		struct Mount {
			using RefCnt = Uint<NBits<Fd>> ;
			struct FdEntry {
				// access
				bool operator+() { return +fd     ; }
				bool operator!() { return !+*this ; }
				// data
				::string    name    ;
				AutoCloseFd fd      ;
				RefCnt      ref_cnt = 0 ;
			} ;
			struct FdTab : private ::umap<fuse_ino_t,FdEntry> {
				using Base = ::umap<fuse_ino_t,FdEntry> ;
				using Base::begin ;
				using Base::end   ;
				using Base::clear ;
				using Base::find  ;
				// services
				Fd fd(fuse_ino_t ino) const {
					if (ino==FUSE_ROOT_ID) return root.fd ;
					auto it = find(ino) ;
					if (it==end()) return {}            ;
					else           return it->second.fd ;
				}
				FdEntry const& at(fuse_ino_t ino) const {
					if (ino==FUSE_ROOT_ID) return root ;
					return Base::at(ino) ;
				}
				::string proc(fuse_ino_t ino) const {
					return "/proc/self/fd/"s+at(ino).fd.fd ;
				}
				::pair<FdEntry&,bool/*inserted*/> inc_ref(fuse_ino_t ino) {
					if (ino==FUSE_ROOT_ID) return { root , false/*inserted*/ } ;
					//
					auto it_inserted = try_emplace( ino , ""s , Fd() , 1 ) ;
					if (!it_inserted.second) it_inserted.first->second.ref_cnt++ ;
					return { it_inserted.first->second , it_inserted.second } ;
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
				FdEntry root ;
			} ;
			// statics
			static void s_autodep_env(AutodepEnv& ade) {
				Record::s_autodep_env(ade) ;
				s_auditor = Record(New,Yes/*enable*/) ;
			}
			static void s_close_report() {
				Trace trace("s_close_report") ;
				Record::s_close_report() ;
				s_enable() = false ;
			}
			static bool& s_enable() {
				return s_auditor.enable ;
			}
		private :
			static void _s_loop( ::stop_token stop , Mount* self , fuse_session* session ) { self->_loop( stop , session ) ; }
			// static data
		public :
			static Record s_auditor ;
			// cxtors & casts
			Mount(       ) = default ;
			Mount(Mount&&) = default ;
			Mount( ::string const& dst_s_ , ::string const& src_s_ , ::string const& pfx_s_ , bool report_writes_ ) :
				dst_s{Disk::mk_abs(dst_s_,Disk::cwd_s())} , src_s{src_s_} , pfx_s{pfx_s_} , report_writes{report_writes_}
			{ open () ; }
			~Mount() ;
			// services
			void open() ;
			//
			::fuse_entry_param mk_fuse_entry_param(                  fuse_ino_t parent , const char* name ) ;
			void               reply_entry        ( fuse_req_t req , fuse_ino_t parent , const char* name ) {
				::fuse_entry_param res  = mk_fuse_entry_param( parent , name ) ;
				::fuse_reply_entry( req , &res ) ;
			}
			::string report_name( fuse_ino_t ino , const char* name ) const {
				if (ino==FUSE_ROOT_ID) {
					SWEAR( name && *name ) ;
					return name ;
				} else {
					if ( name && *name ) return pfx_s+fds.at(ino).name+'/'+name ;
					else                 return pfx_s+fds.at(ino).name          ;
				}
			}
			//
			void report_access( fuse_ino_t parent , const char* name , Accesses a , bool write , ::string&& comment ) const ;
			void report_dep   ( fuse_ino_t parent , const char* name , Accesses a ,              ::string&& comment ) const { report_access(parent,name,a ,false,::move(comment)) ; }
			void report_dep   ( fuse_ino_t parent ,                    Accesses a ,              ::string&& comment ) const { report_access(parent,""  ,a ,false,::move(comment)) ; }
			void report_target( fuse_ino_t parent , const char* name ,                           ::string&& comment ) const { report_access(parent,name,{},true ,::move(comment)) ; }
			void report_target( fuse_ino_t parent ,                                              ::string&& comment ) const { report_access(parent,""  ,{},true ,::move(comment)) ; }
		private :
			void _loop( ::stop_token , fuse_session* ) ;
			// data
		public :
			::string dst_s         ;
			::string src_s         ;
			::string pfx_s         ;         // prefix used when reporting accesses
			bool     report_writes = false ;
			FdTab    fds           ;
		private :
			::jthread _thread ;              // the server loop
			dev_t     _dev    ;              // used to unmount
		} ;

	#endif

}
