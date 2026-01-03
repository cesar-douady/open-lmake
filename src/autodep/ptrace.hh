// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/ptrace.h>

#include "gather.hh"
#include "record.hh"

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

#if HAS_SECCOMP          // must be after utils.hh include
	#include <seccomp.h>
#endif

struct AutodepPtrace {
	// init
	static void s_init(AutodepEnv const&) ;
	// statics
	static int/*rc*/ s_prepare_child(void*) ; // must be called from child
	// static data
	#if HAS_SECCOMP
		static ::scmp_filter_ctx s_scmp ;
	#endif
	// cxtors & casts
	AutodepPtrace() = default ;
	AutodepPtrace(pid_t cp) { init(cp) ; }
	void init(pid_t child_pid) ;
	// services
private :
	bool/*done*/ _changed( int pid , int&/*inout*/ wstatus ) ;
public :
	int/*wstatus*/ process() ;
	// data
	pid_t child_pid = 0 ;
} ;
