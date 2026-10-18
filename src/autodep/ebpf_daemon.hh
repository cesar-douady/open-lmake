// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <ctime>

#include <format>
#include <string>

// /!\ must list all the syscalls of SyscallDescr::s_tab that access files through a path (this is checked by ebpf.cc)
//  X( name              , kind     , dfd1,path1 , dfd2,path2 , fd , flags )
#define EBPF_SYSCALLS(X) \
	X( access            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( faccessat         , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( faccessat2        , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( chdir             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( chmod             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( fchmodat          , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( chroot            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( creat             , CREAT    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( execve            , EXEC     , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( execveat          , EXEC     ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( getdents          , PLAIN    , -1  ,-1    , -1  ,-1    ,  0 , -1    ) \
	X( getdents64        , PLAIN    , -1  ,-1    , -1  ,-1    ,  0 , -1    ) \
	X( io_uring_enter    , IO_URING , -1  ,-1    , -1  ,-1    , -1 , -1    ) \
	X( link              , PLAIN    , -1  , 0    , -1  , 1    , -1 , -1    ) \
	X( linkat            , PLAIN    ,  0  , 1    ,  2  , 3    , -1 , -1    ) \
	X( mkdir             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( mkdirat           , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( mknod             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( mknodat           , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( mount             , PLAIN    , -1  , 1    , -1  ,-1    , -1 , -1    ) \
	X( move_mount        , PLAIN    ,  0  , 1    ,  2  , 3    , -1 , -1    ) \
	X( name_to_handle_at , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( open              , OPEN     , -1  , 0    , -1  ,-1    , -1 ,  1    ) \
	X( openat            , OPEN     ,  0  , 1    , -1  ,-1    , -1 ,  2    ) \
	X( openat2           , OPENAT2  ,  0  , 1    , -1  ,-1    , -1 ,  2    ) \
	X( open_tree         , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( readdir           , PLAIN    , -1  ,-1    , -1  ,-1    ,  0 , -1    ) \
	X( readlink          , READLINK , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( readlinkat        , READLINK ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( rename            , PLAIN    , -1  , 0    , -1  , 1    , -1 , -1    ) \
	X( renameat          , PLAIN    ,  0  , 1    ,  2  , 3    , -1 , -1    ) \
	X( renameat2         , PLAIN    ,  0  , 1    ,  2  , 3    , -1 , -1    ) \
	X( rmdir             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( stat              , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( stat64            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( fstatat64         , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( lstat             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( lstat64           , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( statx             , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( newfstatat        , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( oldstat           , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( oldlstat          , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( statfs            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( statfs64          , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( symlink           , PLAIN    , -1  , 1    , -1  ,-1    , -1 , -1    ) \
	X( symlinkat         , PLAIN    ,  1  , 2    , -1  ,-1    , -1 , -1    ) \
	X( truncate          , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( umount            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( umount2           , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( unlink            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( unlinkat          , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    ) \
	X( utime             , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( utimes            , PLAIN    , -1  , 0    , -1  ,-1    , -1 , -1    ) \
	X( utimensat         , PLAIN    ,  0  , 1    , -1  ,-1    , -1 , -1    )
// syscalls of SyscallDescr::s_tab that need no tracking with ebpf : cwd is captured when needed and io_uring cannot be forbidden (its operations are tracked)
#define EBPF_UNTRACKED_SYSCALLS(X) \
	X(fchdir           ) \
	X(io_uring_register) \
	X(io_uring_setup   )

namespace EbpfDaemon {

	static constexpr int NTrials = 3 ; // number of times to try to launch daemon before giving up

	struct Hello {                    // client -> daemon : sent once at connect, the dir fds of the roots are sent as an SCM_RIGHTS ancillary message
		uint64_t pidns_dev      = 0 ; // the client's pid namespace (dev,ino of /proc/self/ns/pid), so reported tids are meaningful to the client
		uint64_t pidns_ino      = 0 ;
		uint32_t deps_in_system = 0 ; // if 0, system files (/usr, /lib, /etc, ...) are simple, cf. Record::s_is_simple
		uint32_t n_roots        = 0 ; // number of root dirs (<=EBPF_MAX_ROOTS) : only accesses lying in these dirs are reported
	} ;

	struct Reply {            // daemon -> client : job id, plus the job's ring buffer fd sent as an SCM_RIGHTS ancillary message on success
		uint32_t job_id = 0 ; // no ring buffer fd if 0
	} ;

	enum class ReqKey : uint8_t {
		Stats
	,	Exclude // this is used for the tmp dir, which usually lies in the repo, and may be seen by the job elsewhere
	} ;

	struct Stats {
		uint64_t seq  = 0 ; // number of events the kernel has emitted (or tried to emit) for this job
		uint64_t lost = 0 ; // number of events that could not be pushed to the ring buffer (or whose data could not be captured), the job cannot be trusted if not 0
	} ;

	// easy, fast and good enough
	// cf https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	struct Fnv {
		static constexpr size_t Offset = sizeof(size_t)==8 ? 0xcbf29ce484222325 : 0x811c9dc5 ;
		static constexpr size_t Prime  = sizeof(size_t)==8 ? 0x00000100000001b3 : 0x01000193 ;
		//
		Fnv() = default ;
		Fnv(std::string const& s) { for( char c : s ) *this += c ; }
		//
		size_t operator+ (        ) const {                         return val   ; }
		Fnv&   operator+=(size_t x)       { val = (val^x) * Prime ; return *this ; }
		//
		size_t val = Offset ;
	} ;

	// unix-socket protocol between the root daemon (loads/attaches the shared programs) and the unprivileged job clients (job_exec/lautodep)
	constexpr char SockDir[] = "/run/open-lmake" ;

	inline std::string get_file_name(const char* sfx) {
		using namespace std::string_literals ;
		std::string exe        ( PATH_MAX , 0 )                                           ;
		ssize_t     exe_len    = ::readlink( "/proc/self/exe" , exe.data() , exe.size() ) ; if ( exe_len<0 || size_t(exe_len)>=exe.size()   ) throw "cannot read exe name"s        ;
		size_t      slash      = exe.rfind('/',exe.rfind('/',exe_len)-1)                  ; if ( slash==std::string::npos                   ) throw "cannot find 2 / in exe name"s ;
		std::string lmake_root = exe.substr(0,slash)                                      ;
		Fnv         fnv        { lmake_root }                                             ;
		std::string res        = std::format("{}/daemon-{:08x}.{}",SockDir,+fnv,sfx)      ; if ( res.size()>=sizeof(sockaddr_un().sun_path) ) throw "socket name too long"s        ;
		return res ;
	}
	inline std::string get_sock_name() { return get_file_name("sock") ; }

}
