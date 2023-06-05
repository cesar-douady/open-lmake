// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <link.h>   // all dynamic link related

#include "utils.hh"

struct Ctx {
	int get_errno() const ;
	void save_errno   () {}
	void restore_errno() {}
} ;

struct Lock {
	static bool s_busy() { return false ; }
	static ::mutex s_mutex ;
	Lock () { s_mutex.lock  () ; }
	~Lock() { s_mutex.unlock() ; }
} ;
::mutex Lock::s_mutex ;

#include "autodep_ld.hh"

inline int Ctx::get_errno() const {
	static int* (*orig)() = reinterpret_cast<int* (*)()>(get_orig("__errno_location")) ; // gather errno from app space // XXX : find a way to stick to documented interfaces
	return *orig() ;
}

Lmid_t      g_libc_lmid = 0       ;                                            // acquired during initial audit
const char* g_libc_name = nullptr ;                                            // .

void* get_libc_handle() {
	if (!g_libc_name) return nullptr                                                       ;
	else              return ::dlmopen( g_libc_lmid , g_libc_name , RTLD_NOW|RTLD_NOLOAD ) ;
}

#define LD_AUDIT 1
#include "autodep_ld.cc"

struct SymEntry {
	uintptr_t func        = 0     ;
	bool      data_access = false ;
} ;
::umap_s<SymEntry> const g_syscall_tab = {
	{ "chdir"            , { reinterpret_cast<uintptr_t>(Audited::chdir            ) , true } }
,	{ "close"            , { reinterpret_cast<uintptr_t>(Audited::close            ) , true } }
,	{ "__close"          , { reinterpret_cast<uintptr_t>(Audited::__close          ) , true } }
,	{ "creat"            , { reinterpret_cast<uintptr_t>(Audited::creat            ) , true } }
,	{ "creat64"          , { reinterpret_cast<uintptr_t>(Audited::creat64          ) , true } }
,	{ "dup2"             , { reinterpret_cast<uintptr_t>(Audited::dup2             ) , true } }
,	{ "dup3"             , { reinterpret_cast<uintptr_t>(Audited::dup3             ) , true } }
,	{ "execl"            , { reinterpret_cast<uintptr_t>(Audited::execl            ) , true } }
,	{ "execle"           , { reinterpret_cast<uintptr_t>(Audited::execle           ) , true } }
,	{ "execlp"           , { reinterpret_cast<uintptr_t>(Audited::execlp           ) , true } }
,	{ "execv"            , { reinterpret_cast<uintptr_t>(Audited::execv            ) , true } }
,	{ "execve"           , { reinterpret_cast<uintptr_t>(Audited::execve           ) , true } }
,	{ "execveat"         , { reinterpret_cast<uintptr_t>(Audited::execveat         ) , true } }
,	{ "execvp"           , { reinterpret_cast<uintptr_t>(Audited::execvp           ) , true } }
,	{ "execvpe"          , { reinterpret_cast<uintptr_t>(Audited::execvpe          ) , true } }
,	{ "fchdir"           , { reinterpret_cast<uintptr_t>(Audited::fchdir           ) , true } }
,	{ "fopen"            , { reinterpret_cast<uintptr_t>(Audited::fopen            ) , true } }
,	{ "fopen64"          , { reinterpret_cast<uintptr_t>(Audited::fopen64          ) , true } }
,	{ "freopen"          , { reinterpret_cast<uintptr_t>(Audited::freopen          ) , true } }
,	{ "freopen64"        , { reinterpret_cast<uintptr_t>(Audited::freopen64        ) , true } }
,	{ "link"             , { reinterpret_cast<uintptr_t>(Audited::link             ) , true } }
,	{ "linkat"           , { reinterpret_cast<uintptr_t>(Audited::linkat           ) , true } }
//,	{ "mkostemp"         , { reinterpret_cast<uintptr_t>(Audited::mkostemp         ) , true } } // normally only access $TMPDIR which is not tracked for deps (and difficult to implement)
//,	{ "mkostemp64"       , { reinterpret_cast<uintptr_t>(Audited::mkostemp64       ) , true } } // .
//,	{ "mkostemps"        , { reinterpret_cast<uintptr_t>(Audited::mkostemps        ) , true } } // .
//,	{ "mkostemps64"      , { reinterpret_cast<uintptr_t>(Audited::mkostemps64      ) , true } } // .
//,	{ "mkstemp"          , { reinterpret_cast<uintptr_t>(Audited::mkstemp          ) , true } } // .
//,	{ "mkstemp64"        , { reinterpret_cast<uintptr_t>(Audited::mkstemp64        ) , true } } // .
//,	{ "mkstemps"         , { reinterpret_cast<uintptr_t>(Audited::mkstemps         ) , true } } // .
//,	{ "mkstemps64"       , { reinterpret_cast<uintptr_t>(Audited::mkstemps64       ) , true } } // .
,	{ "open"             , { reinterpret_cast<uintptr_t>(Audited::open             ) , true } }
,	{ "__open"           , { reinterpret_cast<uintptr_t>(Audited::__open           ) , true } }
,	{ "__open_nocancel"  , { reinterpret_cast<uintptr_t>(Audited::__open_nocancel  ) , true } }
,	{ "__open_2"         , { reinterpret_cast<uintptr_t>(Audited::__open_2         ) , true } }
,	{ "open64"           , { reinterpret_cast<uintptr_t>(Audited::open64           ) , true } }
,	{ "__open64"         , { reinterpret_cast<uintptr_t>(Audited::__open64         ) , true } }
,	{ "__open64_nocancel", { reinterpret_cast<uintptr_t>(Audited::__open64_nocancel) , true } }
,	{ "__open64_2"       , { reinterpret_cast<uintptr_t>(Audited::__open64_2       ) , true } }
,	{ "openat"           , { reinterpret_cast<uintptr_t>(Audited::openat           ) , true } }
,	{ "__openat_2"       , { reinterpret_cast<uintptr_t>(Audited::__openat_2       ) , true } }
,	{ "openat64"         , { reinterpret_cast<uintptr_t>(Audited::openat64         ) , true } }
,	{ "__openat64_2"     , { reinterpret_cast<uintptr_t>(Audited::__openat64_2     ) , true } }
,	{ "readlink"         , { reinterpret_cast<uintptr_t>(Audited::readlink         ) , true } }
,	{ "readlinkat"       , { reinterpret_cast<uintptr_t>(Audited::readlinkat       ) , true } }
,	{ "__readlinkat_chk" , { reinterpret_cast<uintptr_t>(Audited::__readlinkat_chk ) , true } }
,	{ "__readlink_chk"   , { reinterpret_cast<uintptr_t>(Audited::__readlink_chk   ) , true } }
,	{ "rename"           , { reinterpret_cast<uintptr_t>(Audited::rename           ) , true } }
,	{ "renameat"         , { reinterpret_cast<uintptr_t>(Audited::renameat         ) , true } }
,	{ "renameat2"        , { reinterpret_cast<uintptr_t>(Audited::renameat2        ) , true } }
,	{ "symlink"          , { reinterpret_cast<uintptr_t>(Audited::symlink          ) , true } }
,	{ "symlinkat"        , { reinterpret_cast<uintptr_t>(Audited::symlinkat        ) , true } }
,	{ "truncate"         , { reinterpret_cast<uintptr_t>(Audited::truncate         ) , true } }
,	{ "truncate64"       , { reinterpret_cast<uintptr_t>(Audited::truncate64       ) , true } }
,	{ "unlink"           , { reinterpret_cast<uintptr_t>(Audited::unlink           ) , true } }
,	{ "unlinkat"         , { reinterpret_cast<uintptr_t>(Audited::unlinkat         ) , true } }
,	{ "vfork"            , { reinterpret_cast<uintptr_t>(Audited::vfork            ) , true } }
,	{ "__vfork"          , { reinterpret_cast<uintptr_t>(Audited::__vfork          ) , true } }
//
// mere path accesses, no actual accesses to file data
//
,	{ "access"    , { reinterpret_cast<uintptr_t>(Audited::access   ) , false } }
,	{ "faccessat" , { reinterpret_cast<uintptr_t>(Audited::faccessat) , false } }
,	{ "opendir"   , { reinterpret_cast<uintptr_t>(Audited::opendir  ) , false } }
,	{ "rmdir"     , { reinterpret_cast<uintptr_t>(Audited::rmdir    ) , false } }
,	{ "mkdir"     , { reinterpret_cast<uintptr_t>(Audited::mkdir    ) , false } }
,	{ "mkdirat"   , { reinterpret_cast<uintptr_t>(Audited::mkdirat  ) , false } }
,	{ "statx"     , { reinterpret_cast<uintptr_t>(Audited::statx    ) , false } }
//
,	{ "__xstat"      , { reinterpret_cast<uintptr_t>(Audited::__xstat     ) , false } }
,	{ "__xstat64"    , { reinterpret_cast<uintptr_t>(Audited::__xstat64   ) , false } }
,	{ "__lxstat"     , { reinterpret_cast<uintptr_t>(Audited::__lxstat    ) , false } }
,	{ "__lxstat64"   , { reinterpret_cast<uintptr_t>(Audited::__lxstat64  ) , false } }
,	{ "__fxstatat"   , { reinterpret_cast<uintptr_t>(Audited::__fxstatat  ) , false } }
,	{ "__fxstatat64" , { reinterpret_cast<uintptr_t>(Audited::__fxstatat64) , false } }
#if !NEED_STAT_WRAPPERS
	,	{ "stat"      , { reinterpret_cast<uintptr_t>(Audited::stat     ) , false } }
	,	{ "stat64"    , { reinterpret_cast<uintptr_t>(Audited::stat64   ) , false } }
	,	{ "lstat"     , { reinterpret_cast<uintptr_t>(Audited::lstat    ) , false } }
	,	{ "lstat64"   , { reinterpret_cast<uintptr_t>(Audited::lstat64  ) , false } }
	,	{ "fstatat"   , { reinterpret_cast<uintptr_t>(Audited::fstatat  ) , false } }
	,	{ "fstatat64" , { reinterpret_cast<uintptr_t>(Audited::fstatat64) , false } }
#endif
,	{ "realpath"               , { reinterpret_cast<uintptr_t>(Audited::realpath              ) , false } }
,	{ "__realpath_chk"         , { reinterpret_cast<uintptr_t>(Audited::__realpath_chk        ) , false } }
,	{ "canonicalize_file_name" , { reinterpret_cast<uintptr_t>(Audited::canonicalize_file_name) , false } }
,	{ "scandir"                , { reinterpret_cast<uintptr_t>(Audited::scandir               ) , false } }
,	{ "scandir64"              , { reinterpret_cast<uintptr_t>(Audited::scandir64             ) , false } }
,	{ "scandirat"              , { reinterpret_cast<uintptr_t>(Audited::scandirat             ) , false } }
,	{ "scandirat64"            , { reinterpret_cast<uintptr_t>(Audited::scandirat64           ) , false } }
} ;

