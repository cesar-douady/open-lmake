// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/user.h>

#include <array>
#include <fstream>

using namespace std ;

static constexpr int MaxErrno = 255 ;  // this is pretty comfortable, actual value is 133 // XXX : find a way to use a documented value

int np_get_fd(std::filebuf& fb) ;

static constexpr char np_errno_symbol_name[] = "__errno_location" ;            // XXX : find a way to stick to documented interfaces

::array<uint64_t,6> np_ptrace_get_args(int pid) ;
::pair<int64_t/*res*/,int/*errno*/> np_ptrace_get_res_errno(int pid) ;
long np_ptrace_get_syscall  (int pid) ;
void np_ptrace_clear_syscall(int pid) ;
