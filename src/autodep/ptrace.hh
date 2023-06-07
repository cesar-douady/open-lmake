// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "gather_deps.hh"
#include "record.hh"

#if HAS_PTRACE

	struct AutodepPtrace {
		// statics
		static void s_prepare_child() ;                    // must be called from child
		// static data
		static AutodepEnv s_autodep_env ;
		// cxtors & casts
		AutodepPtrace(        ) = default ;
		AutodepPtrace(pid_t cp) { _init(cp) ; }
	private :
		void _init(pid_t child_pid) ;
		// services
		::pair<bool/*done*/,int/*wstatus*/> _changed( int pid , int wstatus ) ;
	public :
		int/*wstatus*/ process() {
			int  wstatus ;
			int  pid     ;
			bool done    ;
			while( (pid=wait(&wstatus))>=0 ) {
				::tie(done,wstatus) = _changed(pid,wstatus) ;
				if (done) return wstatus ;
			}
			fail("process ",child_pid," did not exit nor was signaled") ;
		}
		// data
		int child_pid ;
	} ;

#else

	struct AutodepPtrace {
		// statics
		[[noreturn]] static void bad() { fail_prod("autodep method ptrace not supported") ; }
		static void s_prepare_child() { bad() ; }
		// static data
		static AutodepEnv s_autodep_env ;                  // useless but needed so that compilation cant be carried out
		// cxtors & casts
		AutodepPtrace() = default ;
		AutodepPtrace(int /*child_pid*/) { bad() ; }
		int/*wstatus*/ process() { bad() ; }
	} ;

#endif
