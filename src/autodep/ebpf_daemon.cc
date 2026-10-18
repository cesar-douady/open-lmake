// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// Root daemon for the ebpf autodep method.
// It loads and attaches the BPF programs ONCE (so every syscall on the machine traverses a single program set), then serves unprivileged job clients
// (job_exec / lautodep) over a unix socket : each client gets a private per-job ring buffer (fd passed via SCM_RIGHTS) into which the kernel routes that
// job's file-access events. The clients themselves never touch bpf() and need no privileges.
//
// The hooks used by the BPF programs are not all guaranteed interfaces (some may be inlined, renamed or have their signature changed from kernel to kernel).
// So, at start-up, the daemon runs a self-test : it traces a child that performs each kind of access and checks that the expected events are received.
// If not, it refuses to start (missing an access would silently lead to wrong builds).
//
// Each client is served by its own thread : registering and releasing a ring buffer in the kernel implies waiting for a rcu grace period (tens of ms),
// which must not be serialized among jobs.
//
// This is a standalone program (it deliberately avoids the lmake app framework) : build it, then run it as root, e.g. : sudo _bin/lmake_bpfd
// options :
//   --test    : run the self-test verbosely (events are dumped) and exit
//   --no-test : skip the self-test (for debug only)

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstdarg>

#include <atomic>
#include <concepts>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "sys_config.h"

#include "ebpf_utils.hh"
#include "ebpf_self_test.hh"

using namespace std        ;
using namespace EbpfDaemon ;

// use constexpr processing rather than #ifdef/#endif so as to discriminate between defined/undefined macros on a single line
template<::integral I,I DfltVal> consteval I _macro_val_str(const char* n_str) {
	I    n      = 0     ;
	bool is_neg = false ;
	int  base   = 10    ;
	if (*n_str=='-') {
		if (!::is_signed_v<I>) return DfltVal ;
		is_neg = true ;
		n_str++ ;
	}
	if (*n_str=='0') {
		n_str++ ;
		if ( *n_str=='x' || *n_str=='X' ) { n_str++ ; base = 16 ; }
		else                                          base =  8 ;
	}
	for( const char* p=n_str ; *p ; p++ )
		if      ( '0'<=*p && *p<='9' && (*p-'0'   <base) ) { if (::is_same_v<I,bool>) n |= *p!='0' ; else n = (n*base) + (*p-'0'   ) ; }
		else if ( 'a'<=*p && *p<='z' && (*p-'a'+10<base) ) { if (::is_same_v<I,bool>) n  = true    ; else n = (n*base) + (*p-'a'+10) ; }
		else if ( 'A'<=*p && *p<='Z' && (*p-'A'+10<base) ) { if (::is_same_v<I,bool>) n  = true    ; else n = (n*base) + (*p-'A'+10) ; }
		else                                                 return DfltVal ;
	if ( !::is_same_v<I,bool> && is_neg ) n = -n ;
	return n ;
}
#define _MACRO_VAL_STR(x         ) #x                                                         // indirect macro to ensure we get the defined value when arg to MACRO_VAL is a defined macro
#define MACRO_VAL(     macro,dflt) _macro_val_str<decltype(dflt),dflt>(_MACRO_VAL_STR(macro))

static constexpr uint32_t MaxExcludes  = 16 ; // max number of dirs a job can exclude, so a client cannot make the daemon hold an unbounded number of fds
static constexpr time_t   HelloTimeout = 10 ; // in seconds, a client must not be able to hold a thread without registering a job this memory is allocated in the kernel for each running job

::mutex      g_reg_mutex ;
struct ebpf* g_skel      = nullptr ;
int          g_log_fd    = -1      ;

static int _pr( enum libbpf_print_level lvl , const char* fmt , va_list ap ) {
	// higher lvl means less critical
	if (lvl>LIBBPF_WARN)                                                       return 0 ;
	else                 { vlog(false/*with_errno*/,false/*with_nl*/,fmt,ap) ; return 1 ; }
}

