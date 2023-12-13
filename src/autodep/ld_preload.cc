// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>

#include "utils.hh"

struct Ctx {
	// cxtors & casts
	Ctx               () { save_errno   () ; }
	~Ctx              () { restore_errno() ; }
	// services
	void save_errno   () { errno_ = errno  ; }
	void restore_errno() { errno  = errno_ ; }
	//
	bool get_no_file() const {
		switch (errno_) {
			case EISDIR  :
			case ENOENT  :
			case ENOTDIR : return true  ;
			default      : return false ;
		}
	}
	// data
	int errno_ ;
} ;

struct Lock {
	// s_mutex prevents several threads from recording deps simultaneously
	// t_loop prevents recursion within a thread :
	//   if a thread does access while processing an original access, this second access must not be recorded, it is for us, not for the user
	//   in that case, Lock let it go, but then, t_busy will return true, which in turn will prevent recording
	// t_loop must be thread local so as to distinguish which thread owns the mutex. Values can be :
	// - 0 : thread is outside and must acquire the mutex to enter
	// - 1 : thread is processing a user access and must record deps
	// - 2 : thread has entered a recursive call and must not record deps
	// statics
	static bool t_busy() { return t_loop ; }                                  // same, before taking a 2nd lock
	// static data
	static              ::mutex s_mutex ;
	static thread_local bool    t_loop  ;
	// cxtors & casts
	Lock () { SWEAR(!t_loop) ; t_loop = true  ; s_mutex.lock  () ; }
	~Lock() { SWEAR( t_loop) ; t_loop = false ; s_mutex.unlock() ; }
} ;
::mutex           Lock::s_mutex ;
bool thread_local Lock::t_loop  = false ;

void* get_orig(const char* syscall) {
	void* res = ::dlsym(RTLD_NEXT,syscall) ;                                   // with CentOS-7, dlopen is in libdl, not in libc, but we want to track it
	swear_prod(res,"cannot find symbol ",syscall," in libc") ;
	return res ;
}

#define LD_PRELOAD 1
#include "ld.cc"
