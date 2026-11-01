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
// This is a standalone program (it deliberately avoids the lmake app framework) : build it, then run it as root, e.g. : sudo _bin/lmake_ebpfd
// options :
//   --test    : run the self-test verbosely (events are dumped) and exit
//   --no-test : skip the self-test (for debug only)

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#include <sys/syscall.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include <atomic>
#include <charconv>
#include <concepts>
#include <exception>
#include <filesystem>
#include <future>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "sys_config.h"

#include "syscall_tab.h"

#include "ebpf_client.hh"
#include "ebpf_utils.hh"

#include "ebpf_event.h"
#include "src/autodep/ebpf.skel.h" // must follow ebpf_event.h, auto-generated, so must be located explicitly

static constexpr uint32_t RingBufSz = 4u<<20 ; // comfortable : events are small as paths are relative to roots and uninteresting accesses are filtered out in the kernel ...
/**/                                           // ... /!\ this memory is allocated in the kernel for each running job

using namespace std                  ;
using namespace std::string_literals ;
using namespace EbpfDaemon           ;

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

::atomic<bool> g_self_test_started = false ;
::atomic<bool> g_self_test_done    = false ;
::string       g_self_test_msg     ;

struct ebpf* _g_skel         = nullptr ;
int          _g_ring_bufs_fd = -1      ;

