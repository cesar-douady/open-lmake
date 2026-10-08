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

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/io_uring.h>
#include <linux/openat2.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include <atomic>
#include <concepts>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sys_config.h"

#include "ebpf_daemon.hh"
#include "ebpf_event.h"
#include "src/autodep/ebpf.skel.h" // auto-generated, so must include dir so the -M flag works

using namespace std ;

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

static constexpr uint32_t MaxExcludes  = 16     ; // max number of dirs a job can exclude, so a client cannot make the daemon hold an unbounded number of fds
static constexpr time_t   HelloTimeout = 10     ; // in seconds, a client must not be able to hold a thread without registering a job
static constexpr uint32_t RingBufSz    = 4u<<20 ; // comfortable : events are small as paths are relative to roots and uninteresting accesses are filtered out in the kernel ...
/**/                                              // ... /!\ this memory is allocated in the kernel for each running job

static struct ebpf*          g_skel        = nullptr ;
static ::atomic<uint32_t>    g_next_job_id = 2       ; // 0 means no job, 1 is used by self-test
static ::mutex               g_reg_mutex   ;           // root registration and syscall table are unique resources
static volatile sig_atomic_t g_stop        = 0       ;

static void _on_signal(int) {
	g_stop = 1 ;
}

static void _log( const char* fmt , ... ) {
	time_t    t      = time(nullptr) ;
	struct tm tm     ;                 localtime_r(&t,&tm)                    ;
	char      ts[32] ;                 strftime(ts,sizeof(ts),"%H:%M:%S",&tm) ;
	va_list   ap     ;                 va_start(ap,fmt)                       ;
	fprintf (        stderr , "lmake_bpfd %s : ",ts ) ;
	vfprintf(        stderr , fmt               ,ap ) ;
	fputc   ( '\n' , stderr                         ) ;
	va_end(ap) ;
}

