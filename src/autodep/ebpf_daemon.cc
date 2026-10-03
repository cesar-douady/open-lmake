// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// Root daemon for the ebpf autodep method.
// It loads and attaches the BPF programs ONCE (so every syscall on the machine traverses a single program set), then serves unprivileged job clients
// (job_exec / lautodep) over a unix socket : each client gets a private per-job ring buffer (fd passed via SCM_RIGHTS) into which the kernel routes that
// job's file-access events. The clients themselves never touch bpf() and need no privileges.
//
// This is a standalone program (it deliberately avoids the lmake app framework) : build it, then run it as root, e.g. : sudo bin/lmake_bpfd

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <ctime>

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "ebpf_event.h"
#include "src/autodep/ebpf.skel.h" // auto-generated, so must include dir so the -M flag works

#include "utils.hh"

static struct ebpf* g_skel = nullptr ;
static const char*  g_sock = nullptr ;
//
static volatile sig_atomic_t g_stop = 0 ;

static void _on_signal(int) {
	g_stop = 1 ;
}

static void _log( const char* fmt , ... ) {
	char    ts[32] ; time_t t=time(nullptr) ; struct tm tm ; localtime_r(&t,&tm) ; strftime(ts,sizeof(ts),"%H:%M:%S",&tm) ;
	fprintf(stderr,"lmake_bpfd %s : ",ts) ;
	va_list ap ; va_start(ap,fmt) ; vfprintf(stderr,fmt,ap) ; va_end(ap) ;
	fputc('\n',stderr) ;
}

static int _pr( enum libbpf_print_level lvl , const char* fmt , va_list ap ) {
	if (lvl>LIBBPF_WARN) return 0 ;
	return vfprintf(stderr,fmt,ap) ;
}

// read exactly sz bytes (a single recv may return a partial message)
static bool/*ok*/ _recv_all( int conn , void* buf , size_t sz ) {
	char* p = static_cast<char*>(buf) ;
	while (sz) {
		ssize_t n = ::recv( conn , p , sz , 0/*flags*/ ) ;
		if (n<=0) return false/*ok*/ ;
		p  += n         ;
		sz -= size_t(n) ;
	}
	return true/*ok*/ ;
}

// send the reply plus, on success, the ring buffer fd as an SCM_RIGHTS ancillary message
static bool/*ok*/ _send_reply( int conn , ebpf_reply const& reply , int rb_fd/*-1 if none*/ ) {
	struct iovec  iov                           = { .iov_base=const_cast<ebpf_reply*>(&reply) , .iov_len=sizeof(reply) } ;
	char          cbuf[CMSG_SPACE(sizeof(int))] = {}                                                                     ;
	struct msghdr msg                           = {}                                                                     ; msg.msg_iov = &iov ; msg.msg_iovlen = 1 ;
	if (rb_fd>=0) {
		msg.msg_control    = cbuf         ;
		msg.msg_controllen = sizeof(cbuf) ;
		struct cmsghdr* cm = CMSG_FIRSTHDR(&msg) ;
		cm->cmsg_level = SOL_SOCKET            ;
		cm->cmsg_type  = SCM_RIGHTS            ;
		cm->cmsg_len   = CMSG_LEN(sizeof(int)) ;
		::memcpy( CMSG_DATA(cm) , &rb_fd , sizeof(int) ) ;
	}
	return ::sendmsg(conn,&msg,0)==ssize_t(sizeof(reply)) ;
}

// per-connection (== per-job) state : the ring buffer fd we created and the job_id, so we can clean up when the client disconnects
struct Job {
	int      rb_fd  = -1 ;
	uint32_t job_id =  0 ;
} ;