static int _pr( enum libbpf_print_level lvl , const char* fmt , va_list ap ) {
	if (lvl>LIBBPF_WARN)                                                       return 0 ;   // higher lvl means less critical
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

static constexpr struct ebpf const* EbpfTmpl  = nullptr                                                                        ;
static constexpr size_t             NSyscalls = sizeof(EbpfTmpl->rodata->Syscalls[0])/sizeof(EbpfTmpl->rodata->Syscalls[0][0]) ;

static constexpr bool SyscallsOk = []() {
	//
	bool ok = true ;
	// tracked syscalls
	#define X(   name , kind , d1,p1 , d2,p2 , fd,mod,fl,msk,nf ) if ( long nr=MACRO_VAL(SYS_##name  ,-1L) ; nr>=0 ) ok &= ulong(nr)<NSyscalls ;
	#define X32( name , kind , d1,p1 , d2,p2 , fd,mod,fl,msk,nf ) if ( long nr=MACRO_VAL(SYS32_##name,-1L) ; nr>=0 ) ok &= ulong(nr)<NSyscalls ;
	EBPF_TRACKED_SYSCALLS(X)
	#if HAS_32
		EBPF_TRACKED_SYSCALLS(X32)
	#endif
	#undef X
	#undef X32
	#define X(  name) if ( long nr=MACRO_VAL(SYS_##name  ,-1L) ; nr>=0 ) ok &= ulong(nr)<NSyscalls ;
	#define X32(name) if ( long nr=MACRO_VAL(SYS32_##name,-1L) ; nr>=0 ) ok &= ulong(nr)<NSyscalls ;
	EBPF_UNTRACKED_SYSCALLS(X)
	#if HAS_32
		EBPF_UNTRACKED_SYSCALLS(X32)
	#endif
	#undef X
	#undef X32
	return ok ;
} () ;
static_assert( SyscallsOk ) ;

static constexpr ::array<::array<EbpfScEntry,NSyscalls>,1+HAS_32> Syscalls = []() {
	::array<::array<EbpfScEntry,NSyscalls>,2/*is_32*/> res ;
	// tracked syscalls
	#define X(  name,kind,d1,p1,d2,p2,fd,mod,fl,msk,nf) if ( long nr=MACRO_VAL(SYS_##name  ,-1L) ; nr>=0 ) res[0][nr] = EBPF_SCE(kind,d1,p1,d2,p2,fd,mod,fl,msk,nf) ;
	#define X32(name,kind,d1,p1,d2,p2,fd,mod,fl,msk,nf) if ( long nr=MACRO_VAL(SYS32_##name,-1L) ; nr>=0 ) res[1][nr] = EBPF_SCE(kind,d1,p1,d2,p2,fd,mod,fl,msk,nf) ;
	EBPF_TRACKED_SYSCALLS(X)
	#if HAS_32
		EBPF_TRACKED_SYSCALLS(X32)
	#endif
	#undef X
	#undef X32
	return res ;
} () ;

//
// protocol
//

// the syscall table is shared by all jobs
void _ebpf_init(struct ebpf* skel) {
	// syscall table
	for( uint32_t is_32=0 ; is_32<1+HAS_32 ; is_32++ )
		for( uint32_t nr=0 ; nr<NSyscalls ; nr++ )
			skel->rodata->Syscalls[is_32][nr] = Syscalls[is_32][nr] ;
	// mrkr
	skel->rodata->Mrkr = g_mrkr ;
}

//
// main
//

// must be created while starting
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

struct Job {
	static ::set<EbpfJobId> s_free_ids ;
	// cxtors & co
	Job() {
		if (s_free_ids.empty()) throw ::format("cannot allocate new idx") ;
		job_id       = *s_free_ids.begin()                                                                                                            ; s_free_ids.erase(s_free_ids.begin()) ;
		ring_buf_fd  = ::bpf_map_create( BPF_MAP_TYPE_RINGBUF , nullptr/*map_name*/ , 0/*key_size*/ , 0/*value_size*/ , RingBufSz , nullptr/*opts*/ ) ;
		uint32_t job_id_32 = job_id ;
		if (ring_buf_fd<0                                                                    ) throw ::format("job {} : cannot create ring buffer",job_id) ;
		if (::bpf_map_update_elem( _g_ring_bufs_fd , &job_id_32 , &ring_buf_fd , BPF_ANY )!=0) throw ::format("job {} : cannot register"          ,job_id) ;
	}
	~Job() {
		uint32_t job_id_32  = job_id ;
		if (::bpf_map_delete_elem( _g_ring_bufs_fd , &job_id_32 )!=0) log_errno(::format("job {} : cannot unregister",job_id).c_str()) ; // cannot throw in dxtor
		if (ring_buf_fd>=0) ::close(ring_buf_fd) ;
		s_free_ids.insert(job_id) ;
	}
	// sevices
	void send_reply(int fd) {                                                                                // send the reply plus, on success, the ring buffer fd as an SCM_RIGHTS ancillary message
		struct iovec    iov                           = { .iov_base=&job_id , .iov_len=sizeof(EbpfJobId) } ;
		char            cbuf[CMSG_SPACE(sizeof(int))] = {}                                                 ;
		struct msghdr   msg                           = {
			.msg_name       = nullptr
		,	.msg_namelen    = 0
		,	.msg_iov        = &iov
		,	.msg_iovlen     = 1
		,	.msg_control    = cbuf
		,	.msg_controllen = sizeof(cbuf)
		,	.msg_flags      = 0
		} ;
		struct cmsghdr* cm = CMSG_FIRSTHDR(&msg) ;
		cm->cmsg_level = SOL_SOCKET            ;
		cm->cmsg_type  = SCM_RIGHTS            ;
		cm->cmsg_len   = CMSG_LEN(sizeof(int)) ;
		::memcpy( CMSG_DATA(cm) , &ring_buf_fd , sizeof(int) ) ;
		//
		int rc = ::sendmsg( fd , &msg , MSG_NOSIGNAL ) ; if (rc!=ssize_t(sizeof(EbpfJobId))) throw "cannot send reply"s ;
	}

	int       ring_buf_fd = -1 ;
	EbpfJobId job_id      =  0 ;
} ;
::set<EbpfJobId> Job::s_free_ids ;

static void _load_ebpf() {
	int rc ;
	/**/              ::libbpf_set_print(_pr                    ) ;
	_g_skel         = ebpf::open        (                       ) ; if (!_g_skel         ) throw "cannot open autodep ebpf filter"s        ;
	/**/              _ebpf_init        (_g_skel                ) ;                                                                          // init constant tables
	rc              = ebpf::load        (_g_skel                ) ; if (rc!=0            ) throw "cannot load autodep ebpf filter"s        ;
	rc              = ebpf::attach      (_g_skel                ) ; if (rc!=0            ) throw "cannot attach autodep ebpf filter"s      ;
	_g_ring_bufs_fd = ::bpf_map__fd     (_g_skel->maps.ring_bufs) ; if (_g_ring_bufs_fd<0) throw "cannot open autodep ebpf filter maps"s   ;
	//
	if (_g_skel->rodata->NJobs  > 1<<(sizeof(EbpfJobId )*8) ) throw "too many job ids"       ;
	if (_g_skel->rodata->NRoots > 1<<(sizeof(EbpfRootId)*8) ) throw "too many roots per job" ;
	//
	for( size_t i=1 ; i<_g_skel->rodata->NJobs ; i++ ) Job::s_free_ids.insert(EbpfJobId(i)) ;                                                // do not use 0 as it is reserved as sentinel
}

static const char* _g_exe = nullptr ;

[[noreturn]] static void _usage() {
	::fprintf( stderr , "usage : %s [timeout_sec] (default=300s, negative for no timeout)\n" , _g_exe ) ;
	::exit(2) ;
}

void self_test() ;

int main( int argc , char* argv[] ) {
	_g_exe = argv[0] ;
	try {
		if (argc>2) _usage() ;
		int timeout = 300'000 ;                                                                                                          // leave after idle for 300s as starting takes ~3s
		if (argc==2) {
			::errc rc = ::from_chars( argv[1] , argv[1]+::strlen(argv[1]) , /*out*/timeout ).ec ; if (rc!=::errc()) _usage() ;
			if      (timeout>::numeric_limits<int>::max()/1000) _usage() ;
			else if (timeout<0                                ) timeout  = -1   ;
			else                                                timeout *= 1000 ;                                                        // timout is in ms
		}
		//
		::filesystem::create_directories(SockDir) ;
		Start start ;
		if (start.already_running) {
			log("daemon already running") ;
			::printf("0\n") ;                                                                                                            // report another daemon is up and running
			return 0 ;
		}
		reset_log() ;
		_raise_n_open_files(10000) ;
		//
		_load_ebpf() ;
		//
		::string           master_name = get_sock_name()     ;
		const char*        master_cn   = master_name.c_str() ;
		struct sockaddr_un addr        = {}                  ; addr.sun_family = AF_UNIX ; ::strncpy( addr.sun_path , master_cn , sizeof(addr.sun_path)-1 ) ;
		//
		int master_fd = ::socket( AF_UNIX , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) ; if (master_fd<0) { log_errno("cannot create socket") ; return 1 ; }
		//
		/**/::unlink( master_cn                                        )     ;                                                           // best effort
		if (::bind  ( master_fd , (struct sockaddr*)&addr,sizeof(addr) )!=0) { log_errno("cannot bind %s"     ,master_cn) ; return 1 ; }
		/**/::chmod ( master_cn , 0666                                 )     ;                                                           // best effort, allow unprivileged jobs
		if (::listen( master_fd , 64                                   )!=0) { log_errno("cannot listen on %s",master_cn) ; return 1 ; }
		log("ready, listening on %s",master_cn) ;
		start.started() ;
		::fprintf( stdout , "1\n" ) ;                 // report we are up and running
		::fflush ( stdout         ) ;
		::close(1/*stdout*/) ;                        // .
		//
		self_test() ;
		//
		::vector<::epoll_event>  events ;
		::unordered_map<int,Job> jobs   ;
		int                      ep_fd  = ::epoll_create1(EPOLL_CLOEXEC) ; if (ep_fd<0) throw "cannot create epoll fd"s ;
		//
		::epoll_event e { .events=EPOLLIN , .data{.fd=master_fd} } ;
		if (::epoll_ctl(ep_fd,EPOLL_CTL_ADD,master_fd,&e)!=0) throw "cannot add master event to epoll fd"s ;
		events.push_back({}) ;
//		for( uint8_t n_wait_jobs=1 ;;) {              // wait for self-test + first job (that launched us)
		for( uint8_t n_wait_jobs=0 ;;) {              // wait for first job (that launched us)
			int to = timeout ;
			if (n_wait_jobs               ) to = -1 ; // waiting for mandatory jobs (self_test + job that launched us)
			if (events.size()!=1/*master*/) to = -1 ;                                                                                    // waiting for on going jobs
			int n_events = ::epoll_wait( ep_fd , events.data() , events.size() , to ) ; if ( n_events<0) throw "cannot wait epoll fd"s ;
			/**/                                                                        if (!n_events  ) break ;                         // timeout has expired
			//
			int events_sz = events.size() ;
			for( int i=0 ; i<n_events ; i++ ) {
				::epoll_event const& event = events[i] ;
				if (event.data.fd==master_fd) {
					int           job_fd      = ::accept4( master_fd , nullptr/*addr*/ , nullptr/*addrlen*/ , SOCK_CLOEXEC ) ;
					::pair        it_inserted = jobs.try_emplace(job_fd)                                                     ; if (!it_inserted.second) throw "cannot insert job in book-keeping"s ;
					Job&          job         = it_inserted.first->second                                                    ;
					::epoll_event e           { .events=EPOLLIN , .data{.fd=job_fd} }                                        ;
					if (::epoll_ctl( ep_fd , EPOLL_CTL_ADD , job_fd , &e )!=0) throw "cannot add new job to epoll"s ;
					job.send_reply(job_fd) ;
					events_sz++ ;
					if (n_wait_jobs) n_wait_jobs-- ;
				} else {
					auto it = jobs.find(event.data.fd) ; if (it==jobs.end()) throw "cannot retreive job in book-keeping"s ;
					if (::epoll_ctl( ep_fd , EPOLL_CTL_DEL , event.data.fd , nullptr )!=0) throw "cannot remove done job from epoll"s ;
					::close(event.data.fd) ;
					events_sz-- ;
					jobs.erase(it) ;
				}
			}
			events.resize(events_sz) ;
		}
	}
	catch (::string    const& e) { log("daemon : %s",e.c_str()) ; return 1 ; }
	catch (::exception const& e) { log("daemon : %s",e.what() ) ; return 1 ; }
	return 0 ;
}

//
// self test : check all hooks are activated in ebpf filter
//

struct Consumer {
	void handle_event( EbpfEventHdr  const&                                       ) {}
	void handle_event( EbpfEventBase const& event , Path const& , Path const& ={} ) {
		if (event.hdr.file_kind==EBPF_FILE_USER) n_unsolved++ ;
		else                                     n_solved  ++ ;
	}
	// data
	int n_solved   = 0 ;
	int n_unsolved = 0 ;
} ;

struct IoUring {
	static constexpr long QueueSz = 16 ;
	template<class T> static T* s_map( size_t sz , int fd , off_t off ) {
		return static_cast<T*>(::mmap( nullptr , sz , PROT_READ|PROT_WRITE , MAP_SHARED|MAP_POPULATE , fd , off )) ;
	}
	// cxtors & co
	IoUring() {
		params.flags          |= IORING_SETUP_SQPOLL                                                ;
		params.sq_thread_idle  = 0                                                                  ;
		ring_fd                = ::syscall( SYS_io_uring_setup , QueueSz , &params )                ; if (ring_fd<0         ) throw "cannot setup io_uring"s       ;
		sq_ring_sz             = params.sq_off.array + params.sq_entries*sizeof(uint32_t)           ;
		sqe_sz                 =                      params.sq_entries*sizeof(::io_uring_sqe)      ;
		sq_ptr                 = s_map<char          >( sq_ring_sz , ring_fd , IORING_OFF_SQ_RING ) ; if (sq_ptr==MAP_FAILED) throw "cannot map io_uring"s         ;
		sqes                   = s_map<::io_uring_sqe>( sqe_sz     , ring_fd , IORING_OFF_SQES    ) ; if (sqes  ==MAP_FAILED) throw "cannot map io_uring entries"s ;
		sq_tail                =  reinterpret_cast<uint    *>( sq_ptr + params.sq_off.tail      )   ;
		sq_array               =  reinterpret_cast<uint    *>( sq_ptr + params.sq_off.array     )   ;
		sq_flags               =  reinterpret_cast<uint32_t*> (sq_ptr + params.sq_off.flags     )   ;
		ring_mask              = *reinterpret_cast<uint    *>( sq_ptr + params.sq_off.ring_mask )   ;
	}
	~IoUring() {
		::munmap( sqes   , sqe_sz     ) ;
		::munmap( sq_ptr , sq_ring_sz ) ;
		::close ( ring_fd             ) ;
	}
	// services
	void submit( uint8_t op , int fd1 , char const* addr1 , int fd2=-1 , char const* addr2=nullptr , uint32_t flags=0 ) {
		uint            tail = *sq_tail  ; if ((tail&ring_mask)!=tail) throw "queue is too short"s ;
		::io_uring_sqe& sqe = sqes[tail] ; sqe = {} ;
		//
		/**/                                               sqe.flags      = IOSQE_ASYNC|IOSQE_IO_LINK ; // ensure ordered execution
		/**/                                               sqe.opcode     =       op                  ;
		/**/                                               sqe.fd         =       fd1                 ;
		/**/                                               sqe.addr       = ulong(addr1)              ;
		if ( fd2!=-1                                     ) sqe.len        =       fd2                 ;
		if ( addr2                                       ) sqe.addr2      = ulong(addr2)              ;
		if ( op==IORING_OP_STATX || op==IORING_OP_OPENAT ) sqe.open_flags = flags ;
		//
		sq_array[tail] = tail ;
		__atomic_store_n( sq_tail , tail+1 , __ATOMIC_RELEASE ) ;
		n_reqs++ ;
	}
	void flush() {
		if (__atomic_load_n(sq_flags,__ATOMIC_ACQUIRE)&IORING_SQ_NEED_WAKEUP) ::syscall( SYS_io_uring_enter , ring_fd , 0 , 0 , IORING_ENTER_SQ_WAKEUP , nullptr , 0 ) ;
		int rc = ::syscall( SYS_io_uring_enter, ring_fd , 0 , n_reqs , IORING_ENTER_GETEVENTS , nullptr , 0 ) ; if (rc!=0) throw "cannot wait fo io_uring to complete"s ;
	}
	// data
	::io_uring_params params     = {}      ;
	int               ring_fd    = -1      ;
	size_t            sq_ring_sz = 0       ;
	size_t            sqe_sz     = 0       ;
	char          *   sq_ptr     = nullptr ;
	::io_uring_sqe*   sqes       = nullptr ;
	uint          *   sq_tail    = nullptr ;
	uint          *   sq_array   = nullptr ;
	uint32_t      *   sq_flags   = nullptr ;
	uint              ring_mask  = 0       ;
	uint              n_reqs     = 0       ;
} ;

// goal of self test is to ensure all ebpf hooks are actually attached
// obvious (else job tracking would not start at all) :
//	tp/syscalls/sys_enter_fsetxattr
//	tp_btf/sched_process_fork
//	tp_btf/sched_process_exit
::pair<int/*n_solved*/,int/*n_unsolved*/> tracee(::string dir_name) {
	int n_unsolved = 0 ;
	int n_solved   = 0 ;
	RingBufBase::s_start_job() ;
	//
	int          dir_fd ;
	int          fd     ;
	struct stat  st     ;
	struct statx stx    ;
	//
	dir_fd = ::open( dir_name.c_str() , O_DIRECTORY ) ;                                       n_solved += 1 ;                      //                                   fexit/do_filp_open
	fd     = ::openat   (       dir_fd,"f"  , O_CREAT|O_WRONLY|O_TRUNC,0644 ) ; ::close(fd) ; n_solved += 1 ;                      // fentry/security_path_mknod        fexit/do_filp_open
	/**/     ::symlinkat( "f" , dir_fd,"l"                                  ) ;               n_solved += 1 ;                      // fentry/security_path_symlink      tp_btp/sys_exit
	/**/     ::mkdirat  (       dir_fd,"d"  , 0755                          ) ;               n_solved += 1 ;                      // fentry/security_path_mkdir        tp_btp/sys_exit
	/**/     ::linkat   (       dir_fd,"f"  , dir_fd,"f2",0                 ) ;               n_solved += 1 ;                      // fentry/security_path_link         tp_btp/sys_exit
	/**/     ::renameat (       dir_fd,"f2" , dir_fd,"f3"                   ) ;               n_solved += 1 ;                      // fentry/security_path_rename       tp_btp/sys_exit
	/**/     ::unlinkat (       dir_fd,"d"  , AT_REMOVEDIR                  ) ;               n_solved += 1 ;                      // fentry/security_path_rmdir        tp_btp/sys_exit
	fd     = ::openat   (       dir_fd,"l"  , O_RDONLY                      ) ; ::close(fd) ; n_solved += 2 ;                      // fentry/security_inode_follow_link fexit/do_filp_open
	/**/     ::fstatat  (       dir_fd,"l"  , &st,0                         ) ;               n_solved += 2 ;                      // fexit/filename_lookup             tp_btp/sys_exit
	/**/     ::unlinkat (       dir_fd,"l"  , 0                             ) ;               n_solved += 1 ;                      // fentry/security_path_unlink       tp_btp/sys_exit
	//
	IoUring uring ;                                                                                                                // tp_btf/task_newtask
	uring.submit( IORING_OP_STATX     , dir_fd , "f"   , -1     , reinterpret_cast<char*>(&stx) , STATX_SIZE ) ; n_unsolved += 1 ; // fexit/io_statx
	uring.submit( IORING_OP_MKDIRAT   , dir_fd , "d"   , 0755                                                ) ; n_solved   += 1 ; // fexit/io_mkdirat
	uring.submit( IORING_OP_SYMLINKAT , dir_fd , "f"   , -1     , "l"                                        ) ; n_solved   += 1 ; // fexit/io_symlinkat,
	uring.submit( IORING_OP_LINKAT    , dir_fd , "f"   , dir_fd , "f4"                                       ) ; n_solved   += 1 ; // fexit/io_linkat
	uring.submit( IORING_OP_RENAMEAT  , dir_fd , "f4"  , dir_fd , "f5"                                       ) ; n_solved   += 1 ; // fexit/io_renameat
	uring.submit( IORING_OP_UNLINKAT  , dir_fd , "f5"                                                        ) ; n_solved   += 1 ; // fexit/io_unlinkat
	uring.flush() ;
	//
	::close(dir_fd) ;
	return { n_solved , n_unsolved } ;
}

void self_test() {
	::string msg ;
	try {
		::string          dir_name = get_file_name("test_dir")                                ;
		Job               job      ;
		RingBuf<Consumer> ring_buf { job.job_id , job.ring_buf_fd , false/*deps_in_system*/ } ;
		//
		::filesystem::remove_all( dir_name                ) ;
		::mkdir                 ( dir_name.c_str() , 0777 ) ;
		RingBufBase::s_add_root( 1/*root_id*/ , dir_name ) ;                                    // 0 is reserved for absolute paths
		//
		::future<::pair<int/*solved*/,int/*unsolved*/>> n_events = ::async( ::launch::async , tracee , dir_name ) ; // launch tracee
		//
		int           fd    = ::epoll_create1(EPOLL_CLOEXEC)                                 ;
		::epoll_event event { .events=EPOLLIN , .data={} }                                   ;
		int           rc    = ::epoll_ctl( fd , EPOLL_CTL_ADD , ring_buf.epoll_fd , &event ) ; if (rc!=0) throw "cannot epoll"s ;
		for (;;) {
			::epoll_wait( fd , &event , 1/*maxevents*/ , -1/*timeout*/ ) ;
			if (ring_buf.consume()) break ;
		}
		::pair<int/*solved*/,int/*unsolved*/> n_expected = n_events.get() ;
		if (ring_buf.consumer.n_solved  !=n_expected.first ) throw ::format( "bad solved event count : expected {}, received {}"   , n_expected.first ,ring_buf.consumer.n_solved  ) ;
		if (ring_buf.consumer.n_unsolved!=n_expected.second) throw ::format( "bad unsolved event count : expected {}, received {}" , n_expected.second,ring_buf.consumer.n_unsolved) ;
	}
	catch (::string    const& e) { msg = e        ; }
	catch (::exception const& e) { msg = e.what() ; }
	if (!msg.empty()) {
		fprintf( stderr , "self-test broken : %s\n" , msg.c_str() ) ;
		exit(2) ;
	}
}
