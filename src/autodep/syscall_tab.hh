// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

struct SyscallDescr {
	static constexpr long NSyscalls = 1024 ;              // must larger than higher syscall number, 1024 is plenty, actual upper value is around 450
	using Tab = ::array<SyscallDescr,NSyscalls> ;         // must be an array and not an umap so as to avoid calls to malloc before it is known to be safe
	// static data
	static Tab const& s_tab() ;                           // ptrace does not support tmp mapping, which simplifies table a bit
	// accesses
	constexpr bool operator+() const { return prio    ; } // prio=0 means entry is not allocated
	constexpr bool operator!() const { return !+*this ; }
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
		,	LIBCALL_ENTRY(dlmopen ,false/*is_stat*/) \
		,	LIBCALL_ENTRY(dlopen  ,false/*is_stat*/) \
		,	LIBCALL_ENTRY(putenv  ,false/*is_stat*/) \
		,	LIBCALL_ENTRY(setenv  ,false/*is_stat*/) \
		,	LIBCALL_ENTRY(unsetenv,false/*is_stat*/)
#else
	// handled by la_objopen (calling Audited::dlmopen does not work for a mysterious reason)
	#define ENUMERATE_LD_PRELOAD_LIBCALLS
#endif

#if NEED_STAT_WRAPPERS
	#define ENUMERATE_DIRECT_STAT_LIBCALLS
#else
	#define ENUMERATE_DIRECT_STAT_LIBCALLS \
	,	LIBCALL_ENTRY(fstatat  ,true/*is_stat*/) \
	,	LIBCALL_ENTRY(fstatat64,true/*is_stat*/) \
	,	LIBCALL_ENTRY(lstat    ,true/*is_stat*/) \
	,	LIBCALL_ENTRY(lstat64  ,true/*is_stat*/) \
	,	LIBCALL_ENTRY(stat     ,true/*is_stat*/) \
	,	LIBCALL_ENTRY(stat64   ,true/*is_stat*/)
#endif

//
// mere path accesses, no actual accesses to file data */
//
#define ENUMERATE_PATH_LIBCALLS \
,	LIBCALL_ENTRY(access                ,true /*is_stat*/) \
,	LIBCALL_ENTRY(canonicalize_file_name,false/*is_stat*/) \
,	LIBCALL_ENTRY(faccessat             ,true /*is_stat*/) \
,	LIBCALL_ENTRY(__fxstatat            ,true /*is_stat*/) \
,	LIBCALL_ENTRY(__fxstatat64          ,true /*is_stat*/) \
,	LIBCALL_ENTRY(__lxstat              ,true /*is_stat*/) \
,	LIBCALL_ENTRY(__lxstat64            ,true /*is_stat*/) \
,	LIBCALL_ENTRY(mkdirat               ,false/*is_stat*/) \
,	LIBCALL_ENTRY(opendir               ,false/*is_stat*/) \
,	LIBCALL_ENTRY(realpath              ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__realpath_chk        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(scandir               ,false/*is_stat*/) \
,	LIBCALL_ENTRY(scandir64             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(scandirat             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(scandirat64           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(statx                 ,true /*is_stat*/) \
,	LIBCALL_ENTRY(__xstat               ,true /*is_stat*/) \
,	LIBCALL_ENTRY(__xstat64             ,true /*is_stat*/)

#define ENUMERATE_LIBCALLS \
	LIBCALL_ENTRY(chdir            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(chmod            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(clone            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(close            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__close          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(creat            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(creat64          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(dup2             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(dup3             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execl            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execle           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execlp           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execv            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execve           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execveat         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execvp           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(execvpe          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(fchdir           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(fchmodat         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(fopen            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(fopen64          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(fork             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__fork           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(freopen          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(freopen64        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(futimesat        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__libc_fork      ,false/*is_stat*/) \
,	LIBCALL_ENTRY(link             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(linkat           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(lutimes          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkdir            ,false/*is_stat*/) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	LIBCALL_ENTRY(mkostemp         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkostemp64       ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkostemps        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkostemps64      ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkstemp          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkstemp64        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkstemps         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mkstemps64       ,false/*is_stat*/) \
,	LIBCALL_ENTRY(mount            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(open             ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__open           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__open_nocancel  ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__open_2         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(open64           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__open64         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__open64_nocancel,false/*is_stat*/) \
,	LIBCALL_ENTRY(__open64_2       ,false/*is_stat*/) \
,	LIBCALL_ENTRY(openat           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__openat_2       ,false/*is_stat*/) \
,	LIBCALL_ENTRY(openat64         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__openat64_2     ,false/*is_stat*/) \
,	LIBCALL_ENTRY(readlink         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(readlinkat       ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__readlinkat_chk ,false/*is_stat*/) \
,	LIBCALL_ENTRY(__readlink_chk   ,false/*is_stat*/) \
,	LIBCALL_ENTRY(rename           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(renameat         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(renameat2        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(rmdir            ,false/*is_stat*/) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	LIBCALL_ENTRY(symlink          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(symlinkat        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(syscall          ,false/*is_stat*/) \
,	LIBCALL_ENTRY(system           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(truncate         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(truncate64       ,false/*is_stat*/) \
,	LIBCALL_ENTRY(unlink           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(unlinkat         ,false/*is_stat*/) \
,	LIBCALL_ENTRY(utime            ,false/*is_stat*/) \
,	LIBCALL_ENTRY(utimensat        ,false/*is_stat*/) \
,	LIBCALL_ENTRY(utimes           ,false/*is_stat*/) \
,	LIBCALL_ENTRY(vfork            ,false/*is_stat*/) /* because vfork semantic does not allow instrumentation of following exec */ \
,	LIBCALL_ENTRY(__vfork          ,false/*is_stat*/) /* . */ \
	ENUMERATE_LD_PRELOAD_LIBCALLS                     \
	ENUMERATE_PATH_LIBCALLS                           \
	ENUMERATE_DIRECT_STAT_LIBCALLS
