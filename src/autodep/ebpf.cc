// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "utils.hh"

#include "ebpf.hh"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <bpf/libbpf.h> // only for the ring_buffer consumer API ; the programs are loaded by the daemon, not here

#include "record.hh"
#include "syscall_tab.hh"
#include "gather.hh"

#include "ebpf_event.h"

using namespace Disk ;

namespace AutodepEbpf {

	// per-job state obtained from the daemon (a single job per job_exec / lautodep process)
	static int      _g_sock   = -1 ; // kept open for the whole job : closing it tells the daemon to release the job
	static int      _g_rb_fd  = -1 ; // this job's ring buffer, received via SCM_RIGHTS
	static uint32_t _g_job_id =  0 ; // carried to the kernel by the child's self-registration marker

	//
	// consumer : turns each captured event back into the same Record operations as ptrace/seccomp
	//

	struct Consumer {
		// services
		Record& record(pid_t tid) {
			auto it = _records.try_emplace(tid,New,tid).first ; // Record(New,pid=tid) : RealPath resolves dir fds via /proc/<tid>
			return it->second ;
		}
		void handle( void* data , size_t sz ) {
			if ( sz<offsetof(ebpf_event,blobs) ) return ;
			//
			ebpf_event const&  ev      = *static_cast<ebpf_event const*>(data)          ;
			uint64_t           args[6] ;                                                  for( int i : iota(6) ) args[i] = ev.args[i] ;
			AutodepReplay::Mem mem     ;
			uint32_t           nb      = ::min<uint32_t>( ev.n_blobs , EBPF_MAX_BLOBS ) ;
			for( uint32_t i : iota(nb) ) {
				ebpf_blob const& b   = ev.blobs[i]                            ;
				size_t           len = ::min<size_t>( b.len , EBPF_BLOB_MAX ) ;
				mem[b.addr] = ::string( reinterpret_cast<const char*>(b.data) , len ) ;
			}
			try                                  { AutodepReplay::replay( record(ev.tid) , ev.nr , ev.is32 , args , ev.rc , mem ) ;                                }
			catch (AutodepReplay::Miss const&  ) { if (!err) err = "an access could not be recorded (path not captured by ebpf), dependencies may be incomplete" ; }
			catch (::string            const& e) { if (!err) err = e                                                                                             ; }
		}
		// data
		::umap<pid_t,Record> _records ;
		::string             err      ;
	} ;

	static int _handle_event( void* ctx , void* data , size_t sz ) {
		static_cast<Consumer*>(ctx)->handle(data,sz) ;
		return 0 ;
	}

	//
	// daemon protocol
	//

	static bool _send_all( int fd , const void* buf , size_t sz ) {
		const char* p = static_cast<const char*>(buf) ;
		while (sz) {
			ssize_t n = ::send( fd , p , sz , 0/*flags*/ ) ; if (n<=0) return false ;
			p  += n         ;
			sz -= size_t(n) ;
		}
		return true ;
	}

	// receive the reply and the ring buffer fd sent as an SCM_RIGHTS ancillary message
	static bool _recv_reply( int fd , ebpf_reply* reply , int* rb_fd ) {
		*rb_fd = -1 ;
		struct iovec  iov                           = { .iov_base=reply , .iov_len=sizeof(*reply) } ;
		char          cbuf[CMSG_SPACE(sizeof(int))] = {}                                            ;
		struct msghdr msg                           = {}                                            ; msg.msg_iov=&iov ; msg.msg_iovlen=1 ; msg.msg_control=cbuf ; msg.msg_controllen=sizeof(cbuf) ;
		//
		if (::recvmsg(fd,&msg,0/*flags*/)!=ssize_t(sizeof(*reply))) return false ;
		//
		for( struct cmsghdr* cm=CMSG_FIRSTHDR(&msg) ; cm ; cm=CMSG_NXTHDR(&msg,cm) )
			if ( cm->cmsg_level==SOL_SOCKET && cm->cmsg_type==SCM_RIGHTS ) ::memcpy( rb_fd , CMSG_DATA(cm) , sizeof(int) ) ;
		return true ;
	}

	//
	// hooks called by Gather (set by load())
	//

