// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define IN_SERVER  1
#define LD_PRELOAD 1

#include "ld_server.hh"
#include "syscall_tab.hh"

thread_local bool                      AutodepLock::t_active = false ;
/**/         Mutex<MutexLvl::Autodep1> AutodepLock::_s_mutex ;

inline bool started() { return AutodepLock::t_active ; } // no auto-start for server

// when in server, we must have a complete redirection table because :
// - dlsym takes an internal lock
// - if a thread A calls dlsym and at the same time thread B does a fork
// - then if the child calls dlsym before exec, it will dead-lock
// - this happens if get_orig needs to call dlsym
// note that when not in server, _g_mutex protects us (but it is not used in server when not spying accesses)
// note also that we cannot put s_libcall_tab in a static outside get_orig as get_orig may be called from global init, before this static initialization
static void* get_orig(const char* libcall) {
	static constexpr size_t NChar = 20 ;                  // 12 is enough to distinguish /!\ this function must be signal-safe, hence must not call malloc : use char[] as key instead of ::string
	using LibCallTab = ::map<::array<char,NChar>,void*> ;
	static Atomic<LibCallTab*> s_libcall_tab = nullptr ;  // use a pointer to avoid uncontrolled destruction at end of execution and finely controlled construction
	// /!\ we must manage the guard explicitly as compiler generated guard makes syscalls, which can induce loops
	if (!s_libcall_tab) {
		#define LIBCALL_ENTRY(libcall) #libcall
		LibCallTab* new_libcall_tab  = new LibCallTab ;
		for( const char* lc1 : { ENUMERATE_LIBCALLS } ) {
			::array<char,NChar> lc2 = {} ; strncpy(lc2.data(),lc1,NChar-1) ;
			bool inserted = new_libcall_tab->try_emplace(lc2,::dlsym(RTLD_NEXT,lc1)).second ;
			SWEAR( inserted , lc1,lc2 ) ;                                                     // ensure troncation does not produce ambguities
		}
		#undef LIBCALL_ENTRY
		LibCallTab* prev_libcall_tab = nullptr ;
		if (!s_libcall_tab.compare_exchange_strong( prev_libcall_tab , new_libcall_tab )) delete new_libcall_tab ;
	}
	if (!libcall) return nullptr ;                                                            // used to initialize s_libcall_tab
	::array<char,NChar> lc = {} ; strncpy(lc.data(),libcall,NChar-1) ;
	try         { return s_libcall_tab.load()->at(lc) ;               }
	catch (...) { fail_prod("cannot find symbol",libcall,"in libc") ; }                       // NO_COV
}
// initialize s_libcall_tab as early as possible, before any fork
// unfortunately some libs do accesses before entering main, so we cannot be sure this init is before all libcalls
static void* init_get_orig = get_orig(nullptr) ;

#include "ld_common.x.cc"

AutodepLock::AutodepLock(::vmap_s<DepDigest>* deps) : lock{_s_mutex} {
	// SWEAR(cwd_s()==Record::s_autodep_env().repo_root_s) ;           // too expensive
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
	if (auditor().seen_chdir) swear_prod(::fchdir(Record::s_repo_root_fd())==0) ; // restore cwd in case it has been modified during user python code execution
}
