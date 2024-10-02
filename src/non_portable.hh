// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/user.h>

#include <array>
#include <fstream>

using namespace std ;

static constexpr char NpErrnoSymbolName[]  = "__errno_location" ; // XXX : find a way to stick to documented interfaces
static constexpr bool NpStackGrowsDownward = true               ; // XXX : find a way to determine that

#if __x86_64__ || __aarch64__
	static constexpr uint8_t NpWordSz = 64 ;
#elif __i386__ || __arm__
	static constexpr uint8_t NpWordSz = 32 ;
#endif

int np_get_fd(std::filebuf& fb) ;

::array<uint64_t,6> np_ptrace_get_args( pid_t pid ,               uint8_t word_sz ) ; // word_sz must be 32 or 64
int64_t             np_ptrace_get_res ( pid_t pid ,               uint8_t word_sz ) ; // .
long                np_ptrace_get_nr  ( pid_t pid ,               uint8_t word_sz ) ; // .
void                np_ptrace_set_res ( pid_t pid , int64_t val , uint8_t word_sz ) ; // .
