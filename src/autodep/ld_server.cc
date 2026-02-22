// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#define IN_SERVER  1
#define LD_PRELOAD 1

#include "ld_server.hh"
#include "syscall_tab.hh"

thread_local bool                     AutodepLock::t_active = false ;
/**/         Mutex<MutexLvl::Autodep> AutodepLock::_s_mutex ;

inline bool started() { return AutodepLock::t_active ; } // no auto-start for server

// when in server, we must have a complete redirection table because :
// - dlsym takes an internal lock
// - if a thread A calls dlsym and at the same time thread B does a fork
// - then if the child calls dlsym before exec, it will dead-lock
// - this happens if get_orig needs to call dlsym
// note that when not in server, _g_mutex protects us (but it is not used in server when not spying accesses)
// note also that we cannot put s_libcall_tab in a static outside get_orig as get_orig may be called from global init, before this static initialization
static constexpr bool _get_orig_cmp_cstr( const char* a , const char* b ) {
	// ::strcmp is not constexpr with gcc-11, so do it by hand
	for( ; *a && *b ; a++,b++ ) {
		if (*a<*b) return true  ;
		if (*a>*b) return false ;
	}
	return *a<*b ;
}
static void* get_orig(const char* libcall) {
	#define LIBCALL_ENTRY(libcall) false                                                      // to enumrate libcalls
	static constexpr size_t NLibcalls = ::initializer_list<bool>{ENUMERATE_LIBCALLS}.size() ;
	#undef LIBCALL_ENTRY
	static constexpr ::array<const char*,NLibcalls> LibcallNames = []() {
		::array<const char*,NLibcalls> libcall_names ;
		size_t                         i             = 0 ;
		#define LIBCALL_ENTRY(libcall) #libcall                                               // to enumrate libcalls
		for( const char* lc : { ENUMERATE_LIBCALLS } ) libcall_names[i++] = lc ;
		#undef LIBCALL_ENTRY
		::sort( libcall_names.begin() , libcall_names.end() , _get_orig_cmp_cstr ) ;
		return libcall_names ;
	}() ;
	static Atomic<::array<void*,NLibcalls>*> s_libcall_tab = nullptr ;                        // use a pointer to avoid uncontrolled destruction at end of execution and finely controlled construction
	// /!\ we must manage the guard explicitly as compiler generated guard makes syscalls, which can induce loops
	if (!s_libcall_tab) {
		::array<void*,NLibcalls>& libcall_tab = *new ::array<void*,NLibcalls> ;
		for( size_t i : iota(NLibcalls) ) libcall_tab[i] = ::dlsym(RTLD_NEXT,LibcallNames[i]) ;
		if (s_libcall_tab.load()) delete &libcall_tab          ;                                // repeat test to avoid double allocation as much as possible
		else                      s_libcall_tab = &libcall_tab ;                                // dont delete old libcall tab as it may be in use by another thread, ...
	}                                                                                           // ... and forget about exceptional double allocation
	if (!libcall) return nullptr ;                                                              // used to initialize s_libcall_tab
	// /!\ this function must be signal-safe, hence must not call malloc
	auto   it = ::lower_bound( LibcallNames , libcall , _get_orig_cmp_cstr ) ; SWEAR_PROD( it!=LibcallNames.end() && ::strcmp(*it,libcall)==0 , *it,libcall ) ;
	size_t i  = it - LibcallNames.begin()                                    ;
	return (*s_libcall_tab.load())[i] ;
}
// initialize s_libcall_tab as early as possible, before any fork
// unfortunately some libs do accesses before entering main, so we cannot be sure this init is before all libcalls
static void* init_get_orig = get_orig(nullptr) ;

#include "ld_common.x.cc"

AutodepLock::AutodepLock(::vmap_s<DepDigest>* deps) : lock{_s_mutex} {
	// SWEAR(cwd_s()==Record::s_autodep_env().repo_root_s) ;           // too expensive
	SWEAR_PROD( !Record::s_deps && !Record::s_deps_err ) ;
	SWEAR_PROD( !*Record::s_access_cache               ) ;
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
