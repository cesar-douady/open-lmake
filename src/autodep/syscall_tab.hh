// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

struct SyscallDescr {
	static constexpr long NSyscalls = 1024 ;           // must larger than higher syscall number, 1024 is plenty, actual upper value is around 450
	using Tab = ::array<SyscallDescr,NSyscalls> ;      // must be an array and not an umap so as to avoid calls to malloc before it is known to be safe
	// static data
	static Tab const& s_tab ;
	// accesses
	constexpr bool operator+() const { return prio ; } // prio=0 means entry is not allocated
	// data
	// /!\ there must be no memory allocation nor cxtor/dxtor as this must be statically allocated when malloc is not available
	void           (*entry)( void*& , Record& , pid_t , uint64_t args[6] , const char* comment ) = nullptr ;
	int64_t/*res*/ (*exit )( void*  , Record& , pid_t , int64_t res                            ) = nullptr ;
	int            filter                                                                        = 0       ; // argument to filter on when known to require no processing
	uint8_t        prio                                                                          = 0       ; // prio for libseccomp (0 means entry is not allocated)
	bool           is_stat                                                                       = false   ;
	const char*    comment                                                                       = nullptr ;
} ;

#ifdef LD_PRELOAD
	#define ENUMERATE_LD_PRELOAD_LIBCALLS \
		/**/                    /*is_stat*/ \
		,	LIBCALL_ENTRY(dlmopen ,false) \
		,	LIBCALL_ENTRY(dlopen  ,false) \
		,	LIBCALL_ENTRY(putenv  ,false) \
		,	LIBCALL_ENTRY(setenv  ,false) \
		,	LIBCALL_ENTRY(unsetenv,false)
#else
	// handled by la_objopen (calling Audited::dlmopen does not work for a mysterious reason)
	#define ENUMERATE_LD_PRELOAD_LIBCALLS
#endif

#define ENUMERATE_DIRECT_STAT_LIBCALLS \

#if MAP_VFORK
	#define ENUMERATE_VFORK_LIBCALLS \
		/**/               /*is_stat*/ \
	,	LIBCALL_ENTRY(vfork  ,false) \
	,	LIBCALL_ENTRY(__vfork,false)
#else
	#define ENUMERATE_VFORK_LIBCALLS
#endif

//
// mere path accesses, no actual accesses to file data */
//
#if HAS_OFF64
	#define ENUMERATE_PATH_LIBCALLS_64 /*is_stat*/ \
	,	LIBCALL_ENTRY(fstatat64          ,true ) \
	,	LIBCALL_ENTRY(__fxstatat64       ,true ) \
	,	LIBCALL_ENTRY(lstat64            ,true ) \
	,	LIBCALL_ENTRY(__lxstat64         ,true ) \
	,	LIBCALL_ENTRY(scandir64          ,false) \
	,	LIBCALL_ENTRY(scandirat64        ,false) \
	,	LIBCALL_ENTRY(stat64             ,true ) \
	,	LIBCALL_ENTRY(__xstat64          ,true )
#else
	#define ENUMERATE_PATH_LIBCALLS_64
#endif
#define ENUMERATE_PATH_LIBCALLS       /*is_stat*/ \
,	LIBCALL_ENTRY(access                ,true ) \
,	LIBCALL_ENTRY(canonicalize_file_name,false) \
,	LIBCALL_ENTRY(faccessat             ,true ) \
,	LIBCALL_ENTRY(fstatat               ,true ) \
,	LIBCALL_ENTRY(__fxstatat            ,true ) \
,	LIBCALL_ENTRY(lstat                 ,true ) \
,	LIBCALL_ENTRY(__lxstat              ,true ) \
,	LIBCALL_ENTRY(mkdirat               ,false) \
,	LIBCALL_ENTRY(opendir               ,false) \
,	LIBCALL_ENTRY(realpath              ,false) \
,	LIBCALL_ENTRY(__realpath_chk        ,false) \
,	LIBCALL_ENTRY(scandir               ,false) \
,	LIBCALL_ENTRY(scandirat             ,false) \
,	LIBCALL_ENTRY(statx                 ,true ) \
,	LIBCALL_ENTRY(stat                  ,true ) \
,	LIBCALL_ENTRY(__xstat               ,true ) \
	ENUMERATE_PATH_LIBCALLS_64

