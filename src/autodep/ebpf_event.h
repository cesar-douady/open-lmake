// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// protocol shared between the BPF program (src/autodep/ebpf.bpf.c), the root daemon (src/autodep/ebpf_daemon.cc) and the job side consumer (src/autodep/ebpf.cc)
// /!\ plain C, fixed width types only : it must compile both as BPF (clang) and as C++
//
// principle :
// - everything the consumer needs is captured in the kernel at access time : resolved path, file info before the access, traversed symlinks
// - the consumer never looks at the disk nor at /proc to understand an access, so it may run late w.r.t. the job without consequences
// - while a syscall (or an io_uring operation) executes, hooks emit capture events, the syscall is then closed by an End event which carries what is necessary to
//   interpret the captures (syscall number, result, args)
// - captures and their End event are associated through tid, End.seq is the unique increasing id of the syscall
// - files that cannot be deps nor targets (not under the repo nor a source dir) are filtered out in the kernel

#ifndef EBPF_EVENT_H
#define EBPF_EVENT_H

#ifdef __cplusplus
	#include <errno.h>
	#include <fcntl.h>
	#include <limits.h>
	#include <linux/stat.h>
	#include <linux/unistd.h>
	#include <stdint.h>
	#include <stdio.h>
#endif

typedef uint16_t EbpfJobId  ;
typedef uint8_t  EbpfRootId ;

#define EBPF_AT_FDCWD            -100
#define EBPF_AT_REMOVEDIR        0x200
#define EBPF_AT_SYMLINK_FOLLOW   0x400
#define EBPF_AT_SYMLINK_NOFOLLOW 0x100
#define EBPF_EAGAIN               11
#define EBPF_EINTR                 4
#define EBPF_NAME_MAX             255
#define EBPF_O_ACCMODE           000000003
#define EBPF_O_CREAT             000000100
#define EBPF_O_DIRECTORY         000200000
#define EBPF_O_EXCL              000000200
#define EBPF_O_NOFOLLOW          000400000
#define EBPF_O_PATH              010000000
#define EBPF_O_RDONLY            000000000
#define EBPF_O_TMPFILE           020200000
#define EBPF_O_TRUNC             000001000
#define EBPF_PATH_MAX            4096
#define EBPF_RENAME_EXCHANGE       0x2
#define EBPF_STATX_MODE            0x2u
#define EBPF_STATX_SIZE          0x200u
#define EBPF_STATX_TYPE            0x1u
#define EBPF_S_IFDIR             0040000
#define EBPF_S_IFMT              0170000
#define EBPF_S_IFLNK             0120000
#define EBPF_S_IFREG             0100000
#define EBPF_S_IXALL             0000111
#define EBPF_X32_SYSCALL_BIT     0x40000000 // syscall is done with the x32 ABI

