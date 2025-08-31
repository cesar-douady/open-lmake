// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "trace.hh"

using namespace Disk ;
using namespace Time ;

StaticUniqPtr<::string> g_trace_file ;

Atomic<bool    > Trace::s_backup_trace = false        ;
Atomic<size_t  > Trace::s_sz           = 100<<20      ; // limit to reasonable value until overridden
Atomic<Channels> Trace::s_channels     = DfltChannels ; // by default, trace default channel

#ifdef TRACE

	size_t                 Trace::_s_pos       =  0      ;
	bool                   Trace::_s_ping      = false   ;
	Fd                     Trace::_s_fd        ;
	Atomic<bool>           Trace::_s_has_trace = false   ;
	uint8_t*               Trace::_s_data      = nullptr ;
	size_t                 Trace::_s_cur_sz    = 0       ;
	Mutex<MutexLvl::Trace> Trace::_s_mutex     ;

	thread_local int       Trace::_t_lvl  = 0       ;
	thread_local bool      Trace::_t_hide = false   ;
	thread_local ::string* Trace::_t_buf  = nullptr ;

	void Trace::s_start() {
		if ( !g_trace_file || !*g_trace_file ) return ;
		Lock lock{_s_mutex} ;
		dir_guard(*g_trace_file) ;
		_s_open() ;
	}

	void Trace::s_new_trace_file(::string const& trace_file) {
		if (!_s_has_trace            ) return ;                // change trace file, but dont start tracing
		if (trace_file==*g_trace_file) return ;
		//
		Lock lock{_s_mutex} ;
		//
		_s_has_trace = false ;
		fence() ;
		::munmap(_s_data,_s_cur_sz) ;
		_s_data   = nullptr ;
		_s_cur_sz = 0       ;
		_s_pos    = 0       ;
		_s_fd.close() ;
		*g_trace_file = trace_file ;
		_s_open() ;
	}

	void Trace::_s_open() {
		if (s_sz<4096     ) return ; // not enough room to trace
		if (!s_channels   ) return ; // nothing to trace
		if (!*g_trace_file) return ; // nowhere to trace to
		dir_guard(*g_trace_file) ;
		if (s_backup_trace) {
			::string prev_old ;
			for( char c : "54321"s ) { ::string old = *g_trace_file+'.'+c ; if (+prev_old) ::rename( old         . c_str() , prev_old.c_str() ) ; prev_old = ::move(old) ; }
			/**/                                                            if (+prev_old) ::rename( g_trace_file->c_str() , prev_old.c_str() ) ;
		}
		::string trace_dir_s    = dir_name_s(*g_trace_file)                                         ;
		::string tmp_trace_file = cat(trace_dir_s,::to_string(Pdate(New).nsec_in_s()),'-',getpid()) ;
		mk_dir_s(trace_dir_s) ;
		//
		_s_cur_sz = 4096                                                            ;
		_s_fd     = { tmp_trace_file , FdAction::CreateReadTrunc , true/*no_std*/ } ;
		//
		if ( !_s_fd                                                        ) throw cat("cannot create temporary trace file ",tmp_trace_file," : ",::strerror(errno)) ;
		if ( ::rename( tmp_trace_file.c_str() , g_trace_file->c_str() )!=0 ) throw cat("cannot create trace file "          ,*g_trace_file ," : ",::strerror(errno)) ;
		//
		try                     { _s_fd.write(::string(_s_cur_sz,0/*ch*/)) ;                                            }
		catch (::string const&) { throw cat("cannot set trace file ",*g_trace_file," to its initial size ",_s_cur_sz) ; }
		//
		_s_pos  = 0                                                                                                                      ;
		_s_data = static_cast<uint8_t*>(::mmap( nullptr/*addr*/ , _s_cur_sz , PROT_READ|PROT_WRITE , MAP_SHARED , _s_fd , 0/*offset*/ )) ;
		SWEAR( _s_data!=MAP_FAILED , *g_trace_file ) ;
		fence() ;
		_s_has_trace = +_s_fd ;      // ensure _s_has_trace is updated once everything is ok as tracing may be called from other threads while being initialized
	}

	void Trace::_t_commit() {
		static const ::string Giant = "<giant record>\n" ;                                                                           // /!\ cannot be constexpr with gcc-11
		//
		::string const& buf_view = _t_buf->size()<=(s_sz>>4) ? *_t_buf : Giant ;
		//
		{	Lock   lock    { _s_mutex }             ;
			size_t new_pos = _s_pos+buf_view.size() ;
			if ( _s_cur_sz<s_sz && new_pos>_s_cur_sz ) {
				size_t old_sz = _s_cur_sz ;
				_s_cur_sz = ::max( new_pos                          , old_sz+::min(old_sz>>2,size_t(1<<24)) ) ;                      // ensure remaps are in log(n) (up to a reasonable size increase)
				_s_cur_sz = ::min( round_up(_s_cur_sz,size_t(4096)) , size_t(s_sz)                          ) ;                      // legalize
				//
				// /!\ dont use ftruncate to avoid race in kernel between ftruncate and write back of dirty pages
				try                     { _s_fd.write(::string(_s_cur_sz-old_sz,0/*ch*/)) ;                                        }
				catch (::string const&) { fail_prod("cannot expand trace from",old_sz,"to",_s_cur_sz,"for file :",*g_trace_file) ; } // NO_COV
				//
				_s_data = static_cast<uint8_t*>(::mremap( _s_data , old_sz , _s_cur_sz , MREMAP_MAYMOVE )) ;
			}
			if (new_pos>s_sz) {
				if (_s_pos<s_sz) ::memset(_s_data+_s_pos,0,s_sz-_s_pos) ;
				_s_ping = !_s_ping        ;
				_s_pos  = 0               ;
				new_pos = buf_view.size() ;
			}
			::memmove( _s_data+_s_pos , buf_view.data() , buf_view.size() ) ;
			_s_pos = new_pos ;
		}
		_t_buf->clear() ;
	}

#endif
