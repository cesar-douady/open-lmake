// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

#include "rpc_job_exec.hh"

struct SyscallDescr {
	static constexpr long NSyscalls = 440 ;                                                            // must larger than higher syscall number
	using Tab = ::array<SyscallDescr,NSyscalls> ;                                                      // must be an array and not an umap so as to avoid calls to malloc before it is known to be safe
	// static data
	static Tab const& s_tab ;
	// accesses
	constexpr bool operator+() const { return entry || exit ; }
	// data
	// /!\ there must be no memory allocation nor cxtor/dxtor as this must be statically allocated when malloc is not available
	void           (*entry)( void*& , Record& , pid_t , uint64_t args[6] , Comment ) = nullptr       ;
	int64_t/*res*/ (*exit )( void*  , Record& , pid_t , int64_t res                ) = nullptr       ;
	int            filter                                                            = 0             ; // argument to filter out when known to require no processing
	Comment        comment                                                           = Comment::None ;
} ;

#ifdef LD_PRELOAD
	#define ENUMERATE_LD_PRELOAD_LIBCALLS \
		,	LIBCALL_ENTRY(dlmopen ) \
		,	LIBCALL_ENTRY(dlopen  ) \
		,	LIBCALL_ENTRY(putenv  ) \
		,	LIBCALL_ENTRY(setenv  ) \
		,	LIBCALL_ENTRY(unsetenv)
#else
	// handled by la_objopen (calling Audited::dlmopen does not work for a mysterious reason)
	#define ENUMERATE_LD_PRELOAD_LIBCALLS
#endif

//
// mere path accesses, no actual accesses to file data */
//
#if LIBC_MAP_STAT
	// on some systems (e.g. centos7), libc does not define stat (&co) syscalls, and if present, they may be used (seen when open-lmake compiled with -fno-inline)
	// on such systems, it is important not to define these entries for an yet obscure reason
	#define ENUMERATE_PATH_STATS
#else
	#define ENUMERATE_PATH_STATS \
	,	LIBCALL_ENTRY(stat     ) \
	,	LIBCALL_ENTRY(lstat    ) \
	,	LIBCALL_ENTRY(fstatat  ) \
	,	LIBCALL_ENTRY(stat64   ) \
	,	LIBCALL_ENTRY(fstatat64) \
	,	LIBCALL_ENTRY(lstat64  )
#endif
#define ENUMERATE_PATH_LIBCALLS \
,	LIBCALL_ENTRY(access                ) \
,	LIBCALL_ENTRY(canonicalize_file_name) \
,	LIBCALL_ENTRY(faccessat             ) \
,	LIBCALL_ENTRY(mkdirat               ) \
,	LIBCALL_ENTRY(opendir               ) \
,	LIBCALL_ENTRY(realpath              ) \
,	LIBCALL_ENTRY(__realpath_chk        ) \
,	LIBCALL_ENTRY(scandir               ) \
,	LIBCALL_ENTRY(scandirat             ) \
,	LIBCALL_ENTRY(scandir64             ) \
,	LIBCALL_ENTRY(scandirat64           ) \
,	LIBCALL_ENTRY(statx                 ) \
\
,	LIBCALL_ENTRY(__xstat               ) \
,	LIBCALL_ENTRY(__fxstatat            ) \
,	LIBCALL_ENTRY(__lxstat              ) \
,	LIBCALL_ENTRY(__xstat64             ) \
,	LIBCALL_ENTRY(__fxstatat64          ) \
,	LIBCALL_ENTRY(__lxstat64            ) \
	ENUMERATE_PATH_STATS

#if HAS_CLOSE_RANGE
	#define ENUMERATE_CLOSE_RANGE_LIBCALLS \
	,	LIBCALL_ENTRY(close_range)
#else
	#define ENUMERATE_CLOSE_RANGE_LIBCALLS
#endif

#define ENUMERATE_DIR_LIBCALLS \
,	LIBCALL_ENTRY(getdents64     ) \
,	LIBCALL_ENTRY(getdirentries  ) \
,	LIBCALL_ENTRY(getdirentries64) \
,	LIBCALL_ENTRY(glob           ) \
,	LIBCALL_ENTRY(glob64         ) \
,	LIBCALL_ENTRY(readdir        ) \
,	LIBCALL_ENTRY(readdir64      ) \
,	LIBCALL_ENTRY(readdir_r      ) \
,	LIBCALL_ENTRY(readdir64_r    )

#define ENUMERATE_LIBCALLS \
	LIBCALL_ENTRY(chdir            ) \