int main( int /*argc*/ , char* /*argv*/[] ) {
	::libbpf_set_print(_pr) ;
	::signal( SIGINT  , _on_signal ) ;
	::signal( SIGTERM , _on_signal ) ;
	::signal( SIGPIPE , SIG_IGN    ) ;
	//
	g_sock = getenv(EBPF_SOCK_ENV) ; if (!g_sock||!*g_sock) g_sock = EBPF_SOCK_DFLT ;
	//
	// load & attach the shared programs
	g_skel = ebpf__open() ;
	if (!g_skel                ) { _log("cannot open ebpf programs"                                                              ) ; return 1 ; }
	if (ebpf__load  (g_skel)!=0) { _log("cannot load ebpf programs (need CAP_BPF+CAP_PERFMON, run as root and with a BTF kernel)") ; return 1 ; }
	if (ebpf__attach(g_skel)!=0) { _log("cannot attach ebpf programs (need a recent BTF kernel)"                                 ) ; return 1 ; }
	// the `wanted` syscall filter is shared by all jobs and owned by syscall_tab (client side) : each client uploads it in its hello (idempotent), see below
	//
	// listen socket
	int                lst  = ::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0) ; if (lst<0) { _log("cannot create socket : %s",strerror(errno)) ; return 1 ; }
	struct sockaddr_un addr = {}                                           ; addr.sun_family = AF_UNIX ; ::strncpy( addr.sun_path , g_sock , sizeof(addr.sun_path)-1 ) ;
	//
	/**/ ::unlink(g_sock) ;
	if ( ::bind  (lst,(struct sockaddr*)&addr,sizeof(addr))!=0 ) { _log("cannot bind %s : %s",g_sock,strerror(errno)) ; return 1 ; }
	/**/ ::chmod (g_sock,0666) ;                                                                                                                       // allow unprivileged clients to connect
	if ( ::listen(lst,64)                                  !=0 ) { _log("cannot listen : %s" ,       strerror(errno)) ; return 1 ; }
	_log("ready, listening on %s",g_sock) ;
	//
	int      job_rbs_fd  = bpf_map__fd(g_skel->maps.job_rbs) ;
	int      job_cfg_fd  = bpf_map__fd(g_skel->maps.job_cfg) ;
	int      wanted_fd   = bpf_map__fd(g_skel->maps.wanted ) ;
	uint32_t next_job_id = 1                                 ;
	//
	::vector<struct pollfd> pfds { {.fd=lst,.events=POLLIN,.revents=0} } ;
	::vector<Job>           jobs { {                                 } } ;
	//
	while (!g_stop) {
		int r = ::poll( pfds.data() , pfds.size() , -1/*timeout*/ ) ;
		if (r<0) {
			if (errno==EINTR) continue ;
			else              break    ;
		}
		// new connections
		if (pfds[0].revents&POLLIN) {
			int conn = ::accept4( lst , nullptr/*addr*/ , nullptr/*addrlen*/ , SOCK_CLOEXEC ) ;
			if (conn>=0) {
				ebpf_hello hello ;
				if (!_recv_all(conn,&hello,sizeof(hello))) {
					::close(conn) ;
				} else {
					// (re)fill the shared wanted filter from the client-provided list (owned by syscall_tab)
					for( uint32_t i : iota(::min<uint32_t>(hello.n_wanted,EBPF_NSYSCALLS)) ) {
						uint32_t nr  = hello.wanted[i] ; if (nr>=EBPF_NSYSCALLS) continue ;
						uint8_t  one = 1               ;
						::bpf_map_update_elem(wanted_fd,&nr,&one,BPF_ANY) ;
					}
					uint32_t   jid   = next_job_id++                                                                                                                              ;
					int        rb_fd = ::bpf_map_create( BPF_MAP_TYPE_RINGBUF , nullptr/*map_name*/ , 0/*key_size*/ , 0/*value_size*/ , 4u<<20/*max_entries*/ , nullptr/*opts*/ ) ;
					ebpf_reply reply { .ok=0 , .job_id=jid }                                                                                                                      ;
					if (rb_fd<0) {
						_log("job %u : cannot create ring buffer : %s",jid,strerror(errno)) ;
						rb_fd = -1 ;
					} else if ( ::bpf_map_update_elem(job_rbs_fd,&jid,&rb_fd,BPF_ANY)!=0 ) {
						_log("job %u : cannot register ring buffer : %s",jid,strerror(errno)) ;
						close(rb_fd) ;
						rb_fd = -1 ;
					} else {
						struct { uint64_t dev , ino ; uint32_t deps_in_system ; } cfg = { hello.pidns_dev , hello.pidns_ino , hello.deps_in_system } ; // must match struct job_cfg_val in ebpf.bpf.c
						::bpf_map_update_elem( job_cfg_fd , &jid , &cfg , BPF_ANY ) ;
						reply.ok = 1 ;
					}
					_send_reply( conn , reply , rb_fd ) ;
					if (reply.ok) {
						pfds.push_back({ .fd=conn , .events=0 , .revents=0 }) ;                                                                        // only interested in hangup
						jobs.push_back({ .rb_fd=rb_fd , .job_id=jid        }) ;
						_log("job %u : registered",jid) ;
					} else {
						/**/          ::close(conn ) ;
						if (rb_fd>=0) ::close(rb_fd) ;
					}
				}
			}
		}
		// disconnected clients : release their job (kernel drops in-flight state for those tids as they exit)
		for( size_t i=1 ; i<pfds.size() ; ) {
			if (pfds[i].revents&(POLLHUP|POLLERR|POLLNVAL)) {
				uint32_t jid = jobs[i].job_id ;
				::bpf_map_delete_elem( job_rbs_fd , &jid ) ;
				::bpf_map_delete_elem( job_cfg_fd , &jid ) ;
				::close(jobs[i].rb_fd) ;
				::close(pfds[i].fd   ) ;
				_log("job %u : released",jid) ;
				pfds.erase(pfds.begin()+i) ;
				jobs.erase(jobs.begin()+i) ;
			} else {
				i++ ;
			}
		}
	}
	_log("shutting down") ;
	::unlink(g_sock)        ;
	::ebpf__destroy(g_skel) ;
	return 0 ;
}