#ifdef __cplusplus
	static_assert( EBPF_AT_FDCWD            == AT_FDCWD            ) ;
	static_assert( EBPF_AT_REMOVEDIR        == AT_REMOVEDIR        ) ;
	static_assert( EBPF_AT_SYMLINK_FOLLOW   == AT_SYMLINK_FOLLOW   ) ;
	static_assert( EBPF_AT_SYMLINK_NOFOLLOW == AT_SYMLINK_NOFOLLOW ) ;
	static_assert( EBPF_EAGAIN              == EAGAIN              ) ;
	static_assert( EBPF_EINTR               == EINTR               ) ;
	static_assert( EBPF_NAME_MAX            == NAME_MAX            ) ;
	static_assert( EBPF_O_ACCMODE           == O_ACCMODE           ) ;
	static_assert( EBPF_O_CREAT             == O_CREAT             ) ;
	static_assert( EBPF_O_DIRECTORY         == O_DIRECTORY         ) ;
	static_assert( EBPF_O_EXCL              == O_EXCL              ) ;
	static_assert( EBPF_O_NOFOLLOW          == O_NOFOLLOW          ) ;
	static_assert( EBPF_O_PATH              == O_PATH              ) ;
	static_assert( EBPF_O_RDONLY            == O_RDONLY            ) ;
	static_assert( EBPF_O_TMPFILE           == O_TMPFILE           ) ;
	static_assert( EBPF_O_TRUNC             == O_TRUNC             ) ;
	static_assert( EBPF_PATH_MAX            == PATH_MAX            ) ;
	#ifdef RENAME_EXCHANGE
		static_assert( EBPF_RENAME_EXCHANGE == RENAME_EXCHANGE ) ;
	#endif
	#if defined(STATX_MODE) && defined(STATX_SIZ) && defined(STAX_TYPE)
		static_assert( EBPF_STATX_MODE == STATX_MODE ) ;
		static_assert( EBPF_STATX_SIZE == STATX_SIZE ) ;
		static_assert( EBPF_STATX_TYPE == STATX_TYPE ) ;
	#endif
	static_assert( EBPF_S_IFDIR         == S_IFDIR                   ) ;
	static_assert( EBPF_S_IFMT          == S_IFMT                    ) ;
	static_assert( EBPF_S_IFLNK         == S_IFLNK                   ) ;
	static_assert( EBPF_S_IFREG         == S_IFREG                   ) ;
	static_assert( EBPF_S_IXALL         == (S_IXUSR|S_IXGRP|S_IXOTH) ) ;
	static_assert( EBPF_X32_SYSCALL_BIT == __X32_SYSCALL_BIT         ) ;
#else
	#define AT_FDCWD            EBPF_AT_FDCWD
	#define AT_REMOVEDIR        EBPF_AT_REMOVEDIR
	#define AT_SYMLINK_FOLLOW   EBPF_AT_SYMLINK_FOLLOW
	#define AT_SYMLINK_NOFOLLOW EBPF_AT_SYMLINK_NOFOLLOW
	#define EAGAIN              EBPF_EAGAIN
	#define EINTR               EBPF_EINTR
	#define NAME_MAX            EBPF_NAME_MAX
	#define O_ACCMODE           EBPF_O_ACCMODE
	#define O_CREAT             EBPF_O_CREAT
	#define O_DIRECTORY         EBPF_O_DIRECTORY
	#define O_EXCL              EBPF_O_EXCL
	#define O_NOFOLLOW          EBPF_O_NOFOLLOW
	#define O_PATH              EBPF_O_PATH
	#define O_RDONLY            EBPF_O_RDONLY
	#define O_TMPFILE           EBPF_O_TMPFILE
	#define O_TRUNC             EBPF_O_TRUNC
	#define PATH_MAX            EBPF_PATH_MAX
	#define RENAME_EXCHANGE     EBPF_RENAME_EXCHANGE
	#define STATX_MODE          EBPF_STATX_MODE
	#define STATX_SIZE          EBPF_STATX_SIZE
	#define STATX_TYPE          EBPF_STATX_TYPE
	#define S_IFDIR             EBPF_S_IFDIR
	#define S_IFMT              EBPF_S_IFMT
	#define S_IFLNK             EBPF_S_IFLNK
	#define S_IFREG             EBPF_S_IFREG
	#define S_IXALL             EBPF_S_IXALL
	#define X32_SYSCALL_BIT     EBPF_X32_SYSCALL_BIT
#endif