// each running job needs a few fds (connection, ring buffer, dirs of interest), so the default limit would be reached with a few hundreds of jobs
static void _raise_n_open_files(rlim_t n_open_files) {
	struct rlimit old_rl = {} ;
	if (::getrlimit(RLIMIT_NOFILE,&old_rl)!=0) {
		log_errno("cannot get max number of open files") ;
		return ;
	}
	if (old_rl.rlim_max<n_open_files) {
		n_open_files = old_rl.rlim_max ;
		log("reduced number of allowed open files from %u to %u",n_open_files,old_rl.rlim_max) ;
	}
	if (old_rl.rlim_cur<n_open_files) {
		struct rlimit new_rl = { .rlim_cur=n_open_files , .rlim_max=::max(old_rl.rlim_max,n_open_files) } ;
		if (::setrlimit(RLIMIT_NOFILE,&new_rl)!=0)
			log_errno("cannot raise the number of open files from %u to %u",old_rl.rlim_cur,n_open_files) ;
	}
}
//
// syscalls
//

static constexpr bool SyscallsOk = []() {
	bool ok = true ;
	// tracked syscalls
	#define X(   name , kind , d1,p1 , d2,p2 , fd , fl ) if ( long nr=MACRO_VAL(SYS_##name  ,-1L) ; nr>=0 ) ok &= nr<EBPF_NSYSCALLS ;
	#define X32( name , kind , d1,p1 , d2,p2 , fd , fl ) if ( long nr=MACRO_VAL(SYS32_##name,-1L) ; nr>=0 ) ok &= nr<EBPF_NSYSCALLS ;
	EBPF_SYSCALLS(X)
	#if HAS_32
		EBPF_SYSCALLS(X32)
	#endif
	#undef X
	#undef X32
	#define X(  name) if ( long nr=MACRO_VAL(SYS_##name  ,-1L) ; nr>=0 ) ok &= nr<EBPF_NSYSCALLS ;
	#define X32(name) if ( long nr=MACRO_VAL(SYS32_##name,-1L) ; nr>=0 ) ok &= nr<EBPF_NSYSCALLS ;
	EBPF_UNTRACKED_SYSCALLS(X)
	#if HAS_32
		EBPF_UNTRACKED_SYSCALLS(X32)
	#endif
	#undef X
	#undef X32
	return ok ;
} () ;
static_assert( SyscallsOk ) ;

static constexpr ::array<::array<uint32_t,EBPF_NSYSCALLS>,1+HAS_32> Syscalls = []() {
	::array<::array<uint32_t,EBPF_NSYSCALLS>,2/*is_32*/> res ;
	// tracked syscalls
	#define X(   name , kind , d1,p1 , d2,p2 , fd , fl ) if ( long nr=MACRO_VAL(SYS_##name  ,-1L) ; nr>=0 && nr<EBPF_NSYSCALLS ) res[0][nr] = EBPF_SC(EBPF_SCK_##kind,d1,p1,d2,p2,fd,fl) ;
	#define X32( name , kind , d1,p1 , d2,p2 , fd , fl ) if ( long nr=MACRO_VAL(SYS32_##name,-1L) ; nr>=0 && nr<EBPF_NSYSCALLS ) res[1][nr] = EBPF_SC(EBPF_SCK_##kind,d1,p1,d2,p2,fd,fl) ;
	EBPF_SYSCALLS(X)
	#if HAS_32
		EBPF_SYSCALLS(X32)
	#endif
	#undef X
	#undef X32
	return res ;
} () ;

//
// protocol
//

// the syscall table is shared by all jobs
void _set_syscalls(struct ebpf* skel) {
	int fd = ::bpf_map__fd(skel->maps.syscalls) ;
	for( uint32_t is32=0 ; is32<1+HAS_32 ; is32++ )
		for( uint32_t nr=0 ; nr<EBPF_NSYSCALLS ; nr++ ) {
			uint32_t k = is32*EBPF_NSYSCALLS + nr ;
			if (::bpf_map_update_elem( fd , &k , &Syscalls[is32][nr] , BPF_ANY )!=0) throw "cannot upload syscall table"s ;
		}
	::close(fd) ;
}

