// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <linux/io_uring.h>
#include <linux/openat2.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include <string>
#include <vector>

#include "ebpf_utils.hh"
#include "ebpf_self_test.hh"

using namespace std ;

using namespace EbpfDaemon ;

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
		case EBPF_JOB_END      : return "job_end"      ;
		case EBPF_PROC_START   : return "proc_start"   ;
		case EBPF_THREAD_START : return "thread_start" ;
		case EBPF_PROC_END     : return "proc_end"     ;
		case EBPF_THREAD_END   : return "thread_end"   ;
		case EBPF_LINK         : return "link"         ;
		case EBPF_LOOKUP       : return "lookup"       ;
		case EBPF_OPEN         : return "open"         ;
		case EBPF_CREATE       : return "create"       ;
		case EBPF_UNLINK       : return "unlink"       ;
		case EBPF_RENAME_SRC   : return "rename_src"   ;
		case EBPF_RENAME_DST   : return "rename_dst"   ;
		case EBPF_DIR          : return "dir"          ;
		case EBPF_RAW          : return "raw"          ;
		case EBPF_END          : return "end"          ;
		default                : return "???"          ;
	}
}

struct TestEvent {
	uint8_t  kind  = 0  ;
	uint8_t  flags = 0  ;
	uint8_t  is32  = 0  ;
	uint8_t  root  = 0  ;
	int32_t  nr    = 0  ;
	int64_t  rc    = 0  ;
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
	TestEvent te { .kind=ev.kind , .flags=ev.flags , .is32=ev.is32 , .root=ev.root , .nr=ev.nr , .rc=ev.rc , .tid=ev.tid } ;
	uint32_t  l0 = ::min<uint32_t>( ev.len[0] , sz-EBPF_HDR_SZ    )                                                        ;
	uint32_t  l1 = ::min<uint32_t>( ev.len[1] , sz-EBPF_HDR_SZ-l0 )                                                        ;
	if (ev.kind==EBPF_RAW) { te.str0 = _mk_path(ev.data,l0)                                                 ; if (ev.root==EBPF_ROOT_ABS) te.str0 = '/'+te.str0 ; }
	else                     te.str0 = _mk_path(ev.data,l0)                                                 ;
	if (l1               )   te.str1 = ::string( reinterpret_cast<const char*>(ev.data+l0) , l1-1/*null*/ ) ;
	if (ctx.verbose) {
		fprintf( stderr , "  tid=%-7u %-10s" , te.tid , _kind_str(te.kind) ) ;
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
				if (ev.flags&EBPF_F_TARGET ) fprintf( stderr , " target=%s" , te.str1.c_str()                                                                                          ) ;
				if (ev.flags&EBPF_F_EXISTS ) fprintf( stderr , " mode=0%o size=%lu mtime=%lu.%09lu" , ev.mode , (unsigned long)ev.size , ev.mtime_ns/1000000000,ev.mtime_ns%1000000000 ) ;
				else                         fprintf( stderr , " absent"                                                                                                               ) ;
				if (ev.flags&EBPF_F_CREATED) fprintf( stderr , " created"                                                                                                              ) ;
				if (ev.kind==EBPF_OPEN     ) fprintf( stderr , " open_flags=0%o" , ev.open_flags                                                                                       ) ;
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

bool/*ok*/ self_test(bool verbose) {
	static constexpr uint32_t TestJobId = 1 ;
	char dir[] = "/tmp/lmake_bpfd_test.XXXXXX" ;
	if (!::mkdtemp(dir)) { log_errno("self-test : cannot create tmp dir") ; return false ; }
	//
	Hello hello = {} ;
	{	struct stat st ;
		if (::stat("/proc/self/ns/pid",&st)==0) { hello.pidns_dev = st.st_dev ; hello.pidns_ino = st.st_ino ; }
	}
	hello.n_roots = 1 ;
	//
	bool ok  = false                                                                      ;
	Job  job { .job_id=TestJobId , .root_fds={::open(dir,O_PATH|O_DIRECTORY|O_CLOEXEC)} } ;
	if (!job.mk_job(hello)) { log("self-test : cannot create job") ; goto Clean ; }
	{	TestCtx             ctx     { .verbose=verbose }                                             ;
		struct ring_buffer* rb      = ::ring_buffer__new( job.rb_fd , _test_event , &ctx , nullptr ) ; if (!rb) { log("self-test : cannot open ring buffer") ; goto Clean ; }
		pid_t               child   = ::fork()                                                       ;
		int                 wstatus = 0                                                              ;
		if (child==0) _test_child(dir,TestJobId)  ;
		else          ::waitpid(child,&wstatus,0) ;
		::ring_buffer__consume(rb) ;
		::ring_buffer__free   (rb) ;
		//
		JobCfg   cfg = {}        ;
		uint32_t jid = TestJobId ;
		::bpf_map_lookup_elem( bpf_map__fd(g_skel->maps.job_cfgs) , &jid , &cfg ) ;
		//
		ok = WIFEXITED(wstatus) && (WEXITSTATUS(wstatus)&TF_Mark) ;
		uint8_t missing = ok ? WEXITSTATUS(wstatus)&~TF_Mark : 0 ;
		if (!ok               )                log("self-test : child failed (wstatus=0x%x)",wstatus                               ) ;
		if (missing&TF_IoUring)                log("self-test : io_uring is not usable, its tracking is not checked"               ) ;
		if (missing&TF_32     )                log("self-test : 32-bit syscalls are not usable, their tracking is not checked"     ) ;
		if (missing&TF_Exec   )                log("self-test : cannot execute a file in %s, tracking of execve is not checked",dir) ;
		if (cfg.overflow      ) { ok = false ; log("self-test : some events have been lost"                                        ) ; }
		// events are processed in order : for each expected syscall, find its End and check captures seen since previous End
		size_t pos          = 0     ;
		bool   seen_fork    = false ;
		bool   seen_exit    = false ;
		bool   seen_job_end = false ;
		for( TestEvent const& te : ctx.events ) {
			seen_fork    |= te.kind==EBPF_PROC_START ;
			seen_exit    |= te.kind==EBPF_PROC_END   ;
			seen_job_end |= te.kind==EBPF_JOB_END    ;
		}
		if (!seen_fork   ) { ok = false ; log("self-test : process creation not seen (hook sched_process_fork)") ; }
		if (!seen_exit   ) { ok = false ; log("self-test : process exit not seen (hook sched_process_exit)"    ) ; }
		if (!seen_job_end) { ok = false ; log("self-test : job end not seen (hook sched_process_exit)"         ) ; }
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
			if (verbose   ) log( "self-test : %-20s : %s" , exp.name , skip?"not checked":good?"ok":"FAILED" ) ;
			if (good||skip) continue ;
			ok = false ;
			if (!found) log("self-test : %s : no event received (hooks : %s)"                        ,exp.name,                exp.hooks) ;
			else        log("self-test : %s : missing captures (expected 0x%x, got 0x%x, hooks : %s)",exp.name,exp.kinds,kinds,exp.hooks) ;
		}
		// check content on a few events
		auto chk = [&]( uint8_t kind , const char* str0 , const char* str1 , const char* what ) {
			for( TestEvent const& te : ctx.events ) if ( te.kind==kind && te.str0==str0 && (!str1||te.str1==str1) ) return ;
			ok = false ;
			log("self-test : %s",what) ;
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
				else                                { ok = false ; log("self-test : bad target for traversed link") ; }
			}
			if (!has_target) log("self-test : targets of symbolic links are not provided by the file system of %s, clients will read them",dir) ;
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
