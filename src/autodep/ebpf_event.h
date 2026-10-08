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
	using ebpf_u8  = uint8_t  ;
	using ebpf_u32 = uint32_t ;
	using ebpf_s32 = int32_t  ;
	using ebpf_u64 = uint64_t ;
	using ebpf_s64 = int64_t  ;
#else
	typedef __u8  ebpf_u8  ;
	typedef __u32 ebpf_u32 ;
	typedef __s32 ebpf_s32 ;
	typedef __u64 ebpf_u64 ;
	typedef __s64 ebpf_s64 ;
#endif

#define EBPF_PATH_MAX  4096              // max size of a captured string (PATH_MAX)
#define EBPF_DATA_SZ   (2*EBPF_PATH_MAX) // an event holds at most 2 strings
#define EBPF_MAX_ROOTS 16                // repo root + source dirs
#define EBPF_NSYSCALLS 512               // >= SyscallDescr::NSyscalls ; sizes the syscall descriptor table (power of 2)

// magic dir fds used as markers, any improbable negative value will do
// /!\ EBPF_MAGIC_FD* must be in sync with Backdoor::MagicFd*
#define EBPF_MAGIC_FD        (-200) // Backdoor::MagicFd       : a backdoor call, if it fails, it is processed by the job itself and its accesses must not be recorded ...
#define EBPF_MAGIC_FD_END    (-201) // Backdoor::MagicFdEnd    : ... until this marker
#define EBPF_MAGIC_FD_ENABLE (-202) // Backdoor::MagicFdEnable : set the autodep enable bit of the thread (carried by the size arg)
#define EBPF_REGISTER_FD     (-300) // the child self-registers as the root of a traced sub-tree (job_id carried by the size arg), so the tid is the one seen by the kernel
#define EBPF_ROOT_FD         (-301) // the daemon registers a root dir (cf. roots in ebpf.bpf.c)

// description of a syscall, so the kernel knows where to find paths when they have not been captured by a hook (i.e. when resolution failed)
// arg indices are on 3 bits, 7 means none
#define EBPF_SC_NONE  7u
#define EBPF_SC_VALID (1u<<31)
#define EBPF_SC( kind , dfd1 , path1 , dfd2 , path2 , fd , flags ) ( \
	EBPF_SC_VALID             \
|	((ebpf_u32)(dfd1 )&0x7)<<0  \
|	((ebpf_u32)(path1)&0x7)<<3  \
|	((ebpf_u32)(dfd2 )&0x7)<<6  \
|	((ebpf_u32)(path2)&0x7)<<9  \
|	((ebpf_u32)(fd   )&0x7)<<12 \
|	((ebpf_u32)(flags)&0x7)<<15 \
|	((ebpf_u32)(kind )&0xf)<<18 \
)
//
#define EBPF_SC_DFD1( d) ( ((d)>> 0) & 0x7 ) // its dir fd arg (none means AT_FDCWD)
#define EBPF_SC_PATH1(d) ( ((d)>> 3) & 0x7 ) // 1st path arg
#define EBPF_SC_DFD2( d) ( ((d)>> 6) & 0x7 ) // its dir fd arg (none means AT_FDCWD)
#define EBPF_SC_PATH2(d) ( ((d)>> 9) & 0x7 ) // 2nd path arg
#define EBPF_SC_FD(   d) ( ((d)>>12) & 0x7 ) // arg holding a fd designating the accessed dir (getdents)
#define EBPF_SC_FLAGS(d) ( ((d)>>15) & 0x7 ) // arg holding open flags
#define EBPF_SC_KIND( d) ( ((d)>>18) & 0xf )
enum EbpfScKind {
	EBPF_SCK_PLAIN                           // nothing special
,	EBPF_SCK_OPEN                            // returns a fd designating the accessed file, used when the open hook is not called (O_PATH)
,	EBPF_SCK_CREAT                           // like open with flags O_WRONLY|O_CREAT|O_TRUNC
,	EBPF_SCK_OPENAT2                         // like open, flags are in the open_how struct pointed to by the flags arg
,	EBPF_SCK_READLINK                        // may carry markers
,	EBPF_SCK_EXEC                            // args are meaningless upon success
,	EBPF_SCK_IO_URING                        // may execute io_uring operations, which are closed by their own End event
} ;

