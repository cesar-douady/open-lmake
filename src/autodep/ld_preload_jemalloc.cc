// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define LD_PRELOAD_JEMALLOC 1
#define LD_PRELOAD          1

#include "utils.hh"

// ensure malloc has been initialized (at least at first call to malloc) in case jemalloc is used with ld_preload to avoid malloc_init->open->malloc->malloc_init loop
static bool _g_started = false ;

inline bool started() { return _g_started ; }

void* get_orig(const char* libcall) {
	void* res = ::dlsym(RTLD_NEXT,libcall) ;
	swear_prod(res,"cannot find symbol",libcall,"in libc") ;
	return res ;
}

#include "ld_common.x.cc"

// if we can intercept program start, the semantic is clear : it is right before global constructors in main program
// else we define a static, which is somewhere before global constructors in main program, but unknown relative to other global contructors
// the first solution may not be the best, but at least it has a clear and reproductible semantic
#if USE_LIBC_START_MAIN
	#pragma GCC visibility push(default)                                                                              // force visibility of functions defined hereinafter, until the corresponding pop
	extern "C" {
		int __libc_start_main( void* main , int argc , void* argv , void* auxvec , void* init , void* fini , void* rtld_fini , void* stack_end) {
			static auto orig = reinterpret_cast<decltype(__libc_start_main)*>(dlsym(RTLD_NEXT,"__libc_start_main")) ;
			free(malloc(1)) ;                                                                                         // ensure malloc is initialized before we start
			_g_started = true ;
			return orig(main,argc,argv,auxvec,init,fini,rtld_fini,stack_end) ;
		}
	}
	#pragma GCC visibility pop
#else
	static bool _g_start = ( free(malloc(1)) , _g_started=true ) ;                                                    // ensure malloc is initialized before we start
#endif
