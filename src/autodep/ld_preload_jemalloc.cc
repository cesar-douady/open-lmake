// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

// ensure malloc has been initialized (at least at first call to malloc) in case jemalloc is used with ld_preload to avoid malloc_init->open->malloc->malloc_init loop
static bool _g_started = false ;

static inline bool started() { return _g_started ; }

#include "ld.x.cc"

#pragma GCC visibility push(default) // force visibility of functions defined hereinafter, until the corresponding pop
extern "C" {
	int __libc_start_main( void* main , int argc , void* argv , void* auxvec , void* init , void* fini , void* rtld_fini , void* stack_end) {
		static auto orig = reinterpret_cast<decltype(__libc_start_main)*>(dlsym(RTLD_NEXT,"__libc_start_main")) ;
		_g_started = true ;
		return orig(main,argc,argv,auxvec,init,fini,rtld_fini,stack_end) ;
	}
}
#pragma GCC visibility pop