#if HAS_OFF64
	#define ENUMERATE_LIBCALLS_64    /*is_stat*/ \
	,	LIBCALL_ENTRY(creat64          ,false) \
	,	LIBCALL_ENTRY(fopen64          ,false) \
	,	LIBCALL_ENTRY(freopen64        ,false) \
	,	LIBCALL_ENTRY(mkostemp64       ,false) \
	,	LIBCALL_ENTRY(mkostemps64      ,false) \
	,	LIBCALL_ENTRY(mkstemp64        ,false) \
	,	LIBCALL_ENTRY(mkstemps64       ,false) \
	,	LIBCALL_ENTRY(open64           ,false) \
	,	LIBCALL_ENTRY(__open64         ,false) \
	,	LIBCALL_ENTRY(__open64_nocancel,false) \
	,	LIBCALL_ENTRY(__open64_2       ,false) \
	,	LIBCALL_ENTRY(openat64         ,false) \
	,	LIBCALL_ENTRY(__openat64_2     ,false) \
	,	LIBCALL_ENTRY(truncate64       ,false)
#else
	#define ENUMERATE_LIBCALLS_64
#endif
#define ENUMERATE_LIBCALLS       /*is_stat*/ \
	LIBCALL_ENTRY(chdir            ,false) \
,	LIBCALL_ENTRY(chmod            ,false) \
,	LIBCALL_ENTRY(clone            ,false) \
,	LIBCALL_ENTRY(__clone2         ,false) \
,	LIBCALL_ENTRY(close            ,false) \
,	LIBCALL_ENTRY(__close          ,false) \
,	LIBCALL_ENTRY(creat            ,false) \
,	LIBCALL_ENTRY(dup2             ,false) \
,	LIBCALL_ENTRY(dup3             ,false) \
,	LIBCALL_ENTRY(execl            ,false) \
,	LIBCALL_ENTRY(execle           ,false) \
,	LIBCALL_ENTRY(execlp           ,false) \
,	LIBCALL_ENTRY(execv            ,false) \
,	LIBCALL_ENTRY(execve           ,false) \
,	LIBCALL_ENTRY(execveat         ,false) \
,	LIBCALL_ENTRY(execvp           ,false) \
,	LIBCALL_ENTRY(execvpe          ,false) \
,	LIBCALL_ENTRY(fchdir           ,false) \
,	LIBCALL_ENTRY(fchmodat         ,false) \
,	LIBCALL_ENTRY(fopen            ,false) \
,	LIBCALL_ENTRY(fork             ,false) \
,	LIBCALL_ENTRY(__fork           ,false) \
,	LIBCALL_ENTRY(freopen          ,false) \
,	LIBCALL_ENTRY(futimesat        ,false) \
,	LIBCALL_ENTRY(__libc_fork      ,false) \
,	LIBCALL_ENTRY(link             ,false) \
,	LIBCALL_ENTRY(linkat           ,false) \
,	LIBCALL_ENTRY(lutimes          ,false) \
,	LIBCALL_ENTRY(mkdir            ,false) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	LIBCALL_ENTRY(mkostemp         ,false) \
,	LIBCALL_ENTRY(mkostemps        ,false) \
,	LIBCALL_ENTRY(mkstemp          ,false) \
,	LIBCALL_ENTRY(mkstemps         ,false) \
,	LIBCALL_ENTRY(mount            ,false) \
,	LIBCALL_ENTRY(open             ,false) \
,	LIBCALL_ENTRY(__open           ,false) \
,	LIBCALL_ENTRY(__open_nocancel  ,false) \
,	LIBCALL_ENTRY(__open_2         ,false) \
,	LIBCALL_ENTRY(openat           ,false) \
,	LIBCALL_ENTRY(__openat_2       ,false) \
,	LIBCALL_ENTRY(readlink         ,false) \
,	LIBCALL_ENTRY(readlinkat       ,false) \
,	LIBCALL_ENTRY(__readlinkat_chk ,false) \
,	LIBCALL_ENTRY(__readlink_chk   ,false) \
,	LIBCALL_ENTRY(rename           ,false) \
,	LIBCALL_ENTRY(renameat         ,false) \
,	LIBCALL_ENTRY(renameat2        ,false) \
,	LIBCALL_ENTRY(rmdir            ,false) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	LIBCALL_ENTRY(symlink          ,false) \
,	LIBCALL_ENTRY(symlinkat        ,false) \
,	LIBCALL_ENTRY(syscall          ,false) \
,	LIBCALL_ENTRY(system           ,false) \
,	LIBCALL_ENTRY(truncate         ,false) \
,	LIBCALL_ENTRY(unlink           ,false) \
,	LIBCALL_ENTRY(unlinkat         ,false) \
,	LIBCALL_ENTRY(utime            ,false) \
,	LIBCALL_ENTRY(utimensat        ,false) \
,	LIBCALL_ENTRY(utimes           ,false) \
	ENUMERATE_LIBCALLS_64                  \
	ENUMERATE_LD_PRELOAD_LIBCALLS          \
	ENUMERATE_PATH_LIBCALLS                \
	ENUMERATE_VFORK_LIBCALLS
