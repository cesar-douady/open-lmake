// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/mman.h>

#include "app.hh"
#include "disk.hh"

#include "trace.hh"

using namespace Disk ;
using namespace Time ;

::string* g_trace_file = nullptr ; // pointer to avoid init/fini order hazards, relative to admin dir

::atomic<bool    > Trace::s_backup_trace = false        ;
::atomic<size_t  > Trace::s_sz           = 100<<20      ; // limit to reasonable value until overridden
::atomic<Channels> Trace::s_channels     = DfltChannels ; // by default, trace default channel

#ifndef NO_TRACE

	size_t                 Trace::_s_pos       =  0      ;
	bool                   Trace::_s_ping      = false   ;
	Fd                     Trace::_s_fd        ;
	::atomic<bool>         Trace::_s_has_trace = false   ;
	uint8_t*               Trace::_s_data      = nullptr ;
	size_t                 Trace::_s_cur_sz    = 0       ;
	Mutex<MutexLvl::Trace> Trace::_s_mutex     ;

	thread_local int            Trace::_t_lvl  = 0       ;
	thread_local bool           Trace::_t_hide = false   ;
	thread_local OStringStream* Trace::_t_buf  = nullptr ;

	void Trace::s_start() {
		t_thread_key = '=' ;                            // called from main thread
		if ( !g_trace_file || !*g_trace_file ) return ;
		Lock lock{_s_mutex} ;
		dir_guard(*g_trace_file) ;
		_s_open() ;
	}

	void Trace::s_new_trace_file(::string const& trace_file) {
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
		if (!s_sz             ) return ;  // no room to trace
		if (!s_channels.load()) return ;  // nothing to trace
		dir_guard(*g_trace_file) ;
		if (s_backup_trace) {
			::string prev_old ;
			for( char c : "54321"s ) { ::string old = to_string(*g_trace_file,'.',c) ; if (+prev_old) ::rename( old.c_str()           , prev_old.c_str() ) ; prev_old = ::move(old) ; }
			/**/                                                                       if (+prev_old) ::rename( g_trace_file->c_str() , prev_old.c_str() ) ;
		}
		::string trace_dir      = dir_name(*g_trace_file)                                      ;
		::string tmp_trace_file = to_string(trace_dir,'/',Pdate(New).nsec_in_s(),'-',getpid()) ;
		try                        { mk_dir(trace_dir) ;                               }
		catch (::string const& e ) { FAIL("cannot create trace dir",trace_dir,':',e) ; }
		//
		_s_cur_sz = 4096                                                                                  ;
		_s_fd     = ::open( tmp_trace_file.c_str() , O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_TRUNC , 0666 ) ; _s_fd.no_std() ;
		//
		if ( !_s_fd                                                        ) FAIL("cannot create temporary trace file",tmp_trace_file,':',strerror(errno)            ) ;
		if ( ::rename( tmp_trace_file.c_str() , g_trace_file->c_str() )!=0 ) FAIL("cannot create trace file"          ,*g_trace_file ,':',strerror(errno)            ) ;
		if ( ::ftruncate(_s_fd,_s_cur_sz)!=0                               ) FAIL("cannot truncate trace file"        ,*g_trace_file ,"to its initial size",_s_cur_sz) ;
		//
		_s_pos  = 0                                                                                                    ;
		_s_data = static_cast<uint8_t*>(::mmap( nullptr , _s_cur_sz , PROT_READ|PROT_WRITE , MAP_SHARED , _s_fd , 0 )) ;
		SWEAR(_s_data!=MAP_FAILED,*g_trace_file) ;
		fence() ;
		_s_has_trace = +_s_fd ;    // ensure _s_has_trace is updated once everything is ok as tracing may be called from other threads while being initialized
	}

	void Trace::_t_commit() {
		//
		#if HAS_OSTRINGSTREAM_VIEW
			::string_view buf_view = _t_buf->view() ;
		#else
			::string      buf_view = _t_buf->str () ;
		#endif
		//
		{	Lock   lock    { _s_mutex }             ;
			size_t new_pos = _s_pos+buf_view.size() ;
			if ( _s_cur_sz<s_sz && new_pos>_s_cur_sz ) {
				size_t old_sz = _s_cur_sz ;
				_s_cur_sz = ::min( round_up( new_pos + (_s_cur_sz>>2) , 4096 ) , size_t(s_sz) ) ; // ensure exponential growth to limit calls to ftruncate
				if (::ftruncate(_s_fd,_s_cur_sz)!=0) FAIL(_s_fd,old_sz,_s_cur_sz) ;
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
		_t_buf->str({}) ;
	}

#endif