// receive hello and the root dir fds sent as an SCM_RIGHTS ancillary message
static ::vector<int>/*root_fds*/ _recv_hello( int conn , Hello&/*out*/ hello ) {
	struct SetTimeout {
		SetTimeout (int fd_) : fd{fd_} { set_timeout(HelloTimeout) ; }
		~SetTimeout(       )           { set_timeout(            ) ; }
		//
		void set_timeout(time_t secs=0) {
			struct timeval tv = { .tv_sec=secs , .tv_usec=0 } ;
			if (::setsockopt( fd , SOL_SOCKET , SO_RCVTIMEO , &tv , sizeof(tv) )!=0) log_errno("cannot set receive timeout to %d",int(secs)) ;
			if (::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &tv , sizeof(tv) )!=0) log_errno("cannot set send    timeout to %d",int(secs)) ;
		}
		//
		int fd ;
	} ;
	::vector<int> root_fds ;
	char*         p        = reinterpret_cast<char*>(&hello) ;
	size_t        sz       = sizeof(hello)                   ;
	SetTimeout    to       { conn }                          ;
	while (sz) {
		struct iovec  iov                                          = { .iov_base=p , .iov_len=sz } ;
		char          cbuf[CMSG_SPACE(EBPF_MAX_ROOTS*sizeof(int))] = {}                            ;
		struct msghdr msg                                          = {}                            ; msg.msg_iov=&iov ; msg.msg_iovlen=1 ; msg.msg_control=cbuf ; msg.msg_controllen=sizeof(cbuf) ;
		ssize_t n = ::recvmsg( conn , &msg , MSG_CMSG_CLOEXEC ) ;
		if (n<=0) goto Err ;
		for( struct cmsghdr* cm=CMSG_FIRSTHDR(&msg) ; cm ; cm=CMSG_NXTHDR(&msg,cm) ) {
			if (!( cm->cmsg_level==SOL_SOCKET && cm->cmsg_type==SCM_RIGHTS )) continue ;
			for( size_t ofs=0 ; ofs<cm->cmsg_len-CMSG_LEN(0) ; ofs+=sizeof(int) ) {
				int fd ; ::memcpy( &fd , CMSG_DATA(cm)+ofs , sizeof(int) ) ;
				root_fds.push_back(fd) ;
				if (root_fds.size()>EBPF_MAX_ROOTS) goto Err ;
			}
		}
		p  += n         ;
		sz -= size_t(n) ;
	}
	if (root_fds.size()!=hello.n_roots) goto Err ;
	return root_fds ;
Err :
	for( int fd : root_fds ) ::close(fd) ;
	return {} ;
}

// send the reply plus, on success, the ring buffer fd as an SCM_RIGHTS ancillary message
static bool/*ok*/ _send_reply( int conn , Reply const& reply , int rb_fd=-1 ) {
	struct iovec  iov                           = { .iov_base=const_cast<Reply*>(&reply) , .iov_len=sizeof(reply) } ;
	char          cbuf[CMSG_SPACE(sizeof(int))] = {}                                                                ;
	struct msghdr msg                           = {}                                                                ; msg.msg_iov = &iov ; msg.msg_iovlen = 1 ;
	if (reply.job_id) {
		msg.msg_control    = cbuf         ;
		msg.msg_controllen = sizeof(cbuf) ;
		struct cmsghdr* cm = CMSG_FIRSTHDR(&msg) ;
		cm->cmsg_level = SOL_SOCKET            ;
		cm->cmsg_type  = SCM_RIGHTS            ;
		cm->cmsg_len   = CMSG_LEN(sizeof(int)) ;
		::memcpy( CMSG_DATA(cm) , &rb_fd , sizeof(int) ) ;
	}
	return ::sendmsg(conn,&msg,MSG_NOSIGNAL)==ssize_t(sizeof(reply)) ;
}

//
// clients
//

static bool/*ok*/ _start_job(Job&/*inout*/ job) {
	Hello hello ;
	//
	job.root_fds = _recv_hello(job.conn,/*out*/hello) ; if (job.root_fds.empty()) return false/*ok*/ ;
	//
	Reply reply { .job_id=job.mk_job(hello)?job.job_id:0 } ;
	//
	return _send_reply(job.conn,reply,job.rb_fd) && reply.job_id ;
}