	// runs in the child, just before execve (must be malloc-free) : self-register (with our job_id) via a marker syscall, in the kernel-visible namespace
	static int/*rc*/ _prepare_child(void*) {
		char c ;
		::syscall( SYS_readlinkat , EBPF_REGISTER_FD , "" , &c , long(_g_job_id) ) ; // job_id carried in arg 3 ; returns EBADF, harmless
		return 0 ;
	}

	// runs in the tracer thread : consume this job's ring buffer until the child exits, then return its wstatus
	static int/*wstatus*/ _process(pid_t child_pid) {
		Lock                lock { Record::s_mutex }                                                ; // single consumer thread, but Record code asserts the lock is held (like the ptrace tracer)
		Consumer            cons ;
		struct ring_buffer* rb   = ::ring_buffer__new( _g_rb_fd , _handle_event , &cons , nullptr ) ; throw_unless( rb , "cannot open ebpf ring buffer" ) ;
		//
		int wstatus = 0 ;
		for (;;) {
			int   r = ::ring_buffer__poll( rb , 100/*ms*/ )       ; if ( r<0 && r!=-EINTR ) break ;   // process available events, wait up to 100ms
			pid_t w = ::waitpid( child_pid , &wstatus , WNOHANG ) ;
			if ( w==child_pid                    ) break ;                                            // child is done
			if ( w<0 && errno!=EINTR && errno!=0 ) break ;                                            // ECHILD or other : give up waiting
		}
		::ring_buffer__consume(rb) ;                                                                  // drain whatever is left
		::ring_buffer__free   (rb) ;
		throw_if( +cons.err , cons.err ) ;
		return wstatus ;
	}

	// connect to the daemon, upload our pid namespace and the recorded-syscall list, and receive this job's ring buffer
	void load(bool deps_in_system) {
		if (_g_sock>=0) return ;                                                                                                                  // already connected (defensive)
		const char*        path = ::getenv(EBPF_SOCK_ENV)                                        ; if ( !path || !*path ) path = EBPF_SOCK_DFLT ;
		int                s    = ::socket( AF_UNIX , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ; throw_unless( s>=0 , "cannot create ebpf socket" ) ;
		struct sockaddr_un addr = {}                                                             ; addr.sun_family=AF_UNIX ; ::strncpy(addr.sun_path,path,sizeof(addr.sun_path)-1) ;
		if (::connect(s,reinterpret_cast<struct sockaddr*>(&addr),sizeof(addr))!=0) {
			::close(s) ;
			throw cat("cannot reach the ebpf daemon on ",path," : start it with 'sudo bin/lmake_bpfd' (or set ",EBPF_SOCK_ENV,")") ;
		}
		// hello : our pid namespace + the syscalls to record (owned by syscall_tab ; the daemon has no access to that list)
		ebpf_hello hello = {} ;
		{	struct ::stat st ;
			if (::stat("/proc/self/ns/pid",&st)==0) {
				hello.pidns_dev = st.st_dev ;
				hello.pidns_ino = st.st_ino ;
			}
		}
		hello.deps_in_system = deps_in_system ; // let the daemon drop system-file accesses in-kernel when they cannot be deps
		uint32_t n = 0 ;
		for( long nr : iota(long(SyscallDescr::NSyscalls)) ) {
			SyscallDescr const& d = SyscallDescr::s_tab[nr] ;
			if (!d.entry) continue ;
			if ( d.comment==Comment::io_uring_enter || d.comment==Comment::io_uring_register || d.comment==Comment::io_uring_setup ) continue ; // io_uring ops captured on the io_* handlers
			if (n<EBPF_NSYSCALLS) hello.wanted[n++] = uint32_t(nr) ;
		}
		hello.n_wanted = n ;
		if (!_send_all(s,&hello,sizeof(hello))) {
			::close(s) ;
			throw "cannot talk to the ebpf daemon"s ;
		}
		ebpf_reply reply = {} ;
		int        rb_fd = -1 ;
		if ( !_recv_reply(s,&reply,&rb_fd) || !reply.ok || rb_fd<0 ) {
			::close(s) ;
			throw "the ebpf daemon refused to register the job"s ;
		}
		//
		_g_sock   = s            ; // keep the connection open : closing it releases the job in the daemon
		_g_rb_fd  = rb_fd        ;
		_g_job_id = reply.job_id ;
		//
		Gather::s_ebpf_prepare_child = _prepare_child ;
		Gather::s_ebpf_process       = _process       ;
	}

}
