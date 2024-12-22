// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define LD_PRELOAD 1

#include <dlfcn.h>

#include "utils.hh"

static bool started() { return true ; }

void* get_orig(const char* libcall) {
	void* res = ::dlsym(RTLD_NEXT,libcall) ;
	swear_prod(res,"cannot find symbol",libcall,"in libc") ;
	return res ;
}

#include "ld_common.x.cc"