,	LIBCALL_ENTRY(chmod            ) \
,	LIBCALL_ENTRY(chroot           ) \
,	LIBCALL_ENTRY(clone            ) \
,	LIBCALL_ENTRY(__clone2         ) \
,	LIBCALL_ENTRY(close            ) \
,	LIBCALL_ENTRY(__close          ) \
,	LIBCALL_ENTRY(creat            ) \
,	LIBCALL_ENTRY(dup2             ) \
,	LIBCALL_ENTRY(dup3             ) \
,	LIBCALL_ENTRY(execl            ) \
,	LIBCALL_ENTRY(execle           ) \
,	LIBCALL_ENTRY(execlp           ) \
,	LIBCALL_ENTRY(execv            ) \
,	LIBCALL_ENTRY(execve           ) \
,	LIBCALL_ENTRY(execveat         ) \
,	LIBCALL_ENTRY(execvp           ) \
,	LIBCALL_ENTRY(execvpe          ) \
,	LIBCALL_ENTRY(fchdir           ) \
,	LIBCALL_ENTRY(fchmodat         ) \
,	LIBCALL_ENTRY(fopen            ) \
,	LIBCALL_ENTRY(fork             ) \
,	LIBCALL_ENTRY(__fork           ) \
,	LIBCALL_ENTRY(freopen          ) \
,	LIBCALL_ENTRY(futimesat        ) \
,	LIBCALL_ENTRY(__libc_fork      ) \
,	LIBCALL_ENTRY(link             ) \
,	LIBCALL_ENTRY(linkat           ) \
,	LIBCALL_ENTRY(lutimes          ) \
,	LIBCALL_ENTRY(mkdir            ) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	LIBCALL_ENTRY(mkostemp         ) \
,	LIBCALL_ENTRY(mkostemps        ) \
,	LIBCALL_ENTRY(mkstemp          ) \
,	LIBCALL_ENTRY(mkstemps         ) \
,	LIBCALL_ENTRY(mount            ) \
,	LIBCALL_ENTRY(open             ) \
,	LIBCALL_ENTRY(__open           ) \
,	LIBCALL_ENTRY(__open_nocancel  ) \
,	LIBCALL_ENTRY(__open_2         ) \
,	LIBCALL_ENTRY(openat           ) \
,	LIBCALL_ENTRY(__openat_2       ) \
,	LIBCALL_ENTRY(readlink         ) \
,	LIBCALL_ENTRY(readlinkat       ) \
,	LIBCALL_ENTRY(__readlinkat_chk ) \
,	LIBCALL_ENTRY(__readlink_chk   ) \
,	LIBCALL_ENTRY(rename           ) \
,	LIBCALL_ENTRY(renameat         ) \
,	LIBCALL_ENTRY(renameat2        ) \
,	LIBCALL_ENTRY(rmdir            ) /* necessary against NFS strange notion of coherence as this touches containing dir */ \
,	LIBCALL_ENTRY(symlink          ) \
,	LIBCALL_ENTRY(symlinkat        ) \
,	LIBCALL_ENTRY(syscall          ) \
,	LIBCALL_ENTRY(system           ) \
,	LIBCALL_ENTRY(truncate         ) \
,	LIBCALL_ENTRY(unlink           ) \
,	LIBCALL_ENTRY(unlinkat         ) \
,	LIBCALL_ENTRY(utime            ) \
,	LIBCALL_ENTRY(utimensat        ) \
,	LIBCALL_ENTRY(utimes           ) \
\
,	LIBCALL_ENTRY(creat64          ) \
,	LIBCALL_ENTRY(fopen64          ) \
,	LIBCALL_ENTRY(freopen64        ) \
,	LIBCALL_ENTRY(mkostemp64       ) \
,	LIBCALL_ENTRY(mkostemps64      ) \
,	LIBCALL_ENTRY(mkstemp64        ) \
,	LIBCALL_ENTRY(mkstemps64       ) \
,	LIBCALL_ENTRY(open64           ) \
,	LIBCALL_ENTRY(__open64         ) \
,	LIBCALL_ENTRY(__open64_nocancel) \
,	LIBCALL_ENTRY(__open64_2       ) \
,	LIBCALL_ENTRY(openat64         ) \
,	LIBCALL_ENTRY(__openat64_2     ) \
,	LIBCALL_ENTRY(truncate64       ) \
\
	ENUMERATE_LD_PRELOAD_LIBCALLS    \
	ENUMERATE_PATH_LIBCALLS          \
	ENUMERATE_CLOSE_RANGE_LIBCALLS   \
	ENUMERATE_DIR_LIBCALLS