static bool/*done*/ _process_job(Job&/*inout*/ job) {
	ReqKey          req                           ;
	int             req_fd                        = -1                                                                                                                                     ;
	struct ::iovec  iov                           { .iov_base=&req , .iov_len=1                                                                                                          } ;
	char            cbuf[CMSG_SPACE(sizeof(int))] = {}                                                                                                                                     ;
	struct ::msghdr msg                           { .msg_name=nullptr , .msg_namelen=0 , .msg_iov=&iov , .msg_iovlen=1 , .msg_control=cbuf , .msg_controllen=sizeof(cbuf) , .msg_flags=0 } ;
	ssize_t         n                             = ::recvmsg( job.conn , &msg , MSG_CMSG_CLOEXEC )                                                                                        ;
	if ( n<0 && errno==EINTR ) return false/*done*/ ;
	if ( n<=0                ) return true /*.   */ ;
	for( struct cmsghdr* cm=CMSG_FIRSTHDR(&msg) ; cm ; cm=CMSG_NXTHDR(&msg,cm) ) {
		if (!( cm->cmsg_level==SOL_SOCKET && cm->cmsg_type==SCM_RIGHTS )) continue ;
		for( size_t ofs=0 ; ofs<(cm->cmsg_len-CMSG_LEN(0)) ; ofs+=sizeof(int) ) {    // close all received fds but the first one
			int fd ; ::memcpy( &fd , CMSG_DATA(cm)+ofs , sizeof(int) ) ;
			if (req_fd<0) req_fd = fd ;
			else          ::close(fd) ;
		}
	}
	bool ok = false ;                                                                // as long as not recognized ok
	switch (req) {
		case ReqKey::Stats : {
			JobCfg cfg   = {}                              ; ::bpf_map_lookup_elem( bpf_map__fd(g_skel->maps.job_cfg) , &job.job_id , &cfg ) ;
			Stats  stats { .seq=cfg.seq , .lost=cfg.lost } ;
			ok = ::send( job.conn , &stats , sizeof(stats) , MSG_NOSIGNAL )==ssize_t(sizeof(stats)) ;
		} break ;
		case ReqKey::Exclude : {
			char done =
				req_fd>=0
			&&	job.root_fds.size()<EBPF_MAX_ROOTS
			&&	job.register_root( EBPF_ROOT_EXCLUDE , req_fd )
			;
			if (done) {
				job.root_fds.push_back(req_fd) ;
				req_fd = -1 ;                                                        // keep it open so that dentry stays valid
			}
			ok = ::send( job.conn , &done , 1/*len*/ , MSG_NOSIGNAL )==1 ;
		} break ;
	}
	if (req_fd>=0) ::close(req_fd) ;
	return !ok ;
}

//
// main
//

// must be created while staring
struct Start {
	Start() {
		::string     lck_name  = get_file_name("lck")                                                      ;
		struct flock lck_start { .l_type=F_WRLCK , .l_whence=SEEK_SET , .l_start=0 , .l_len=1 , .l_pid=0 } ; // first byte is only held during startup
		struct flock lck_run   { .l_type=F_WRLCK , .l_whence=SEEK_SET , .l_start=1 , .l_len=1 , .l_pid=0 } ; // second byte is held during the entire daemon's life
		//
		_fd = ::open( lck_name.c_str() , O_RDWR|O_CREAT , 0600 ) ; if (_fd<0 ) throw "cannot open lock file : "s+strerror(errno) ;
		//
		// if another daemon is running, wait for the end of its start up sequence
		int rcs = ::fcntl( _fd , F_SETLKW , &lck_start ) ; if (rcs!=0) throw "cannot detect if another daemon is starting : "s+::strerror(errno) ;
		int rcr = ::fcntl( _fd , F_SETLK  , &lck_run   ) ;                                                                                         // stays acquired until end of process
		if (rcr!=0) {
			if ( errno==EAGAIN || errno==EACCES ) already_running = true ;
			else                                  throw "cannot detect if another daemon is running : "s+::strerror(errno) ;
		}
	}
	void started() {
		struct flock lck { .l_type=F_UNLCK , .l_whence=SEEK_SET , .l_start=0 , .l_len=1 , .l_pid=0 } ;                                             // first byte is only held during startup
		int          rc  = ::fcntl( _fd , F_SETLK  , &lck )                                          ; if (rc!=0) throw "cannot signal end of start-up : "s+::strerror(errno) ;
	}
	// data
	bool already_running = false ;
private :
	int _fd = -1 ;                                                                                                                                 // stays open until end of process
} ;

static struct ebpf* _load_ebpf() {
	::libbpf_set_print(_pr) ;
	ebpf* res = ebpf::open_and_load() ; if (!res ) throw "cannot load autodep ebpf filter"s   ;
	int   rc  = ebpf::attach(res)     ; if (rc!=0) throw "cannot attach autodep ebpf filter"s ;
	_set_syscalls(res) ;
	return res ;
}

