// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// Job side of the ebpf autodep method.
// The kernel (ebpf.bpf.c) reports, for each syscall of the job accessing files of interest :
// - captures : resolved paths (relative to the repo or to a source dir) together with file info before the syscall modifies anything, and traversed symlinks with their target
// - an End event which closes them and carries the syscall number, its result and its args
// Here, we turn that into the same access reports as the other autodep methods do (cf. record.cc, the semantic of each syscall mirrors what is done there).
// Unlike them, we are not synchronous with the job : we may process an access long after it occurred, when files may have been modified, unlinked or renamed.
// So the rule is that we do not look at the disk : what must be known about files comes from the kernel.
// The exceptions are marked with DISK, they are all corner cases (failed syscalls for which the kernel resolved nothing and the error does not tell whether file exists).

#include <sys/stat.h>
#include <sys/wait.h>

#if HAS_32
	#include "world_32.h"
#endif

#include "app.hh"
#include "process.hh"

#include "record.hh"
#include "syscall_tab.hh"
#include "gather.hh"

#include "ebpf_client.hh"
#include "ebpf_event.h"

#include "ebpf.hh"

#ifndef O_TMPFILE
	#define O_TMPFILE 0 // no check for O_TMPFILE if it does not exist
#endif

using namespace Disk       ;
using namespace Time       ;
using namespace EbpfDaemon ;

void AutodepEbpf::Consumer::handle_path_res(Path const& path) {
	(void)path ;
}

void AutodepEbpf::Consumer::handle_event( EbpfEventHdr const& hdr , Path const& file , Path const& dst ) {
	(void)hdr  ;
	(void)file ;
	(void)dst  ;
}

//	static FileInfo _mk_info(EbpfEvent const& ev) {
//		if (!(ev.flags&EBPF_F_EXISTS)) return {} ;
//		FileStat st = {} ;
//		st.st_mode         = ev.mode                   ;
//		st.st_size         = ev.size                   ;
//		st.st_mtim.tv_sec  = ev.mtime_ns/1'000'000'000 ;
//		st.st_mtim.tv_nsec = ev.mtime_ns%1'000'000'000 ;
//		return FileInfo(st) ;
//	}

// runs in the child, just before execve /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
int/*rc*/ AutodepEbpf::s_prepare_child(void*) {
	RingBufBase::s_start_job() ;
	return 0 ;
}

bool/*ok*/ AutodepEbpf::s_register_mount_ok(::string const& m) {
	static constexpr const char* NoMounts[] = {
		"/dev/"
	,	"/proc/"
	,	"/run/"  // special case for /run/user/<uid>
	,	"/snap/"
	,	"/sys/"
	} ;
	static ::string run_uid = cat("/run/user/",::getuid()) ;
	/**/                             if (m==run_uid       ) return true /*ok*/ ;
	for( const char* nm : NoMounts ) if (m.starts_with(nm)) return false/*. */ ;
	/**/                                                    return true /*. */ ;
}

AutodepEbpf::AutodepEbpf(NewType) {
	for( [[maybe_unused]] int _ : iota(3) ) {
		if (ring_buf.connect_to_daemon()) goto Connected ;
		Child ebpfd {
			.as_session = true
		,	.cmd_line   = { *g_lmake_root_s+"_bin/lmake_ebpfd" , "lmake" }
		,	.stdout     = Child::PipeFd
		} ;
		ebpfd.spawn() ;
		::string reply = ebpfd.stdout.read() ;
		switch (reply[0]) {
			case '0' : ebpfd.wait     () ; break                                             ; // daemon is already running, wait for this new one that will terminate shortly
			case '1' : ebpfd.mk_daemon() ; break                                             ; // daemon has started       , let it live its daemon life
			default  : ebpfd.wait     () ; throw cat("malformed ebpf daemon reply : ",reply) ;
		}
	}
	throw "cannot connect to nor launch daemon"s ;
Connected :
	ring_buf.init() ;
	//
	EbpfRootId root_id = 1 ;
	RingBufBase::s_add_root( root_id++ , Record::s_autodep_env().repo_root_s ) ;
	for( ::string const& l : AcFd("/proc/self/mounts").read_lines() ) {
		::vector_s v = split(l) ; SWEAR( v.size()>1 , v ) ;
		::string&  m = v[1]     ; if (m=="/") continue ;                                       // / is auto-registered as root_id 0
		if (s_register_mount_ok(m)) {
			RingBufBase::s_add_root( root_id++ , m ) ;
			roots.push_back(::move(m)) ;
		}
	}
}

// runs in the tracer thread : consume this job's ring buffer until the child exits, then return its wstatus
int/*wstatus*/ AutodepEbpf::process(pid_t child_pid) {
	Lock          lock  { Record::s_mutex } ;                        // single consumer thread, but Record code asserts the lock is held (like the ptrace tracer)
	Epoll         epoll { New             } ;
	epoll.add_read(ring_buf.epoll_fd) ;
	for (;;) {
		bool has_events = +epoll.wait(Delay(1)) ;                    // check child status every second to detect ring buffer overflow
		if (has_events) {
			bool done = ring_buf.consume() ;
			if (done) {
				int   wstatus ;
				pid_t pid     = ::waitpid( child_pid , &wstatus , 0/*options*/ ) ; if (pid!=child_pid) throw "cannot waitpid job"s ;
				return wstatus ;
			}
		} else {
			pid_t pid = ::waitpid( child_pid , nullptr , WNOHANG ) ;
			if (pid==child_pid) throw "ring buffer overflow"s ;      // no job end seen in ring buffer
			if (pid!=0        ) throw "cannot waitpid job"s   ;
		}
	}
}
