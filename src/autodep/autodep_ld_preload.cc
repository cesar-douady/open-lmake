// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

struct Ctx {
	Ctx               ()       { save_errno   () ; }
	~Ctx              ()       { restore_errno() ; }
	int  get_errno    () const { return errno_ ;   }
	void save_errno   ()       { errno_ = errno  ; }
	void restore_errno()       { errno  = errno_ ; }
	int errno_ ;
} ;

struct Lock {
	// statics
	static bool s_busy() { SWEAR(t_loop) ; return t_loop>1 ; }
	// static data
	static              ::mutex s_mutex ;
	static thread_local uint8_t t_loop  ;
	// cxtors & casts
	Lock () { if (  t_loop++) { SWEAR(!s_mutex.try_lock()) ; return ; } s_mutex.lock  () ; }
	~Lock() { if (--t_loop  ) { SWEAR(!s_mutex.try_lock()) ; return ; } s_mutex.unlock() ; }
} ;
::mutex              Lock::s_mutex ;
thread_local uint8_t Lock::t_loop  ;

void* get_orig(const char* syscall) {
	void* res = ::dlsym(RTLD_NEXT,syscall) ;                              // with CentOS-7, dlopen is in libdl, not in libc, but we want to track it
	swear_prod(res,"cannot find symbol ",syscall," in libc") ;
	return res ;
}

#define LD_PRELOAD 1
#include "autodep_ld.cc"
