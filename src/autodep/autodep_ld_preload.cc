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

#include "autodep_ld.hh"

static void* _s_libc_handle = nullptr ;
static int _dl_cb( struct dl_phdr_info* info , size_t /*size*/ , void* ) {
	if (!is_libc(info->dlpi_name)) return 0 ;
	_s_libc_handle = ::dlopen( info->dlpi_name , RTLD_NOW|RTLD_NOLOAD ) ;
	return 1 ;
}
void* get_libc_handle() {
	dl_iterate_phdr(_dl_cb,nullptr) ;                                           // /!\ we cannot simply use RTLD_NEXT because when there are several versions of the symbol in libc
	return _s_libc_handle ;                                                     // (which is the case for realpath), RTLD_NEXT would return the oldest version instead of the default one
}

#define LD_PRELOAD 1
#include "autodep_ld.cc"
