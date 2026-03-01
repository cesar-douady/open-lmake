// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "syscall_tab.hh"

namespace AutodepPtrace {
	int/*rc*/      prepare_child(void*          ) ; // must be called from child
	int/*wstatus*/ process      (pid_t child_pid) ;
} ;

#if CAN_AUTODEP_SECCOMP
	namespace AutodepSeccomp {
		int/*rc*/      prepare_child(void*          ) ; // must be called from child
		int/*wstatus*/ process      (pid_t child_pid) ;
	} ;
#endif
