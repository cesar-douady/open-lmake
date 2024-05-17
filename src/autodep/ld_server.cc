// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define IN_SERVER  1
#define LD_PRELOAD 1

#include <dlfcn.h>

#include "ld_server.hh"
#include "syscall_tab.hh"

thread_local bool                      AutodepLock::t_active = false ;
/**/         Mutex<MutexLvl::Autodep1> AutodepLock::_s_mutex ;

static bool started() { return AutodepLock::t_active ; } // no auto-start for server

// when in server, we must have a complete redirection table because :
// - dlsym takes an internal lock
// - if a thread A calls dlsym and at the same time thread B does a fork
// - then if the child calls dlsym before exec, it will dead-lock
// - this happens if get_orig needs to call dlsym
// note that when not in server, _g_mutex protects us (but it is not used in server when not spying accesses)
// note also that we cannot put s_syscall_tab in a static outside get_orig as get_orig may be called from global init, before this static initialization
void* get_orig(const char* syscall) {
	#define SYSCALL_ENTRY(syscall) { #syscall , ::dlsym(RTLD_NEXT,#syscall) }
	static ::umap_s<void*> const* const s_syscall_tab = new ::umap_s<void*>{ ENUMERATE_SYSCALLS } ;
	if (!syscall) return nullptr ;                                                                  // used to initialize s_syscall_tab
	void* res = s_syscall_tab->at(syscall) ;
	swear_prod(res,"cannot find symbol ",syscall," in libc") ;
	return res ;
}
static void* init_get_orig = get_orig(nullptr) ;                                                    // initialize s_syscall_tab as early as possible, hoping that there is no fork before

#include "ld.x.cc"
#include "ld_common.x.cc"

AutodepLock::AutodepLock(::vmap_s<DepDigest>* deps) : lock{_s_mutex} {
	// SWEAR(cwd()==Record::s_autodep_env().root_dir) ;                // too expensive
	SWEAR( !Record::s_deps && !Record::s_deps_err ) ;
	SWEAR( !*Record::s_access_cache               ) ;
	Record::s_deps     = deps ;
	Record::s_deps_err = &err ;
	t_active           = true ;
}

AutodepLock::~AutodepLock() {
	Record::s_deps     = nullptr ;
	Record::s_deps_err = nullptr ;
	t_active           = false   ;
	Record::s_access_cache->clear() ;
	if (auditer().seen_chdir) swear_prod(::fchdir(Record::s_root_fd())==0) ; // restore cwd in case it has been modified during user Python code execution
}