int main( int argc , char* argv[] ) {
	try {
		bool test_only = false ;
		bool no_test   = false ;
		for( int i=1 ; i<argc ; i++ )
			if      (::strcmp(argv[i],"--test"   )==0)   test_only = true ;
			else if (::strcmp(argv[i],"--no-test")==0)   no_test   = true ;
			else                                       { fprintf(stderr,"usage : %s [--test|--no-test]\n",argv[0]) ; return 2 ; }
		::filesystem::create_directories(SockDir) ;
		Start start ;
		if (start.already_running) {
			log("daemon already running") ;
			::fprintf(  stdout , "0\n" ) ;                                                                                                       // report another daemon is up and running
			return 0 ;
		}
		reset_log() ;
		_raise_n_open_files(10000) ;
		//
		g_skel = _load_ebpf() ;
		//
		if (!no_test) {
			if (self_test(test_only/*verbose*/))   log("self-test ok"                                                        ) ;
			else                                 { log("self-test failed, accesses cannot be reliably tracked on this system") ; return 1 ; }
		}
		if (!test_only) {
			::string           sock_name = get_sock_name() ;
			struct sockaddr_un addr      = {}              ; addr.sun_family = AF_UNIX ; ::strncpy( addr.sun_path , sock_name.c_str() , sizeof(addr.sun_path)-1 ) ;
			//
			int sock_fd = ::socket( AF_UNIX , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ; if (sock_fd<0) { log_errno("cannot create socket") ; return 1 ; }
			//
			/**/ ::unlink(sock_name.c_str()) ;
			if ( ::bind(sock_fd,(struct sockaddr*)&addr,sizeof(addr))!=0 ) { log_errno("cannot bind %s"     ,sock_name.c_str()) ; return 1 ; }
			/**/ ::chmod(sock_name.c_str(),0666) ;                                                                                               // allow unprivileged clients to connect
			if ( ::listen(sock_fd,64)                                !=0 ) { log_errno("cannot listen on %s",sock_name.c_str()) ; return 1 ; }
			log("ready, listening on %s",sock_name.c_str()) ;
			start.started() ;
			::fprintf( stdout , "1\n" ) ;                                                                                                        // report we are up and running
			::fflush ( stdout         ) ;
			::close(1/*stdout*/) ;                                                                                                               // .
			//
			uint32_t                 job_id = 2                              ;                                                                   // 0 means no job, 1 is used by self-test
			::vector<::epoll_event>  events ;
			::unordered_map<int,Job> jobs   ;
			int                      ep_fd  = ::epoll_create1(EPOLL_CLOEXEC) ; if (ep_fd<0) throw "cannot create epoll fd"s ;
			//
			::epoll_event e { .events=EPOLLIN , .data{.fd=sock_fd} } ;
			if (::epoll_ctl(ep_fd,EPOLL_CTL_ADD,sock_fd,&e)!=0) throw "cannot add master event to epoll fd"s ;
			events.push_back({}) ;
			for( bool first=true ;; first=false ) {
				int timeout  = !first&&events.size()==1/*master*/ ? 300000 : -1                ;                                                 // leave after idle for 300s, except for first ...
				int n_events = ::epoll_wait( ep_fd , events.data() , events.size() , timeout ) ; if (n_events<0) throw "cannot wait epoll fd"s ; // ... conn as starting takes ~3s
				if (!n_events) break ;                                                                                                           // timeout has expired
				//
				for( int i=0 ; i<n_events ; i++ ) {
					::epoll_event const& event = events[i] ;
					if (event.data.fd==sock_fd) {
						int           job_fd = ::accept4( sock_fd , nullptr/*addr*/ , nullptr/*addrlen*/ , SOCK_CLOEXEC ) ;
						pair it_inserted = jobs.try_emplace(job_fd) ; if (!it_inserted.second) throw "cannot insert job in book-keeping"s ;
						Job& job = it_inserted.first->second ;
						job.conn   = job_fd   ;
						job.job_id = job_id++ ;
						_start_job(/*inout*/job) ;
						::epoll_event e      { .events=EPOLLIN , .data{.fd=job_fd} } ;
						::epoll_ctl( ep_fd , EPOLL_CTL_ADD , job_fd , &e ) ;
						events.push_back({}) ;
					} else {
						auto it = jobs.find(event.data.fd) ; if (it==jobs.end()) throw "cannot retreive job in book-keeping"s ;
						if (_process_job(/*inout*/it->second)) {
							::epoll_ctl( ep_fd , EPOLL_CTL_DEL , event.data.fd , nullptr ) ;
							::close(event.data.fd) ;
							events.pop_back() ;
							jobs.erase(it) ;
						}
					}
				}
			}
		}
	}
	catch (::string    const& e) { log("%s",e.c_str()) ; return 1 ; }
	catch (::exception const& e) { log("%s",e.what() ) ; return 1 ; }
	return 0 ;
}