typedef uint32_t EbpfScEntry ;
#define EBPF_SCE_NO_ARG 7u
#define EBPF_SCE( kind , dfd1 , path1 , dfd2 , path2 , fd , mod , flags , mask , nf ) ( \
	( (EbpfScEntry)((EBPF_SC_##kind)&0xf) <<  0 ) \
|	( (EbpfScEntry)((dfd1          )&0x7) <<  4 ) \
|	( (EbpfScEntry)((path1         )&0x7) <<  7 ) \
|	( (EbpfScEntry)((dfd2          )&0x7) << 10 ) \
|	( (EbpfScEntry)((path2         )&0x7) << 13 ) \
|	( (EbpfScEntry)((fd            )&0x7) << 16 ) \
|	( (EbpfScEntry)((mod           )&0x7) << 19 ) \
|	( (EbpfScEntry)((flags         )&0x7) << 22 ) \
|	( (EbpfScEntry)((mask          )&0x7) << 25 ) \
|	( (EbpfScEntry)((nf            )&0x3) << 28 ) \
|	( (EbpfScEntry)1                      << 30 ) \
)
//
#define EBPF_SCE_KIND( d) ((uint8_t)( ((d)>> 0) & 0xf ))
#define EBPF_SCE_DFD1( d) ((uint8_t)( ((d)>> 4) & 0x7 )) // 1st dir fd arg (none means AT_FDCWD)
#define EBPF_SCE_PATH1(d) ((uint8_t)( ((d)>> 7) & 0x7 )) // 1st path   arg
#define EBPF_SCE_DFD2( d) ((uint8_t)( ((d)>>10) & 0x7 )) // 2nd dir fd arg (none means AT_FDCWD)
#define EBPF_SCE_PATH2(d) ((uint8_t)( ((d)>>13) & 0x7 )) // 2nd path   arg
#define EBPF_SCE_FD(   d) ((uint8_t)( ((d)>>16) & 0x7 )) // fd         arg
#define EBPF_SCE_MOD(  d) ((uint8_t)( ((d)>>19) & 0x7 )) // mode       arg
#define EBPF_SCE_FLAGS(d) ((uint8_t)( ((d)>>22) & 0x7 )) // flags      arg
#define EBPF_SCE_MASK( d) ((uint8_t)( ((d)>>25) & 0x7 )) // mask       arg
#define EBPF_SCE_NF(   d) ((uint8_t)( ((d)>>28) & 0x3 )) // no_follow

enum EbpfSyscallKind {
	EBPF_SC_NONE = 0
,	EBPF_SC_CREATE
,	EBPF_SC_FORBIDDEN
,	EBPF_SC_LINK      // hard link, data[0] : source, data[1] : dest
,	EBPF_SC_OPEN
,	EBPF_SC_PATH      // just analyze PATH, no access to file
,	EBPF_SC_READDIR
,	EBPF_SC_READLINK
,	EBPF_SC_RENAME    // data[0] : source, data[1] : dest
,	EBPF_SC_STAT
,	EBPF_SC_UNLINK
,	EBPF_SC_UPDATE
} ;

#define EBPF_MAGIC_FD -200

#ifdef __cplusplus
	static_assert( EBPF_MAGIC_FD<-1 && EBPF_MAGIC_FD!=AT_FDCWD ) ; // check MAGIC_FD can be distingued with no ambiguity
#endif

enum EbpfMagicProc {
	EBPF_MAGIC_REGISTER // from tracer : register as job tracer
,	EBPF_MAGIC_ROOT     // from tracer : add a root dir to job
,	EBPF_MAGIC_JOB      // from tracee : identify top level job thread
,	EBPF_MAGIC_DISABLE  // from tracee : temporarily disable tracking
,	EBPF_MAGIC_ENABLE   // from tracee : re-enable tracking
} ;

typedef struct {
	enum EbpfMagicProc proc           ;
	uint64_t           mrkr           ; // if proc=EBPF_MAGIC_REGISTER, ensure job_exec speaks to the right filter
	EbpfJobId          job_id         ; // if proc=EBPF_MAGIC_REGISTER
	bool               deps_in_system ; // if proc=EBPF_MAGIC_REGISTER
	EbpfRootId         root_id        ; // if proc=EBPF_MAGIC_ROOT
	int                root_fd        ; // if proc=EBPF_MAGIC_ROOT
} EbpfMagicReq ;

enum EbpfEventKind {
	EBPF_NONE
	// process tracking
,	EBPF_JOB_START             // job has exited
,	EBPF_PROC_START            // a process has been created in the job (implies EBPF_THREAD_START)
,	EBPF_THREAD_START          // a thread  has been created in the job
,	EBPF_THREAD_END            // a thread  has exited
,	EBPF_PROC_END              // a process has exited                  (implies EBPF_THREAD_EXIT )
,	EBPF_JOB_END               // job has exited
,	EBPF_PANIC                 // an error occurred, reason is in data
	// 2 files events
,	EBPF_TARGET                // targets are sent before main message
	// path resolution
,	EBPF_PATH_RES              // a symlink has been traversed during path resolution , 2nd string is its target
	// accesses
,	EBPF_READ
,	EBPF_WRITE                 // may also read
,	EBPF_UNLINK                // .
,	EBPF_LINK                  // hard link
,	EBPF_RENAME
,	EBPF_EXCHANGE
,	EBPF_READDIR
// only in syscall table
	// aliases
,	EBPF_HAS_DATA = EBPF_PANIC // <EBPF_HAS_DATA means event has only hdr
} ;

enum EbpfFileKind {
	EBPF_FILE_NONE                // no info        (not emitted)
,	EBPF_FILE_IGNORE              // file is simple (not emitted)
,	EBPF_FILE_USER                // file is not resolved
,	EBPF_FILE_NO_FILE             // file is not regular nor sym link
,	EBPF_FILE_DIR                 // file is a dir (only for rename)
,	EBPF_FILE_LNK                 // file is a sym link
,	EBPF_FILE_REG                 // file is a regular, not executable
,	EBPF_FILE_EXE                 // file is regular, executable
	// aliases
,	EBPF_FILE_EMIT=EBPF_FILE_USER // if >=, event can be emitted
} ;

enum EbpfFlag {
	EBPF_F_STOPPED   = 1<<0                                        // thread was stopped and must be continued by userland after analysis
	// only for accesses
,	EBPF_F_NO_FOLLOW = 1<<1                                        // only for EBPF_FILE_USER, trailing symlink is not followed
,	EBPF_F_DEP_LNK   = 1<<2                                        // access dep as readlink
,	EBPF_F_DEP_REG   = 1<<3                                        // access dep as open
,	EBPF_F_DEP_STAT  = 1<<4                                        // sense dep existence
,	EBPF_F_OK        = 1<<5
	// aliases
,	EBPF_F_DEP_ALL = EBPF_F_DEP_LNK|EBPF_F_DEP_REG|EBPF_F_DEP_STAT // access all aspects of dep, such as with stat
} ;

typedef struct {                              // thread management messages only need this header
	uint32_t tid       ;                      // thread  id in the client's pid namespace
	uint8_t  kind      ;                      // enum : EbpfKind
	uint8_t  file_kind ;                      // enum : EbpfFileKind
	uint8_t  flags     ;                      // only for accesses (here for layout optimization)
	uint8_t  n_lnks    ;                      // only for accesses (here for layout optimization) : number of symlinks traversed during path resolution, max 80 (40 for each file)
} EbpfEventHdr ;
typedef struct {
	EbpfEventHdr hdr      ;
	uint64_t     stamp_ns ;
	uint64_t     size     ;                   // file size
	uint64_t     mtime_ns ;                   // st_mtime*10^9+st_mtime_ns
} EbpfEventBase ;
typedef struct {
	EbpfEventBase base                      ;
	char          data[PATH_MAX+NAME_MAX+1] ; //we must take some margin to please verifier when assembling components
} EbpfEvent ;

static inline bool _open_dir      (int flags) { return  (flags&O_DIRECTORY) || (  (flags&O_TMPFILE)                                ) ; }
static inline bool _open_no_follow(int flags) { return  (flags&O_NOFOLLOW ) || (  (flags&O_CREAT  ) && (flags&O_EXCL)              ) ; }
static inline bool _open_do_stat  (int flags) { return  (flags&O_PATH     ) || ( !(flags&O_CREAT  ) || (flags&O_EXCL)              ) ; } // sensitive to existence
static inline bool _open_do_read  (int flags) { return !(flags&O_PATH     ) && ( !(flags&O_TRUNC  )                                ) ; } // even if we cant read, file may depend on previous content
static inline bool _open_do_write (int flags) { return !(flags&O_PATH     ) && (  (flags&O_TRUNC  ) || (flags&O_ACCMODE)!=O_RDONLY ) ; }

#endif