enum EbpfKind {
	// process tracking
	EBPF_PROC_START // a thread has been created in the job, nr is its parent
,	EBPF_PROC_END   // a thread has exited, captures not closed by an End event are meaningless
	// captures : a path (and the info of the file it designates before the syscall modifies it), relative to root if root<EBPF_MAX_ROOTS
,	EBPF_LINK       // a symlink has been traversed during path resolution , 2nd string is its target if EBPF_F_TARGET
,	EBPF_LOOKUP     // a path has been resolved (filename_lookup)          , this is all for stat-like syscalls
,	EBPF_OPEN       // a file has been opened                              , open_flags is meaningful
,	EBPF_CREATE     // a file is about to be created (mknod, mkdir, symlink, link), it does not exist
,	EBPF_UNLINK     // a file is about to be unlinked (unlink, rmdir)
,	EBPF_RENAME_SRC // a file is about to be renamed, this is the source
,	EBPF_RENAME_DST // .                              this is the destination
,	EBPF_DIR        // a dir is read (getdents)
,	EBPF_RAW        // a path could not be resolved : 1st string is the dir it is relative to (empty if absolute), 2nd string is the path as provided, nr is the index of the path (0 or 1)
	// end of syscall
,	EBPF_END        // closes all the captures above for the same tid
} ;

#define EBPF_ROOT_ABS     0xff // path is not relative to a root but absolute, as seen by the job (i.e. relative to its root dir)
#define EBPF_ROOT_EXCLUDE 0xfe // not reported : what lies in such a dir is not of interest, although it may lie in a root

// flags
#define EBPF_F_EXISTS  0x01 // file exists, info fields are meaningful
#define EBPF_F_CREATED 0x02 // file has been created by the open
#define EBPF_F_TARGET  0x04 // link target is present
#define EBPF_F_IO      0x08 // End : this is the end of an io_uring operation, nr is its opcode (IORING_OP_*)
#define EBPF_F_DISCARD 0x10 // End : captures must be discarded, the syscall will be restarted

// only accesses lying in a root (the repo or a source dir) are of interest
// roots are identified by their dentry, so recognizing them is independent of the way they are seen by the job (chroot, bind mounts, ...)
struct RootKey {
	ebpf_u64 dentry ;
	ebpf_u32 job_id ;
	ebpf_u32 pad    ;
} ;

struct JobCfg {
	ebpf_u64 pidns_dev      ; // (dev,ino) of the client's pid namespace, so we can report tids as it sees them
	ebpf_u64 pidns_ino      ;
	ebpf_u64 seq            ; // number of events emitted (or tried)
	ebpf_u64 lost           ; // number of events lost
	ebpf_u32 deps_in_system ; // if 0, system files are simple and dropped
} ;

struct RootReg {      // the daemon registers a root by doing a lookup with EBPF_ROOT_FD while this is armed
	ebpf_u32 job_id ;
	ebpf_u32 idx    ;
	ebpf_u32 armed  ;
	ebpf_u32 done   ;
	ebpf_u64 dentry ; // out : the key under which root has been registered, so that it can be forgotten
} ;

struct EbpfEvent {
	ebpf_u64 seq                ; // per job, increasing with no hole : a hole means an event has been lost
	ebpf_u32 tid                ; // thread   id in the client's pid namespace
	ebpf_u32 pid                ; // process id in the client's pid namespace
	ebpf_u8  kind               ;
	ebpf_u8  root               ; // captures : index of the root the path is relative to
	ebpf_u8  flags              ;
	ebpf_u8  is32               ; // End      : the syscall used the 32-bit ABI
	ebpf_s32 nr                 ; // End      : syscall number
	// end of syscall
	ebpf_s64 rc                 ; // End      : syscall result (negative means -errno)
	ebpf_u64 args[6]            ; // End      : syscall args
	ebpf_u64 stamp_ns           ; // End      : date (CLOCK_TAI), 0 if not available
	// file info
	ebpf_u32 mode               ; // captures : st_mode  if EBPF_F_EXISTS
	ebpf_u32 open_flags         ; // Open,End : open flags, Rename_src,Rename_dst : st_mode of the other file (0 if it does not exist) as it is not reported if not of interest
	ebpf_u64 size               ; // captures : st_size  if EBPF_F_EXISTS
	ebpf_s64 mtime_s            ; // captures : st_mtime if EBPF_F_EXISTS
	ebpf_u32 mtime_ns           ; // .
	// strings
	ebpf_u32 len [2           ] ; // number of bytes of each string, they are stored back-to-back in data
	ebpf_u8  data[EBPF_DATA_SZ] ; // paths resolved by the kernel are stored as their components, leaf first, each followed by a null (the root dir has no component)
} ;
#define EBPF_HDR_SZ ( (ebpf_u32)__builtin_offsetof(struct EbpfEvent,data) )

#endif
