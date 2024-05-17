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
	void           (*entry)( void*& , Record& , pid_t , uint64_t args[6] , const char* comment ) = nullptr ;
	int64_t/*res*/ (*exit )( void*  , Record& , pid_t , int64_t res                            ) = nullptr ;
	int            filter                                                                        = 0       ; // argument to filter on when known to require no processing
	uint8_t        prio                                                                          = 0       ; // prio for libseccomp (0 means entry is not allocated)
	bool           data_access                                                                   = false   ;
	const char*    comment                                                                       = nullptr ;
} ;

#ifdef LD_PRELOAD
	#define ENUMERATE_LD_PRELOAD_SYSCALLS \
		,	SYSCALL_ENTRY(dlmopen ) \
		,	SYSCALL_ENTRY(dlopen  ) \
		,	SYSCALL_ENTRY(putenv  ) \
		,	SYSCALL_ENTRY(setenv  ) \
		,	SYSCALL_ENTRY(unsetenv)
#else
	// handled by la_objopen (calling Audited::dlmopen does not work for a mysterious reason)
	#define ENUMERATE_LD_PRELOAD_SYSCALLS
#endif

#if NEED_STAT_WRAPPERS
	#define ENUMERATE_DIRECT_STAT_SYSCALLS
#else
	#define ENUMERATE_DIRECT_STAT_SYSCALLS \
	,	SYSCALL_ENTRY(fstatat  ) \
	,	SYSCALL_ENTRY(fstatat64) \
	,	SYSCALL_ENTRY(lstat    ) \
	,	SYSCALL_ENTRY(lstat64  ) \
	,	SYSCALL_ENTRY(stat     ) \
	,	SYSCALL_ENTRY(stat64   )
#endif

//
// mere path accesses, no actual accesses to file data */
//
#define ENUMERATE_PATH_SYSCALLS \
,	SYSCALL_ENTRY(access                ) \
,	SYSCALL_ENTRY(canonicalize_file_name) \
,	SYSCALL_ENTRY(faccessat             ) \
,	SYSCALL_ENTRY(__fxstatat            ) \
,	SYSCALL_ENTRY(__fxstatat64          ) \
,	SYSCALL_ENTRY(__lxstat              ) \
,	SYSCALL_ENTRY(__lxstat64            ) \
,	SYSCALL_ENTRY(mkdirat               ) \
,	SYSCALL_ENTRY(opendir               ) \
,	SYSCALL_ENTRY(realpath              ) \
,	SYSCALL_ENTRY(__realpath_chk        ) \
,	SYSCALL_ENTRY(scandir               ) \
,	SYSCALL_ENTRY(scandir64             ) \
,	SYSCALL_ENTRY(scandirat             ) \
,	SYSCALL_ENTRY(scandirat64           ) \
,	SYSCALL_ENTRY(statx                 ) \
,	SYSCALL_ENTRY(__xstat               ) \
,	SYSCALL_ENTRY(__xstat64             )

#define ENUMERATE_SYSCALLS \
	SYSCALL_ENTRY(chdir            ) \
,	SYSCALL_ENTRY(chmod            ) \
,	SYSCALL_ENTRY(clone            ) \
,	SYSCALL_ENTRY(close            ) \
,	SYSCALL_ENTRY(__close          ) \
,	SYSCALL_ENTRY(creat            ) \
,	SYSCALL_ENTRY(creat64          ) \
,	SYSCALL_ENTRY(dup2             ) \
,	SYSCALL_ENTRY(dup3             ) \
,	SYSCALL_ENTRY(execl            ) \
,	SYSCALL_ENTRY(execle           ) \
,	SYSCALL_ENTRY(execlp           ) \
,	SYSCALL_ENTRY(execv            ) \
,	SYSCALL_ENTRY(execve           ) \
,	SYSCALL_ENTRY(execveat         ) \
,	SYSCALL_ENTRY(execvp           ) \
,	SYSCALL_ENTRY(execvpe          ) \
,	SYSCALL_ENTRY(fchdir           ) \
,	SYSCALL_ENTRY(fchmodat         ) \
,	SYSCALL_ENTRY(fopen            ) \
,	SYSCALL_ENTRY(fopen64          ) \
,	SYSCALL_ENTRY(fork             ) \
,	SYSCALL_ENTRY(__fork           ) \
,	SYSCALL_ENTRY(freopen          ) \
,	SYSCALL_ENTRY(freopen64        ) \
,	SYSCALL_ENTRY(futimesat        ) \
,	SYSCALL_ENTRY(__libc_fork      ) \
,	SYSCALL_ENTRY(link             ) \
,	SYSCALL_ENTRY(linkat           ) \
,	SYSCALL_ENTRY(lutimes          ) \
,	SYSCALL_ENTRY(mkdir            ) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	SYSCALL_ENTRY(mkostemp         ) \
,	SYSCALL_ENTRY(mkostemp64       ) \
,	SYSCALL_ENTRY(mkostemps        ) \
,	SYSCALL_ENTRY(mkostemps64      ) \
,	SYSCALL_ENTRY(mkstemp          ) \
,	SYSCALL_ENTRY(mkstemp64        ) \
,	SYSCALL_ENTRY(mkstemps         ) \
,	SYSCALL_ENTRY(mkstemps64       ) \
,	SYSCALL_ENTRY(mount            ) \
,	SYSCALL_ENTRY(open             ) \
,	SYSCALL_ENTRY(__open           ) \
,	SYSCALL_ENTRY(__open_nocancel  ) \
,	SYSCALL_ENTRY(__open_2         ) \
,	SYSCALL_ENTRY(open64           ) \
,	SYSCALL_ENTRY(__open64         ) \
,	SYSCALL_ENTRY(__open64_nocancel) \
,	SYSCALL_ENTRY(__open64_2       ) \
,	SYSCALL_ENTRY(openat           ) \
,	SYSCALL_ENTRY(__openat_2       ) \
,	SYSCALL_ENTRY(openat64         ) \
,	SYSCALL_ENTRY(__openat64_2     ) \
,	SYSCALL_ENTRY(readlink         ) \
,	SYSCALL_ENTRY(readlinkat       ) \
,	SYSCALL_ENTRY(__readlinkat_chk ) \
,	SYSCALL_ENTRY(__readlink_chk   ) \
,	SYSCALL_ENTRY(rename           ) \
,	SYSCALL_ENTRY(renameat         ) \
,	SYSCALL_ENTRY(renameat2        ) \
,	SYSCALL_ENTRY(rmdir            ) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	SYSCALL_ENTRY(symlink          ) \
,	SYSCALL_ENTRY(symlinkat        ) \
,	SYSCALL_ENTRY(syscall          ) \
,	SYSCALL_ENTRY(system           ) \
,	SYSCALL_ENTRY(truncate         ) \
,	SYSCALL_ENTRY(truncate64       ) \
,	SYSCALL_ENTRY(unlink           ) \
,	SYSCALL_ENTRY(unlinkat         ) \
,	SYSCALL_ENTRY(utime            ) \
,	SYSCALL_ENTRY(utimensat        ) \
,	SYSCALL_ENTRY(utimes           ) \
,	SYSCALL_ENTRY(vfork            ) /* because vfork semantic does not allow instrumentation of following exec */ \
,	SYSCALL_ENTRY(__vfork          ) /* . */ \
	ENUMERATE_LD_PRELOAD_SYSCALLS      \
	ENUMERATE_PATH_SYSCALLS            \
	ENUMERATE_DIRECT_STAT_SYSCALLS
