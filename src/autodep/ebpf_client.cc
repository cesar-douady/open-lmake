// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#include <limits.h>
#include <string.h>     // strerror
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <format>

#include "ebpf_event.h"
#include "ebpf_client.hh"

using namespace std               ;
using namespace ::string_literals ;

namespace EbpfDaemon {

	struct Fnv {                                                // easy, fast and good enough
		// cf https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
		static constexpr uint64_t Offset = 0xcbf29ce484222325 ; // value for 32-bit checksum : 0x811c9dc5 ;
		static constexpr uint64_t Prime  = 0x00000100000001b3 ; // value for 32-bit checksum : 0x01000193 ;
		//
		Fnv() = default ;
		Fnv(::string const& s) { for( char c : s ) *this += c ; }
		//
		uint64_t operator+ (      ) const {                         return val   ; }
		Fnv&     operator+=(char x)       { val = (val^x) * Prime ; return *this ; }
		//
		uint64_t val = Offset ;
	} ;

	uint64_t const g_mrkr = []() {
		try {
			::string exe        (PATH_MAX,0)                                         ;
			ssize_t  exe_len    = ::readlink("/proc/self/exe",exe.data(),exe.size()) ; if ( exe_len<0 || size_t(exe_len)>=exe.size() ) throw "cannot read exe name"s        ; exe.resize(exe_len) ;
			size_t   slash      = exe.rfind('/',exe.rfind('/',exe_len)-1)            ; if ( slash==::string::npos                    ) throw "cannot find 2 / in exe name"s ;
			::string lmake_root = exe.substr(0,slash)                                ;
			return +Fnv(lmake_root) ;
		} catch (...) {
			return uint64_t(0) ;
		}
	}() ;

	::string get_file_name(::string const& sfx) {
		::string res = ::format("{}/daemon-{:08x}.{}",SockDir,g_mrkr,sfx) ; if (res.size()>=sizeof(sockaddr_un().sun_path)) throw "socket name too long"s ;
		return res ;
	}

	bool/*ok*/ RingBufBase::connect_to_daemon() {
		//
		// connect to daemon socket
		//
		::string      sn   = get_sock_name() ;
		::sockaddr_un addr = {}              ; addr.sun_family=AF_UNIX ; ::strncpy(addr.sun_path,sn.c_str(),sizeof(addr.sun_path)-1) ;
		//
		/**/ _daemon_fd = ::socket( AF_UNIX , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ )                     ; if (_daemon_fd<0) throw "cannot create ebpf socket"s ;
		int  rc         = ::connect( _daemon_fd , reinterpret_cast<struct sockaddr*>(&addr) , sizeof(addr) ) ; if (rc!=0       ) return false/*ok*/ ;                 // no daemon
		//
		// a stuck daemon must not block the job forever
		{	::timeval to = { .tv_sec=60 , .tv_usec=0 } ;
			::setsockopt( _daemon_fd , SOL_SOCKET , SO_RCVTIMEO , &to , sizeof(to) ) ;
		}
		//
		// get info from daemon
		//
		::iovec  iov                           = { .iov_base=&job_id , .iov_len=sizeof(job_id)                                                                                          } ;
		char     cbuf[CMSG_SPACE(sizeof(int))] = {}                                                                                                                                       ;
		::msghdr msg                           = { .msg_name=nullptr , .msg_namelen=0 , .msg_iov=&iov , .msg_iovlen=1 , .msg_control=cbuf , .msg_controllen=sizeof(cbuf) , .msg_flags=0 } ;
		//
		ssize_t msg_len = ::recvmsg( _daemon_fd , &msg , MSG_CMSG_CLOEXEC ) ; if (msg_len!=ssize_t(sizeof(job_id))) throw "no/bad reply from ebpf daemon"s ;
		//
		for( cmsghdr* cm=CMSG_FIRSTHDR(&msg) ; cm ; cm=CMSG_NXTHDR(&msg,cm) )
			if ( cm->cmsg_level==SOL_SOCKET && cm->cmsg_type==SCM_RIGHTS ) {
				::memcpy( &_ring_buf_fd , CMSG_DATA(cm) , sizeof(int) ) ;
				break ;
			}
		if (_ring_buf_fd<0) throw "bad reply from ebpf daemon"s ;
		return true/*ok*/ ;                                                                                                                                           // connected
	}

	void RingBufBase::init() {
		if (_registered) return ;
		EbpfMagicReq req ;
		req.proc   = EBPF_MAGIC_REGISTER ;
		req.mrkr   = g_mrkr              ;
		req.job_id = job_id              ;
		::fsetxattr( EBPF_MAGIC_FD , nullptr/*name*/ , &req , sizeof(req) , 0/*flags*/ ) ;
		s_add_root( EBPF_ROOT_ID_PROC , "/proc" ) ;
		_registered = true ;
	}

	RingBufBase::~RingBufBase() {
		/**/               ::close(_ring_buf_fd) ;
		if (_daemon_fd>=0) ::close(_daemon_fd  ) ;
	}

	// /!\ this function must be malloc free as it may be called in preexec_func during a fork
	void RingBufBase::s_start_job() {
		EbpfMagicReq req {} ; req.proc = EBPF_MAGIC_JOB ;
		::fsetxattr( EBPF_MAGIC_FD , nullptr/*name*/ , &req , sizeof(req) , 0/*flags*/ ) ;
	}

	Path::Path( EbpfRootId ri , ::string_view data ) : root_id{ri} {
		for (;;) {
			size_t pos = data.rfind('\0') ;
			path.append(data.substr(pos+1)) ;
			if (pos==::string::npos) break ;
			path.push_back('/') ;
			data = data.substr(0,pos) ;
		}
	}

	void RingBufBase::s_add_root( EbpfRootId root_id , ::string const& root ) {                                                        // empty means /
		const char*  r   = root.empty() ? "/" : root.c_str() ; if (r[0]!='/') throw "cannot add relative root"s ;
		EbpfMagicReq req ;
		req.proc    = EBPF_MAGIC_ROOT           ;
		req.root_id = root_id                   ;
		req.fd      = ::open( r , O_DIRECTORY ) ; ; if (req.fd<0     ) throw ::format("cannot add (%s) root {}",::strerror(errno),r) ; // dont close to maintain its dentry live in kernel
		::fsetxattr( EBPF_MAGIC_FD , root.c_str() , &req , sizeof(req) , 0/*flags*/ ) ;
		if (root_id!=EBPF_ROOT_ID_EXTERN)
			for( ::string d=root ; !d.empty() ; d=d.substr(0,d.rfind('/')) ) {                                                         // super-dir of root dirs must be registered as well
				req.root_id = EBPF_ROOT_ID_SUPER                ;
				req.fd      = ::open( d.c_str() , O_DIRECTORY ) ;                                                                      // dont close to maintain its dentry live in kernel
				::fsetxattr( EBPF_MAGIC_FD , root.c_str() , &req , sizeof(req) , 0/*flags*/ ) ;
			}
	}

}
