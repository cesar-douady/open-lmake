// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/ptrace.h>
#if HAS_PTRACE_GET_SYSCALL_INFO        // must be after utils.hh include, use portable calls if implemented
	#include <linux/ptrace.h>          // for struct ptrace_syscall_info, must be after sys/ptrace.h to avoid stupid request macro definitions
	#if MUST_UNDEF_PTRACE_MACROS       // must be after utils.hh include
		#undef PTRACE_CONT             // /!\ stupid linux/ptrace.h defines ptrace requests as macros while ptrace expects an enum on some systems
		#undef PTRACE_GET_SYSCALL_INFO // .
		#undef PTRACE_PEEKDATA         // .
		#undef PTRACE_POKEDATA         // .
		#undef PTRACE_SETOPTIONS       // .
		#undef PTRACE_SYSCALL          // .
		#undef PTRACE_TRACEME          // .
	#endif
#endif

#include "gather_deps.hh"
#include "record.hh"

struct AutodepPtrace {
	// statics
	static void s_prepare_child() ;    // must be called from child
	// static data
	static AutodepEnv* s_autodep_env ; // declare as pointer to avoid static late initialization
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
