// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "trace.hh"

using namespace Disk ;
using namespace Time ;

::string* g_trace_file = nullptr ; // pointer to avoid init/fini order hazards, relative to admin dir

bool              Trace::s_backup_trace = false        ;
::atomic<size_t>  Trace::s_sz           = 100<<20      ; // limit to reasonable value until overridden
Channels          Trace::s_channels     = DfltChannels ; // by default, trace default channel

#ifndef NO_TRACE

	size_t                 Trace::_s_pos    =  0    ;
	bool                   Trace::_s_ping   = false ;
	Fd                     Trace::_s_fd     ;
	::atomic<bool>         Trace::_s_has_fd = false ;
	Mutex<MutexLvl::Trace> Trace::_s_mutex  ;

	thread_local int            Trace::_t_lvl  = 0       ;
	thread_local bool           Trace::_t_hide = false   ;
	thread_local OStringStream* Trace::_t_buf  = nullptr ;

	void Trace::s_start(Channels cs) {
		if ( !g_trace_file || !*g_trace_file ) return ;
		t_thread_key = '=' ;                            // called from main thread
		s_channels   = cs  ;
		dir_guard(*g_trace_file) ;
		_s_open() ;
	}

	void Trace::s_new_trace_file(::string const& trace_file) {
		if (trace_file==*g_trace_file) return ;
		//
		Lock lock{_s_mutex} ;
		//
		_s_has_fd = false ;
		fence() ;
		_s_fd.close() ;
		*g_trace_file = trace_file ;
		_s_open() ;
	}
	void Trace::_s_open() {
		if (!s_sz      ) return ;           // no room to trace
		if (!s_channels) return ;           // nothing to trace
		dir_guard(*g_trace_file) ;
		if (s_backup_trace) {
			::string prev_old ;
			for( char c : "54321"s ) { ::string old = to_string(*g_trace_file,'.',c) ; if (+prev_old) ::rename( old.c_str()           , prev_old.c_str() ) ; prev_old = ::move(old) ; }
			/**/                                                                       if (+prev_old) ::rename( g_trace_file->c_str() , prev_old.c_str() ) ;
		}
		unlnk(*g_trace_file) ;              // avoid write clashes if trace is still being written by another process
		Fd fd = open_write(*g_trace_file) ;
		fd.no_std() ;
		_s_pos = 0   ;
		_s_fd  = fd  ;                      // ensure _s_fd is updated once everything is ok as tracing may be called from other threads while being initialized
		fence() ;
		_s_has_fd = +fd ;
	}

#endif
