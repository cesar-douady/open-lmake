// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// event layout shared between the BPF program (src/autodep/ebpf.bpf.c) and the user-space consumer (src/autodep/ebpf.cc)
// /!\ plain C, fixed width types only : it must compile both as BPF (clang) and as C++ (included by ebpf.cc)

#ifndef EBPF_EVENT_H
#define EBPF_EVENT_H

#ifdef __cplusplus
	#include <cstdint>
	using ebpf_u8  = uint8_t  ;
	using ebpf_u32 = uint32_t ;
	using ebpf_u64 = uint64_t ;
	using ebpf_s64 = int64_t  ;
#else
	typedef unsigned char      ebpf_u8  ;
	typedef unsigned int       ebpf_u32 ;
	typedef unsigned long long ebpf_u64 ;
	typedef long long          ebpf_s64 ;
#endif

#define EBPF_BLOB_MAX  4096 // enough for a full PATH_MAX path or an open_how struct
#define EBPF_MAX_BLOBS 3    // at most 2 paths (rename, link) + 1 open_how (openat2)

// the child self-registers into the traced set from kernel context (so the key is always in the namespace bpf_get_current_pid_tgid() uses, avoiding pid-namespace mismatches e.g. under WSL) :
// prepare_child issues readlinkat(EBPF_REGISTER_FD,"",buf,job_id) and the sys_enter program registers the current task (with that job_id) on seeing it
#define EBPF_REGISTER_FD (-300)

// unix-socket protocol between the root daemon (loads/attaches the shared programs) and the unprivileged job clients (job_exec / lautodep)
#define EBPF_SOCK_ENV  "LMAKE_EBPF_SOCK"      // env var to override the socket path
#define EBPF_SOCK_DFLT "/tmp/lmake_bpfd.sock" // default socket path
#define EBPF_NSYSCALLS 512                    // >= SyscallDescr::NSyscalls ; sizes the `wanted` filter (power of 2)

struct ebpf_hello {                   // client -> daemon : sent once at connect
	ebpf_u64 pidns_dev              ; // the job's pid namespace (dev,ino of /proc/self/ns/pid), so reported tids are resolvable by the client
	ebpf_u64 pidns_ino              ;
	ebpf_u32 deps_in_system         ; // if 0, system files (/usr, /lib, /etc, ...) are "simple" and dropped in-kernel ; if 1, they may be deps and are kept
	ebpf_u32 n_wanted               ; // number of syscall numbers to record (owned by syscall_tab, uploaded so the daemon need not duplicate the list)
	ebpf_u32 wanted[EBPF_NSYSCALLS] ; // wanted[i] = syscall number to record (only the first n_wanted entries are meaningful)
} ;

struct ebpf_reply {   // daemon -> client : job id, plus the job's ring buffer fd sent as an SCM_RIGHTS ancillary message on success
	ebpf_u32 ok     ; // 1 on success, 0 on failure (no fd then)
	ebpf_u32 job_id ;
} ;

// one chunk of tracee memory captured by the kernel (a path string copied by getname_flags, or an open_how struct)
struct ebpf_blob {
	ebpf_u64 addr                ; // user-space address this blob was read at ; it matches the corresponding entry in ebpf_event::args, and is the key used by the replay memory
	ebpf_u32 len                 ; // number of valid bytes in data
	ebpf_u8  data[EBPF_BLOB_MAX] ;
} ;

// one syscall (or io_uring operation translated into the equivalent syscall) to be replayed through the syscall_tab machinery
struct ebpf_event {
	ebpf_u64         stamp_ns              ; // date of the access (bpf_ktime_get_boot_ns), reserved for future conservative-date handling
	ebpf_s64         rc                    ; // syscall result (negative means -errno)
	ebpf_u64         args[6]               ; // syscall arguments as seen at syscall entry (pointer args are user-space addresses matching a blob)
	ebpf_s64         nr                    ; // syscall number, index into SyscallDescr::s_tab (io_uring ops are mapped to their syscall equivalent)
	ebpf_u32         tid                   ; // thread id (in the tracer pid namespace) used to resolve /proc/<tid>/{cwd,fd}
	ebpf_u32         pid                   ; // thread group id
	ebpf_u32         is32                  ; // if set, the syscall used the 32-bit ABI (index into s_tab32)
	ebpf_u32         cwd_len               ; // number of valid bytes in cwd (0 if not resolved)
	ebpf_u8          cwd[EBPF_BLOB_MAX]    ; // current working directory resolved in-kernel : path components, leaf first, NUL-separated (rebuilt into an absolute path by the consumer, ...
	ebpf_u32         n_blobs               ; // ... avoids /proc which is unreliable once the task exited)
	struct ebpf_blob blobs[EBPF_MAX_BLOBS] ;
} ;

#endif