static int _pr( enum libbpf_print_level lvl , const char* fmt , va_list ap ) {
	if (lvl>LIBBPF_WARN) return 0                       ;
	else                 return vfprintf(stderr,fmt,ap) ;
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
// jobs
//

struct Job {
	// cxtors & co
	~Job() {
		::bpf_map_delete_elem( bpf_map__fd(g_skel->maps.job_rbs) , &job_id ) ;
		::bpf_map_delete_elem( bpf_map__fd(g_skel->maps.job_cfg) , &job_id ) ;
		for( RootKey const& rk : root_keys ) ::bpf_map_delete_elem( bpf_map__fd(g_skel->maps.roots) , &rk ) ;
		for( int fd            : root_fds  ) ::close(fd   ) ;
		if (rb_fd>=0)                        ::close(rb_fd) ;
		if (conn >=0)                        ::close(conn ) ;
	}
	//services
	// a root is identified in the kernel by its dentry, which user space cannot know
	// so we do a lookup of the dir with a magic dir fd while a registration is armed and the hook on this lookup records the dentry
	// dir_fd must be kept open as long as the root is registered so that dentry stays valid
	bool/*ok*/ register_root( uint32_t idx , int dir_fd ) {
		::lock_guard<::mutex> lock     { g_reg_mutex }                                                ;
		int                   fd       = bpf_map__fd(g_skel->maps.root_reg)                           ;
		uint32_t              zero     = 0                                                            ;
		RootReg               rr       { .job_id=job_id , .idx=idx , .armed=1 , .done=0 , .dentry=0 } ;
		char                  path[64]                                                                ; snprintf(path,sizeof(path),"/proc/self/fd/%d",dir_fd) ;
		//
		if (::bpf_map_update_elem(fd,&zero,&rr,BPF_ANY)!=0) {
			_log("job %u : cannot update map : %s",job_id,strerror(errno)) ;
			return false ;
		}
		::syscall( SYS_faccessat , EBPF_ROOT_FD , path , F_OK ) ;                                                                       // dir fd is ignored as path is absolute, it is just a marker
		if (::bpf_map_lookup_elem(fd,&zero,&rr)!=0) { _log("job %u : cannot lookup map : %s",job_id,strerror(errno)) ; return false ; }
		if (!rr.done                              ) { _log("job %u : cannot retrieve : %s"  ,job_id,strerror(errno)) ; return false ; }
		root_keys.push_back({ .dentry=rr.dentry , .job_id=job_id , .pad=0 }) ;
		return true ;
	}
	// create the ring buffer of job and register it in the kernel, root_fds are those of hello
	// in case of error, caller must release job, as when it is done
	bool/*ok*/ mk_job(Hello const& hello) {
		rb_fd = ::bpf_map_create( BPF_MAP_TYPE_RINGBUF , nullptr/*map_name*/ , 0/*key_size*/ , 0/*value_size*/ , RingBufSz , nullptr/*opts*/ ) ;
		if (rb_fd<0) {
			_log("job %u : cannot create ring buffer : %s",job_id,strerror(errno)) ;
			return false ;
		}
		JobCfg cfg { .pidns_dev=hello.pidns_dev , .pidns_ino=hello.pidns_ino , .seq=0 , .lost=0 , .deps_in_system=hello.deps_in_system } ;
		if (
			::bpf_map_update_elem( bpf_map__fd(g_skel->maps.job_rbs) , &job_id , &rb_fd , BPF_ANY )!=0
		||	::bpf_map_update_elem( bpf_map__fd(g_skel->maps.job_cfg) , &job_id , &cfg   , BPF_ANY )!=0
		) {
			_log("job %u : cannot register : %s",job_id,strerror(errno)) ;
			return false ;
		}
		if (hello.n_roots!=root_fds.size()) { _log("job %u : internal error (size mismatch %u!=%u)",job_id,hello.n_roots,root_fds.size()) ; return false ; }
		if (hello.n_roots>EBPF_MAX_ROOTS  ) { _log("job %u : internal error (root overflow)"       ,job_id                              ) ; return false ; }
		for( uint32_t i=0 ; i<root_fds.size() ; i++ )
			if (!register_root( i , root_fds[i] )) { _log("job %u : cannot register root %u",job_id,i) ; return false ; }
		return true ;
	}
	// data
	int               conn      = -1 ;
	int               rb_fd     = -1 ;
	uint32_t          job_id    =  0 ;
	::vector<int>     root_fds  = {} ; // roots are identified by their dentry in the kernel, keep them open so dentries stay valid
	::vector<RootKey> root_keys = {} ; // what has been registered in the kernel
} ;

//
// protocol
//

// the syscall table is shared by all jobs
bool/*ok*/ _set_syscalls() {
	int fd = ::bpf_map__fd(g_skel->maps.syscalls) ;
	for( uint32_t is32=0 ; is32<1+HAS_32 ; is32++ )
		for( uint32_t nr=0 ; nr<EBPF_NSYSCALLS ; nr++ ) {
			uint32_t k = is32*EBPF_NSYSCALLS + nr ;
			if (::bpf_map_update_elem( fd , &k , &Syscalls[is32][nr] , BPF_ANY )!=0) return false ;
		}
	return true ;
}

// receive hello and the root dir fds sent as an SCM_RIGHTS ancillary message
static ::vector<int>/*root_fds*/ _recv_hello( int conn , Hello&/*out*/ hello ) {
	struct SetTimeout {
		SetTimeout (int fd_) : fd{fd_} { set_timeout(HelloTimeout) ; }
		~SetTimeout(       )           { set_timeout(            ) ; }
		//
		void set_timeout(time_t secs=0) {
			struct timeval tv = { .tv_sec=secs , .tv_usec=0 } ;
			::setsockopt( fd , SOL_SOCKET , SO_RCVTIMEO , &tv , sizeof(tv) ) ;
			::setsockopt( fd , SOL_SOCKET , SO_SNDTIMEO , &tv , sizeof(tv) ) ;
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
static bool/*ok*/ _send_reply( int conn , Reply const& reply , int rb_fd/*-1 if none*/ ) {
	struct iovec  iov                           = { .iov_base=const_cast<Reply*>(&reply) , .iov_len=sizeof(reply) } ;
	char          cbuf[CMSG_SPACE(sizeof(int))] = {}                                                                ;
	struct msghdr msg                           = {}                                                                ; msg.msg_iov = &iov ; msg.msg_iovlen = 1 ;
	if (rb_fd>=0) {
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
// self-test
//

static ::string _mk_path( uint8_t const* data , uint32_t len ) { // components are stored leaf first, each followed by a null
	::string res ;
	uint32_t end = len ;
	while (end) {
		uint32_t start = end-1 ;                                 // skip terminating null
		while ( start && data[start-1] ) start-- ;
		if (!res.empty()) res += '/' ;
		res.append( reinterpret_cast<const char*>(data+start) , end-1-start ) ;
		end = start ;
	}
	return res ;
}

static const char* _kind_str(uint8_t k) {
	switch (k) {
		case EBPF_PROC_START : return "proc_start" ;
		case EBPF_PROC_END   : return "proc_end"   ;
		case EBPF_LINK       : return "link"       ;
		case EBPF_LOOKUP     : return "lookup"     ;
		case EBPF_OPEN       : return "open"       ;
		case EBPF_CREATE     : return "create"     ;
		case EBPF_UNLINK     : return "unlink"     ;
		case EBPF_RENAME_SRC : return "rename_src" ;
		case EBPF_RENAME_DST : return "rename_dst" ;
		case EBPF_DIR        : return "dir"        ;
		case EBPF_RAW        : return "raw"        ;
		case EBPF_END        : return "end"        ;
		default              : return "???"        ;
	}
}

struct TestEvent {
	uint8_t  kind  = 0  ;
	uint8_t  flags = 0  ;
	uint8_t  is32  = 0  ;
	uint8_t  root  = 0  ;
	int32_t  nr    = 0  ;
	int64_t  rc    = 0  ;
	uint64_t seq   = 0  ;
	uint32_t tid   = 0  ;
	::string str0  = {} ;
	::string str1  = {} ;
} ;
struct TestCtx {
	::vector<TestEvent> events  = {}    ;
	bool                verbose = false ;
} ;

static int _test_event( void* ctx_ , void* data , size_t sz ) {
	TestCtx&         ctx = *static_cast<TestCtx        *>(ctx_) ;
	EbpfEvent const& ev  = *static_cast<EbpfEvent const*>(data) ;
	if (sz<EBPF_HDR_SZ) return 0 ;
	TestEvent te { .kind=ev.kind , .flags=ev.flags , .is32=ev.is32 , .root=ev.root , .nr=ev.nr , .rc=ev.rc , .seq=ev.seq , .tid=ev.tid } ;
	uint32_t  l0 = ::min<uint32_t>( ev.len[0] , sz-EBPF_HDR_SZ    )                                                                      ;
	uint32_t  l1 = ::min<uint32_t>( ev.len[1] , sz-EBPF_HDR_SZ-l0 )                                                                      ;
	if (ev.kind==EBPF_RAW) { te.str0 = _mk_path(ev.data,l0)                                                 ; if (ev.root==EBPF_ROOT_ABS) te.str0 = '/'+te.str0 ; }
	else                     te.str0 = _mk_path(ev.data,l0)                                                 ;
	if (l1               )   te.str1 = ::string( reinterpret_cast<const char*>(ev.data+l0) , l1-1/*null*/ ) ;
	if (ctx.verbose) {
		fprintf( stderr , "  %4llu tid=%-7u %-10s" , (unsigned long long)te.seq , te.tid , _kind_str(te.kind) ) ;
		switch (ev.kind) {
			case EBPF_END : {
				const char* io      = ev.flags&EBPF_F_IO      ? "(io_uring)" : "" ;
				const char* is32    = te.is32                 ? "(32)"       : "" ;
				const char* discard = ev.flags&EBPF_F_DISCARD ? " discard"   : "" ;
				fprintf( stderr , " nr=%d%s%s rc=%lld%s open_flags=0%o" , te.nr , io , is32 , (long long)te.rc , discard , ev.open_flags ) ;
			} break ;
			case EBPF_PROC_START : fprintf( stderr , " pid=%u parent=%d"     , ev.pid , te.nr                           ) ; break ;
			case EBPF_PROC_END   :                                                                                          break ;
			case EBPF_RAW        : fprintf( stderr , " [%d] dir=%s path=%s" , te.nr , te.str0.c_str() , te.str1.c_str() ) ; break ;
			default :
				fprintf( stderr , " root=%u path=%s" , te.root , te.str0.c_str() ) ;
				if (ev.flags&EBPF_F_TARGET ) fprintf( stderr , " target=%s" , te.str1.c_str()                                                                                      ) ;
				if (ev.flags&EBPF_F_EXISTS ) fprintf( stderr , " mode=0%o size=%llu mtime=%lld.%09u" , ev.mode , (unsigned long long)ev.size , (long long)ev.mtime_s , ev.mtime_ns ) ;
				else                         fprintf( stderr , " absent"                                                                                                           ) ;
				if (ev.flags&EBPF_F_CREATED) fprintf( stderr , " created"                                                                                                          ) ;
				if (ev.kind==EBPF_OPEN     ) fprintf( stderr , " open_flags=0%o" , ev.open_flags                                                                                   ) ;
		}
		fputc('\n',stderr) ;
	}
	ctx.events.push_back(::move(te)) ;
	return 0 ;
}

// submit a single operation through io_uring, return its result (<=-1000 if io_uring is not usable)
// in struct io_uring_sqe, off is also addr2 and open_flags is also the flags of all other operations
static long _uring_op( uint8_t opcode , int fd , const void* addr , uint64_t off , uint32_t len , uint32_t op_flags , unsigned sqe_flags=0 ) {
	struct io_uring_params p       = {}                                 ;
	int                    ring_fd = ::syscall(SYS_io_uring_setup,4,&p) ; if (ring_fd<0) return -1000-errno ;
	//
	size_t sq_sz   = p.sq_off.array + p.sq_entries*sizeof(unsigned)                                                                    ;
	size_t cq_sz   = p.cq_off.cqes  + p.cq_entries*sizeof(struct io_uring_cqe)                                                         ;
	bool   single  = p.features&IORING_FEAT_SINGLE_MMAP                                                                                ; if (single        )   sq_sz = ::max(sq_sz,cq_sz) ;
	char*  sq      = static_cast<char*>(::mmap(nullptr,sq_sz,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_POPULATE,ring_fd,IORING_OFF_SQ_RING)) ; if (sq==MAP_FAILED) { ::close(ring_fd) ; return -2000-errno ; }
	char*  cq      = single ? sq : static_cast<char*>(::mmap(nullptr,cq_sz,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_POPULATE,ring_fd,IORING_OFF_CQ_RING)) ;
	size_t sqes_sz = p.sq_entries*sizeof(struct io_uring_sqe) ;
	auto*  sqes    = static_cast<struct io_uring_sqe*>(::mmap(nullptr,sqes_sz,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_POPULATE,ring_fd,IORING_OFF_SQES)) ;
	//
	unsigned*            sq_tail  = reinterpret_cast<unsigned*>(sq+p.sq_off.tail     )       ;
	unsigned*            sq_mask  = reinterpret_cast<unsigned*>(sq+p.sq_off.ring_mask)       ;
	unsigned*            sq_array = reinterpret_cast<unsigned*>(sq+p.sq_off.array    )       ;
	unsigned*            cq_head  = reinterpret_cast<unsigned*>(cq+p.cq_off.head     )       ;
	unsigned*            cq_mask  = reinterpret_cast<unsigned*>(cq+p.cq_off.ring_mask)       ;
	auto*                cqes     = reinterpret_cast<struct io_uring_cqe*>(cq+p.cq_off.cqes) ;
	unsigned             tail     = *sq_tail                                                 ;
	unsigned             idx      = tail&*sq_mask                                            ;
	struct io_uring_sqe& sqe      = sqes[idx]                                                ; ::memset(&sqe,0,sizeof(sqe)) ;
	//
	sqe.opcode     = opcode          ;
	sqe.fd         = fd              ;
	sqe.addr       = uintptr_t(addr) ;
	sqe.off        = off             ;
	sqe.len        = len             ;
	sqe.open_flags = op_flags        ;
	sqe.flags      = sqe_flags       ;
	sq_array[idx]  = idx             ;
	__atomic_store_n(sq_tail,tail+1,__ATOMIC_RELEASE) ;
	long r   = ::syscall(SYS_io_uring_enter,ring_fd,1,1,IORING_ENTER_GETEVENTS,nullptr,0) ;
	long res = r<0 ? -3000-errno : cqes[*cq_head&*cq_mask].res                            ;
	/**/         ::munmap(sqes,sqes_sz) ;
	if (!single) ::munmap(cq  ,cq_sz  ) ;
	/**/         ::munmap(sq  ,sq_sz  ) ;
	/**/         ::close (ring_fd     ) ;
	return res ;
}
static long _uring_open( const char* path , unsigned sqe_flags ) {
	long res = _uring_op( IORING_OP_OPENAT , AT_FDCWD , path , 0 , 0 , O_RDONLY , sqe_flags ) ;
	if (res>=0) ::close(res) ;
	return res ;
}

// facilities that may legitimately be unusable on a system, in which case what relies on them cannot be checked
// they are reported by the traced child through its exit code
static constexpr uint8_t TF_None    = 0    ;
static constexpr uint8_t TF_IoUring = 1<<0 ; // io_uring may be disabled (sysctl kernel.io_uring_disabled, seccomp, ...)
static constexpr uint8_t TF_32      = 1<<1 ; // 32-bit syscalls may not be supported by the kernel
static constexpr uint8_t TF_Exec    = 1<<2 ; // the dir used for the test may be mounted noexec
static constexpr uint8_t TF_Mark    = 1<<6 ; // set in exit code so that it cannot be confused with a failure

struct TestExpect {
	const char* name  ;
	long        nr    ;           // syscall number (or io_uring opcode if io)
	bool        io    ;
	bool        ok    ;           // expected result
	uint32_t    kinds ;           // captures that must be seen before End
	const char* hooks ;           // hooks involved, to help diagnostic
	uint8_t     needs = TF_None ; // facility without which this cannot be checked
} ;

// the scenario executed by the traced child, in dir (which is the registered root)
// /!\ must be in sync with _g_test_expects
static void _test_child( const char* dir , uint32_t job_id ) {
	char c ;
	if (::chdir(dir)!=0) ::_exit(2) ;
	::syscall( SYS_readlinkat , EBPF_REGISTER_FD , "" , &c , long(job_id) ) ; // self-register
	struct stat st        ;
	char        buf[4096] ;
	long        fd        ;
	fd = ::syscall( SYS_openat     ,       AT_FDCWD , "f"    , O_CREAT|O_WRONLY|O_TRUNC , 0644       ) ; ::close(fd) ;
	fd = ::syscall( SYS_openat     ,       AT_FDCWD , "f"    , O_RDONLY                              ) ; ::close(fd) ;
	/**/ ::syscall( SYS_newfstatat ,       AT_FDCWD , "f"    , &st , 0                               ) ;
	/**/ ::syscall( SYS_faccessat2 ,       AT_FDCWD , "f"    , R_OK                     , AT_EACCESS ) ;
	/**/ ::syscall( SYS_symlinkat  , "f" , AT_FDCWD , "l"                                            ) ;
	fd = ::syscall( SYS_openat     ,       AT_FDCWD , "l"    , O_RDONLY                              ) ; ::close(fd) ;
	/**/ ::syscall( SYS_readlinkat ,       AT_FDCWD , "l"    , buf , sizeof(buf)                     ) ;
	/**/ ::syscall( SYS_mkdirat    ,       AT_FDCWD , "d"    ,                            0755       ) ;
	/**/ ::syscall( SYS_linkat     ,       AT_FDCWD , "f"    , AT_FDCWD , "d/h"  , 0                 ) ;
	/**/ ::syscall( SYS_renameat2  ,       AT_FDCWD , "d/h"  , AT_FDCWD , "d/h2" , 0                 ) ;
	/**/ ::syscall( SYS_fchmodat   ,       AT_FDCWD , "f"    ,                            0600       ) ;
	/**/ ::syscall( SYS_truncate   ,                  "f"    , 0                                     ) ;
	/**/ ::syscall( SYS_unlinkat   ,       AT_FDCWD , "d/h2" , 0                                     ) ;
	/**/ ::syscall( SYS_unlinkat   ,       AT_FDCWD , "d"    , AT_REMOVEDIR                          ) ;
	/**/ ::syscall( SYS_mknodat    ,       AT_FDCWD , "p"    , S_IFIFO|0644 , 0                      ) ;
	/**/ ::syscall( SYS_chdir      ,                  "."                                            ) ;
	fd = ::syscall( SYS_openat     ,       AT_FDCWD , "."    , O_RDONLY|O_DIRECTORY                  ) ;
	/**/ ::syscall( SYS_getdents64 ,       fd                , buf , sizeof(buf)                     ) ; ::close(fd) ;
	fd = ::syscall( SYS_openat     ,       AT_FDCWD , "f"    , O_PATH                                ) ; ::close(fd) ;
	// unresolved paths
	/**/ ::syscall( SYS_openat     ,       AT_FDCWD , "nope" , O_RDONLY                              ) ;
	/**/ ::syscall( SYS_newfstatat ,       AT_FDCWD , "nope" , &st , 0                               ) ;
	/**/ ::syscall( SYS_unlinkat   ,       AT_FDCWD , "nope" , 0                                     ) ;
	/**/ ::syscall( SYS_renameat2  ,       AT_FDCWD , "nope" , AT_FDCWD , "nope2" , 0                ) ;
	// io_uring : executed by this thread, by a worker, and failing, then all other tracked operations
	uint8_t missing = 0 ;
	if (_uring_open("f",0)<=-1000) {
		missing |= TF_IoUring ;
	} else {
		struct statx    stx ;
		struct open_how how = {} ; how.flags = O_RDONLY ;
		/**/ _uring_open( "f"    , IOSQE_ASYNC ) ;
		/**/ _uring_open( "nope" , 0           ) ;
		fd = _uring_op( IORING_OP_OPENAT2  , AT_FDCWD , "f"   , uintptr_t(&how  ) , sizeof(how) , 0 ) ; if (fd>=0) ::close(fd) ;
		/**/ _uring_op( IORING_OP_STATX    , AT_FDCWD , "f"   , uintptr_t(&stx  ) , STATX_MTIME , 0 ) ;
		/**/ _uring_op( IORING_OP_MKDIRAT  , AT_FDCWD , "ud"  , 0                 , 0755        , 0 ) ;
		/**/ _uring_op( IORING_OP_SYMLINKAT, AT_FDCWD , "f"   , uintptr_t("ul"  ) , 0           , 0 ) ;
		/**/ _uring_op( IORING_OP_LINKAT   , AT_FDCWD , "f"   , uintptr_t("uh"  ) , AT_FDCWD    , 0 ) ;
		/**/ _uring_op( IORING_OP_RENAMEAT , AT_FDCWD , "uh"  , uintptr_t("uh2" ) , AT_FDCWD    , 0 ) ;
		/**/ _uring_op( IORING_OP_UNLINKAT , AT_FDCWD , "uh2" , 0                 , 0           , 0 ) ;
	}
	// execve in a sub-process (must be the last syscall of the sub-process)
	fd = ::syscall( SYS_openat , AT_FDCWD , "x" , O_CREAT|O_WRONLY|O_TRUNC , 0755 ) ;
	{ static constexpr char Script[] = "#!/bin/true\n" ; ssize_t n = ::write(fd,Script,sizeof(Script)-1) ; (void)n ; }
	::close(fd) ;
	auto sub_ok = [](pid_t p) {
		int ws = 0 ;
		return p>0 && ::waitpid(p,&ws,0)==p && WIFEXITED(ws) && WEXITSTATUS(ws)==0 ;
	} ;
	{	pid_t p = ::fork() ;
		if (p==0) {
			char* argv[] = { const_cast<char*>("x") , nullptr } ;
			::execve("./x",argv,nullptr) ;
			::_exit(3)                   ;
		}
		if (!sub_ok(p)) missing |= TF_Exec ;
	}
	::_exit(TF_Mark|missing) ;
}
#define K(k) (1u<<EBPF_##k)
static const TestExpect _g_test_expects[] = {
//	  name                    nr                    io      ok      kinds                         hooks                                              needs
	{ "open(O_CREAT)"       , SYS_openat          , false , true  , K(CREATE)|K(OPEN)           , "security_path_mknod, security_file_open"                     }
,	{ "open(O_RDONLY)"      , SYS_openat          , false , true  , K(OPEN)                     , "security_file_open"                                          }
,	{ "stat"                , SYS_newfstatat      , false , true  , K(LOOKUP)                   , "filename_lookup"                                             }
,	{ "access"              , SYS_faccessat2      , false , true  , K(LOOKUP)                   , "filename_lookup"                                             }
,	{ "symlink"             , SYS_symlinkat       , false , true  , K(CREATE)                   , "security_path_symlink"                                       }
,	{ "open through link"   , SYS_openat          , false , true  , K(LINK)|K(OPEN)             , "security_inode_follow_link, security_file_open"              }
,	{ "readlink"            , SYS_readlinkat      , false , true  , K(LOOKUP)                   , "filename_lookup"                                             }
,	{ "mkdir"               , SYS_mkdirat         , false , true  , K(CREATE)                   , "security_path_mkdir"                                         }
,	{ "link"                , SYS_linkat          , false , true  , K(LOOKUP)|K(CREATE)         , "filename_lookup, security_path_link"                         }
,	{ "rename"              , SYS_renameat2       , false , true  , K(RENAME_SRC)|K(RENAME_DST) , "security_path_rename"                                        }
,	{ "chmod"               , SYS_fchmodat        , false , true  , K(LOOKUP)                   , "filename_lookup"                                             }
,	{ "truncate"            , SYS_truncate        , false , true  , K(LOOKUP)                   , "filename_lookup"                                             }
,	{ "unlink"              , SYS_unlinkat        , false , true  , K(UNLINK)                   , "security_path_unlink"                                        }
,	{ "rmdir"               , SYS_unlinkat        , false , true  , K(UNLINK)                   , "security_path_rmdir"                                         }
,	{ "mknod"               , SYS_mknodat         , false , true  , K(CREATE)                   , "security_path_mknod"                                         }
,	{ "chdir"               , SYS_chdir           , false , true  , K(LOOKUP)                   , "filename_lookup"                                             }
,	{ "open(O_DIRECTORY)"   , SYS_openat          , false , true  , K(OPEN)                     , "security_file_open"                                          }
,	{ "getdents"            , SYS_getdents64      , false , true  , K(DIR)                      , "sys_exit"                                                    }
,	{ "open(O_PATH)"        , SYS_openat          , false , true  , K(OPEN)                     , "sys_exit"                                                    }
,	{ "open, no file"       , SYS_openat          , false , false , K(RAW)                      , "sys_exit"                                                    }
,	{ "stat, no file"       , SYS_newfstatat      , false , false , K(RAW)                      , "sys_exit"                                                    }
,	{ "unlink, no file"     , SYS_unlinkat        , false , false , K(RAW)                      , "sys_exit"                                                    }
,	{ "rename, no file"     , SYS_renameat2       , false , false , K(RAW)                      , "sys_exit"                                                    }
,	{ "io_uring open"       , IORING_OP_OPENAT    , true  , true  , K(OPEN)                     , "io_openat2, security_file_open"                 , TF_IoUring }
,	{ "io_uring open async" , IORING_OP_OPENAT    , true  , true  , K(OPEN)                     , "task_newtask, io_openat2, security_file_open"   , TF_IoUring }
,	{ "io_uring, no file"   , IORING_OP_OPENAT    , true  , false , K(RAW)                      , "io_openat2, do_filp_open"                       , TF_IoUring }
,	{ "io_uring openat2"    , IORING_OP_OPENAT2   , true  , true  , K(OPEN)                     , "io_openat2, security_file_open"                 , TF_IoUring }
,	{ "io_uring statx"      , IORING_OP_STATX     , true  , true  , K(LOOKUP)                   , "io_statx, filename_lookup"                      , TF_IoUring }
,	{ "io_uring mkdir"      , IORING_OP_MKDIRAT   , true  , true  , K(CREATE)                   , "io_mkdirat, security_path_mkdir"                , TF_IoUring }
,	{ "io_uring symlink"    , IORING_OP_SYMLINKAT , true  , true  , K(CREATE)                   , "io_symlinkat, security_path_symlink"            , TF_IoUring }
,	{ "io_uring link"       , IORING_OP_LINKAT    , true  , true  , K(LOOKUP)|K(CREATE)         , "io_linkat, filename_lookup, security_path_link" , TF_IoUring }
,	{ "io_uring rename"     , IORING_OP_RENAMEAT  , true  , true  , K(RENAME_SRC)|K(RENAME_DST) , "io_renameat, security_path_rename"              , TF_IoUring }
,	{ "io_uring unlink"     , IORING_OP_UNLINKAT  , true  , true  , K(UNLINK)                   , "io_unlinkat, security_path_unlink"              , TF_IoUring }
,	{ "open(O_CREAT) x"     , SYS_openat          , false , true  , K(CREATE)|K(OPEN)           , "security_path_mknod, security_file_open"                     }
,	{ "execve"              , SYS_execve          , false , true  , K(OPEN)                     , "sched_process_fork, security_file_open"         , TF_Exec    }
} ;
#undef K

static bool/*ok*/ _self_test(bool verbose) {
	static constexpr uint32_t TestJobId = 1 ;
	char dir[] = "/tmp/lmake_bpfd_test.XXXXXX" ;
	if (!::mkdtemp(dir)) { _log("self-test : cannot create tmp dir : %s",strerror(errno)) ; return false ; }
	//
	Hello hello = {} ;
	{	struct stat st ;
		if (::stat("/proc/self/ns/pid",&st)==0) { hello.pidns_dev = st.st_dev ; hello.pidns_ino = st.st_ino ; }
	}
	hello.n_roots = 1 ;
	//
	bool ok  = false                                                                      ;
	Job  job { .job_id=TestJobId , .root_fds={::open(dir,O_PATH|O_DIRECTORY|O_CLOEXEC)} } ;
	if (!job.mk_job(hello)) { _log("self-test : cannot create job"       ) ; goto Clean ; }
	{	TestCtx             ctx     { .verbose=verbose }                                             ;
		struct ring_buffer* rb      = ::ring_buffer__new( job.rb_fd , _test_event , &ctx , nullptr ) ; if (!rb) { _log("self-test : cannot open ring buffer") ; goto Clean ; }
		pid_t               child   = ::fork()                                                       ;
		int                 wstatus = 0                                                              ;
		if (child==0) _test_child(dir,TestJobId)  ;
		else          ::waitpid(child,&wstatus,0) ;
		::ring_buffer__consume(rb) ;
		::ring_buffer__free   (rb) ;
		//
		JobCfg   cfg = {}        ;
		uint32_t jid = TestJobId ;
		::bpf_map_lookup_elem( bpf_map__fd(g_skel->maps.job_cfg) , &jid , &cfg ) ;
		//
		ok = WIFEXITED(wstatus) && (WEXITSTATUS(wstatus)&TF_Mark) ;
		uint8_t missing = ok ? WEXITSTATUS(wstatus)&~TF_Mark : 0 ;
		if (!ok                       )                _log("self-test : child failed (wstatus=0x%x)",wstatus                                               ) ;
		if (missing&TF_IoUring        )                _log("self-test : io_uring is not usable, its tracking is not checked"                               ) ;
		if (missing&TF_32             )                _log("self-test : 32-bit syscalls are not usable, their tracking is not checked"                     ) ;
		if (missing&TF_Exec           )                _log("self-test : cannot execute a file in %s, tracking of execve is not checked",dir                ) ;
		if (cfg.lost                  ) { ok = false ; _log("self-test : %llu events lost",(unsigned long long)cfg.lost                                     ) ; }
		if (cfg.seq!=ctx.events.size()) { ok = false ; _log("self-test : %llu events emitted but %zu received",(unsigned long long)cfg.seq,ctx.events.size()) ; }
		// events are processed in order : for each expected syscall, find its End and check captures seen since previous End
		size_t pos       = 0     ;
		bool   seen_fork = false ;
		bool   seen_exit = false ;
		for( TestEvent const& te : ctx.events ) {
			seen_fork |= te.kind==EBPF_PROC_START && te.nr!=0 ;
			seen_exit |= te.kind==EBPF_PROC_END               ;
		}
		if (!seen_fork) { ok = false ; _log("self-test : process creation not seen (hook sched_process_fork)") ; }
		if (!seen_exit) { ok = false ; _log("self-test : process exit not seen (hook sched_process_exit)"    ) ; }
		for( TestExpect const& exp : _g_test_expects ) {
			uint32_t kinds = 0     ;
			bool     found = false ;
			size_t   p     = pos   ;
			for(; p<ctx.events.size() ; p++ ) {
				TestEvent const& te = ctx.events[p] ;
				if ( te.kind!=EBPF_END                                                       ) { kinds |= 1u<<te.kind ; continue ; }
				if ( te.flags&EBPF_F_DISCARD                                                 ) { kinds  = 0           ; continue ; }
				if ( te.nr==exp.nr && bool(te.flags&EBPF_F_IO)==exp.io && (te.rc>=0)==exp.ok ) { found = true         ; break    ; }
				kinds = 0 ;
			}
			if (found) pos = p+1 ;                                                                                // else, go on with following checks from same position
			bool good = found && (kinds&exp.kinds)==exp.kinds ;
			bool skip = exp.needs&missing                     ;                                                   // what is usable must be tracked
			if (verbose   ) _log( "self-test : %-20s : %s" , exp.name , skip?"not checked":good?"ok":"FAILED" ) ;
			if (good||skip) continue ;
			ok = false ;
			if (!found) _log( "self-test : %s : no event received (hooks : %s)"                                , exp.name ,                     exp.hooks ) ;
			else        _log( "self-test : %s : missing captures (expected 0x%x, got 0x%x, hooks : %s)"        , exp.name , exp.kinds , kinds , exp.hooks ) ;
		}
		// check content on a few events
		auto chk = [&]( uint8_t kind , const char* str0 , const char* str1 , const char* what ) {
			for( TestEvent const& te : ctx.events ) if ( te.kind==kind && te.str0==str0 && (!str1||te.str1==str1) ) return ;
			ok = false ;
			_log("self-test : %s",what) ;
		} ;
		chk( EBPF_OPEN   , "f"   , nullptr , "bad path for opened file"                                       ) ;
		chk( EBPF_LINK   , "l"   , nullptr , "bad path for traversed link"                                    ) ;
		chk( EBPF_CREATE , "d/h" , nullptr , "bad path for created file"                                      ) ;
		chk( EBPF_RAW    , ""    , "nope"  , "bad path for unresolved file (it must be relative to the root)" ) ;
		// the target of a link is only provided by the kernel if the file system stores it in the inode, else it is read by the client
		{	bool has_target = false ;
			for( TestEvent const& te : ctx.events ) if ( te.kind==EBPF_LINK && te.str0=="l" ) {
				if      (!(te.flags&EBPF_F_TARGET)) continue ;
				else if (te.str1=="f"             ) has_target = true ;
				else                                { ok = false ; _log("self-test : bad target for traversed link") ; }
			}
			if (!has_target) _log("self-test : targets of symbolic links are not provided by the file system of %s, clients will read them",dir) ;
		}
	}
Clean :
	// tmp dir only contains what the child has created
	::string dir_s = dir ; dir_s += '/' ;
	for( const char* f : {"f","l","p","x","d/h","d/h2","ul","uh","uh2"} ) ::unlink((dir_s+f).c_str()) ;
	for( const char* d : {"d","ud"}                                     ) ::rmdir ((dir_s+d).c_str()) ;
	/**/                                                                  ::rmdir (dir              ) ;
	return ok ;
}

//
// clients
//

// serve a client, i.e. a job, until it disconnects
static void _serve(int conn) {
	{	Hello         hello    ;
		::vector<int> root_fds = _recv_hello(conn,/*out*/hello)                             ; if (root_fds.empty()) goto Return ;
		uint32_t      job_id   = g_next_job_id++                                            ;
		Reply         reply    { .err=false , .job_id=job_id                              } ;
		Job           job      { .conn=conn , .job_id=job_id , .root_fds=::move(root_fds) } ;    // from now on, job owns conn and root_fds
		//
		if (!job.mk_job(hello))   reply.err = true ;
		//
		if ( _send_reply(conn,reply,reply.err?-1:job.rb_fd) && !reply.err ) {
			uint32_t n_excludes = 0 ;
			for (;;) {
				ReqKey        req                           ;
				int           req_fd                        = -1                              ;
				struct iovec  iov                           = { .iov_base=&req , .iov_len=1 } ;
				char          cbuf[CMSG_SPACE(sizeof(int))] = {}                              ;
				struct msghdr msg                           = {}                              ; msg.msg_iov=&iov ; msg.msg_iovlen=1 ; msg.msg_control=cbuf ; msg.msg_controllen=sizeof(cbuf) ;
				ssize_t       n                             = ::recvmsg( conn , &msg , MSG_CMSG_CLOEXEC ) ;
				if ( n<0 && errno==EINTR ) continue ;
				if ( n<=0                ) break    ;                                            // client is gone
				for( struct cmsghdr* cm=CMSG_FIRSTHDR(&msg) ; cm ; cm=CMSG_NXTHDR(&msg,cm) ) {
					if (!( cm->cmsg_level==SOL_SOCKET && cm->cmsg_type==SCM_RIGHTS )) continue ;
					for( size_t ofs=0 ; ofs<(cm->cmsg_len-CMSG_LEN(0)) ; ofs+=sizeof(int) ) {    // close all received fds but the first one
						int fd ; ::memcpy( &fd , CMSG_DATA(cm)+ofs , sizeof(int) ) ;
						if (req_fd<0) req_fd = fd ;
						else          ::close(fd) ;
					}
				}
				bool ok ;
				switch (req) {
					case ReqKey::Stats : {
						JobCfg cfg   = {}                              ; ::bpf_map_lookup_elem( bpf_map__fd(g_skel->maps.job_cfg) , &job.job_id , &cfg ) ;
						Stats  stats { .seq=cfg.seq , .lost=cfg.lost } ;
						ok = ::send(conn,&stats,sizeof(stats),MSG_NOSIGNAL)==ssize_t(sizeof(stats)) ;
					} break ;
					case ReqKey::Exclude : {
						char done = req_fd>=0 && n_excludes<MaxExcludes && job.register_root( EBPF_ROOT_EXCLUDE , req_fd ) ;
						if (done) {
							job.root_fds.push_back(req_fd) ;
							req_fd = -1 ;
							n_excludes++ ;
						}                                                                        // keep it open so that dentry stays valid
						ok = ::send(conn,&done,1/*len*/,MSG_NOSIGNAL)==1 ;
					} break ;
					default : ok = false ;                                                       // unknown request, client cannot be trusted
				}
				if (req_fd>=0) ::close(req_fd) ;
				if (!ok      ) break ;
			}
		}
	}
Return :
	::close(conn) ;
}

//
// main
//

int main( int argc , char* argv[] ) {
	bool test_only     = false                   ;
	bool no_test       = false                   ;
	bool rc            = 0                       ;
	for( int i=1 ; i<argc ; i++ ) {
		if      (::strcmp(argv[i],"--test"   )==0) test_only = true ;
		else if (::strcmp(argv[i],"--no-test")==0) no_test   = true ;
		else                                       { fprintf(stderr,"usage : %s [--test|--no-test]\n",argv[0]) ; return 2 ; }
	}
	::libbpf_set_print(_pr) ;
	::signal( SIGINT  , _on_signal ) ;
	::signal( SIGTERM , _on_signal ) ;
	::signal( SIGPIPE , SIG_IGN    ) ;
	//
	// each running job needs a few fds (connection, ring buffer, dirs of interest), so the default limit would be reached with a few hundreds of jobs
	{	struct rlimit rl     = {}     ;
		rlim_t        wanted = 1u<<20 ;
		if ( FILE* f=::fopen("/proc/sys/fs/nr_open","r") ; f ) {
			unsigned long n ;
			if (::fscanf(f,"%lu",&n)==1) wanted = ::min<rlim_t>(wanted,n) ;
			::fclose(f) ;
		}
		if ( ::getrlimit(RLIMIT_NOFILE,&rl)==0 && rl.rlim_cur<wanted ) {
			rl.rlim_cur = wanted                                 ;
			rl.rlim_max = ::max<rlim_t>(rl.rlim_max,wanted)      ;
			if (::setrlimit(RLIMIT_NOFILE,&rl)!=0) _log("cannot raise the number of open files : %s",strerror(errno)) ;
		}
	}
	//
	// load & attach the shared programs
	g_skel = ebpf__open() ;
	if (!g_skel                ) { _log("cannot open ebpf programs"                                                              ) ; goto Err ; }
	if (ebpf__load  (g_skel)!=0) { _log("cannot load ebpf programs (need CAP_BPF+CAP_PERFMON, run as root and with a BTF kernel)") ; goto Err ; }
	if (ebpf__attach(g_skel)!=0) { _log("cannot attach ebpf programs (need a recent BTF kernel)"                                 ) ; goto Err ; }
	if (!_set_syscalls()       ) { _log("cannot upload syscall table"                                                            ) ; goto Err ; }
	//
	if (!no_test) {
		if (!_self_test(test_only/*verbose*/)) {
			_log("self-test failed, accesses cannot be reliably tracked on this system") ;
			goto Err ;
		}
		_log("self-test ok") ;
	}
	if (!test_only) {
		try {
			::filesystem::create_directories(SockDir) ;
			::string           sock_name = get_sock_name()                              ;
			struct sockaddr_un addr      = {}                                           ; addr.sun_family = AF_UNIX ; ::strncpy( addr.sun_path , sock_name.c_str() , sizeof(addr.sun_path)-1 ) ;
			int                fd        = ::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0) ; if (fd<0) { _log("cannot create socket : %s",strerror(errno)) ; goto Err ; }
			//
			/**/ ::unlink(sock_name.c_str()) ;
			if ( ::bind(fd,(struct sockaddr*)&addr,sizeof(addr))!=0 ) { _log("cannot bind %s : %s"     ,sock_name.c_str(),strerror(errno)) ; goto Err ; }
			/**/ ::chmod(sock_name.c_str(),0666) ;                                                                                                        // allow unprivileged clients to connect
			if ( ::listen(fd,64)                                !=0 ) { _log("cannot listen on %s : %s",sock_name.c_str(),strerror(errno)) ; goto Err ; }
			_log("ready, listening on %s",sock_name.c_str()) ;
			//
			while (!g_stop) {
				struct pollfd pfd = { .fd=fd , .events=POLLIN , .revents=0 }  ;
				int           r   = ::poll( &pfd , 1/*nfds*/ , -1/*timeout*/ ) ;
				if (r<0) {
					if (errno==EINTR) continue ;
					else              break    ;
				}
				if (!(pfd.revents&POLLIN)) continue ;
				int conn = ::accept4( fd , nullptr/*addr*/ , nullptr/*addrlen*/ , SOCK_CLOEXEC ) ;
				if (conn<0) continue ;
				try                             { ::thread(_serve,conn).detach() ;                            }
				catch (::system_error const& e) { _log("cannot serve client : %s",e.what()) ; ::close(conn) ; }
			}
			_log("shutting down")       ;                                                                                                                 // maps are released when process exits
			::unlink(sock_name.c_str()) ;
		}
		catch (::string    const& e) { _log( "%s" , e.c_str() ) ; goto Err ; }
		catch (::exception const& e) { _log( "%s" , e.what () ) ; goto Err ; }
	}
	goto Return ;
Err :
	rc = 1 ;
Return :
	if (g_skel) ::ebpf__destroy(g_skel) ;
	return rc ;
}
