// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/user.h>

#include <fstream>

int np_get_fd(std::filebuf& fb) ;

static constexpr char np_errno_symbol_name[] = "__errno_location" ;            // XXX : find a way to stick to documented interfaces

int  np_ptrace_get_errno    (int pid) ;
void np_ptrace_clear_syscall(int pid) ;
