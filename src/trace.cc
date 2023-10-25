// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "trace.hh"

using namespace Disk ;
using namespace Time ;

::string* g_trace_file ;               // pointer to avoid init/fini order hazards, relative to admin dir

bool              Trace::s_backup_trace = false ;
::atomic<size_t>  Trace::s_sz           = -1    ;          // do not limit trace as long as not instructed to
thread_local char Trace::t_key          = '?'   ;

#ifndef NO_TRACE

	size_t  Trace::_s_pos   =  0    ;
	bool    Trace::_s_ping  = false ;
	Fd      Trace::_s_fd    ;
	::mutex Trace::_s_mutex ;

	thread_local int            Trace::_t_lvl  = 0       ;
	thread_local bool           Trace::_t_hide = false   ;
	thread_local OStringStream* Trace::_t_buf  = nullptr ;

	void Trace::s_start() {
		if ( !g_trace_file || g_trace_file->empty() ) return ;
		t_key = '=' ;                                                          // called from main thread
		dir_guard(*g_trace_file) ;
		_s_open() ;
	}

	void Trace::s_new_trace_file(::string const& trace_file) {
		if (trace_file==*g_trace_file) return ;
		//
		::unique_lock lock{_s_mutex} ;
		//
		_s_fd.close() ;
		*g_trace_file = trace_file ;
		_s_open() ;
	}
	void Trace::_s_open() {
		dir_guard(*g_trace_file) ;
		if (s_backup_trace) {
			::string prev_old ;
			for( char c : "54321"s ) { ::string old = to_string(*g_trace_file,'.',c) ; if (!prev_old.empty()) ::rename( old.c_str()           , prev_old.c_str() ) ; prev_old = ::move(old) ; }
			/**/                                                                       if (!prev_old.empty()) ::rename( g_trace_file->c_str() , prev_old.c_str() ) ;
		}
		_s_fd = open_write(*g_trace_file) ;
		_s_fd.no_std() ;
	}

#endif