template<class Sym> static inline uintptr_t _la_symbind( Sym* sym , unsigned int /*ndx*/ , uintptr_t* /*ref_cook*/ , uintptr_t* def_cook , unsigned int* /*flags*/ , const char* sym_name ) {
	Audit::t_audit() ;                                                         // force Audit static init
	//
	if (g_force_orig) return sym->st_value ;                                   // avoid recursion loop
	if (!*def_cook  ) return sym->st_value ;                                   // cookie is used to identify libc
	//
	auto it = g_syscall_tab.find(sym_name) ;
	if (it==g_syscall_tab.end()) return sym->st_value ;
	//
	SymEntry const& entry = it->second ;
	SWEAR(Audit::s_lnk_support!=LnkSupport::Unknown) ;
	if (entry.data_access                     ) return entry.func    ;         // if not a stat-like syscall, we need to spy it
	if (Audit::s_lnk_support==LnkSupport::Full) return entry.func    ;         // we need to analyze uphill dirs
	if (!Audit::s_ignore_stat                 ) return entry.func    ;         // we need to generate deps for stat-like accesses
	/**/                                        return sym->st_value ;         // nothing to do, do not spy
}

#pragma GCC visibility push(default)
extern "C" {

	unsigned int la_version(unsigned int /*version*/) { return LAV_CURRENT ; }

	unsigned int la_objopen( struct link_map* map , Lmid_t lmid , uintptr_t *cookie ) {
		bool is_libc_ = is_libc(map->l_name) ;
		*cookie = is_libc_ ;
		if (is_libc_) {
			g_libc_lmid = lmid        ;                                        // seems more robust to avoid directly calling dlmopen while in a call-back due to opening a dl
			g_libc_name = map->l_name ;
		}
		return LA_FLG_BINDFROM | (is_libc_?LA_FLG_BINDTO:0) ;
	}

	char* la_objsearch( const char* name , uintptr_t* /*cookie*/ , unsigned int flag ) {
		switch (flag) {
			case LA_SER_ORIG    : if (strrchr(name,'/')) { Lock lock ; Audit::read(AT_FDCWD,name) ; } break ;
			case LA_SER_LIBPATH :
			case LA_SER_RUNPATH :                        { Lock lock ; Audit::read(AT_FDCWD,name) ; } break ;
			default : ;
		}
		return const_cast<char*>(name) ;
	}

	uintptr_t la_symbind64(Elf64_Sym* s,unsigned int n,uintptr_t* rc,uintptr_t* dc,unsigned int* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }
	uintptr_t la_symbind32(Elf32_Sym* s,unsigned int n,uintptr_t* rc,uintptr_t* dc,unsigned int* f,const char* sn) { return _la_symbind(s,n,rc,dc,f,sn) ; }

}
#pragma GCC visibility pop
