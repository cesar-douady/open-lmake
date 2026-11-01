// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// BPF program for the ebpf autodep method.
// It observes file accesses of the traced process sub-trees, including those done through io_uring (which bypass syscalls).
//
// Everything is captured in the kernel when the access occurs, so that the consumer (ebpf.cc) never has to look at the disk to understand what happened :
// - hooks called during the syscall capture what the kernel has resolved : the path, the file info before any modification and the traversed symlinks
// - the syscall is closed at exit by an End event which carries what is necessary to interpret the captures
// - if a path could not be resolved (the syscall failed), it is reported as provided by the user together with the dir it is relative to
// Files that can be neither deps nor targets (i.e. not lying in the repo nor in a source dir) are filtered out here, they are 90% of the accesses.
//
// Hooks are chosen to be as stable as possible :
// - tracepoints (sys_exit, sched_process_fork, sched_process_exit, task_newtask)
// - LSM entry points (security_*), they are global functions called from all the paths we are interested in and they are passed already resolved objects
// - filename_lookup, a global function through which all the syscalls that just resolve a path go
// - io_uring handlers (io_openat2, ...), they are called through a table, hence they cannot be inlined
// As none of the functions has a guaranteed interface, the daemon checks at start-up that each of them actually fires (cf. ebpf_daemon.cc).
//
// No lock is necessary :
// - the state of a syscall in progress is held in a task storage, only accessed by the task itself, so there is no concurrency (and this is robust to preemption)
// - events are built in this storage and copied to the ring buffer, which is multi-producer
// - job wide counters are atomically updated

#include "vmlinux.h" // must precede standard includes

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "sys_config.h"

typedef __u8          uint8_t  ;
typedef __u16         uint16_t ;
typedef __u32         uint32_t ;
typedef __u64         uint64_t ;
typedef unsigned int  uint     ;
typedef unsigned long ulong    ;

#include "ebpf_event.h"

#define N_JOBS 4096
#define N_ROOTS 256         // number of allowed roots for each job
typedef EbpfJobId  JobId  ;
typedef EbpfRootId RootId ;

char LICENSE[] SEC("license") = "GPL" ; // required to use GPL-only helpers

#define IO_WORKER    0x10    // task is an io_uring worker
#define ERESTART_MIN 512     // from 512 to 516, the syscall will be restarted
#define ERESTART_MAX 516
#define RING_BUF_SZ  (1<<22) // must be a power of 2 at least 4096

#if HAS_32
	#define SYSCALL_32 0x0002 // 32-bit syscall
#endif

#define N_SYSCALLS 512 // all handled syscalls must be less than this

#define HAS_STAMPS bpf_core_enum_value_exists(enum bpf_func_id,BPF_FUNC_ktime_get_tai_ns)

// intitialized at startup by daemon
const volatile uint64_t    Mrkr                             = 0       ; // a marker to ensure jobs connect to the right filter
const volatile EbpfScEntry Syscalls[(1+HAS_32)][N_SYSCALLS] = {{0}}   ; // 0 if syscall is not tracked
const volatile uint32_t    NJobs                            = N_JOBS  ;
const volatile uint32_t    NRoots                           = N_ROOTS ;

//
// maps
//

struct RingBuf {                                          // template describing the per-job ring buffers stored in ring_bufs
	__uint( type        , BPF_MAP_TYPE_RINGBUF ) ;
	__uint( max_entries , RING_BUF_SZ          ) ;        // actual size is decided by the daemon
} ring_buf SEC(".maps") ;
struct {                                                  // job_id -> that job's ring buffer, so a consumer never sees another job's (possibly another user's) accesses
	__uint ( type        , BPF_MAP_TYPE_ARRAY_OF_MAPS ) ;
	__uint ( max_entries , N_JOBS                     ) ;
	__type ( key         , uint32_t                   ) ;
	__array( values      , struct RingBuf             ) ;
} ring_bufs SEC(".maps") ;

typedef struct {
	bool overflow       ;                        // true if an event could not be reported
	bool deps_in_system ;
} JobInfo ;
struct {                                         // job_id -> that job's ring buffer, so a consumer never sees another job's (possibly another user's) accesses
	__uint( type        , BPF_MAP_TYPE_ARRAY ) ;
	__uint( max_entries , N_JOBS             ) ;
	__type( key         , uint32_t           ) ;
	__type( value       , JobInfo            ) ;
} job_infos SEC(".maps") ;

typedef struct {
	JobId     job_id      ;
	bool      is_tracer   ;                           // true for tracer that launches job
	bool      is_job      ;                           // true for root thread of job
	uint16_t  disable_cnt ;                           // autodep is active if 0
	uint16_t  file_len    ;                           // len of file name in ev
	EbpfEvent ev          ;                           // event being built, off-stack as it is large
} Task ;
struct {                                              // state of the traced threads
	__uint( type      , BPF_MAP_TYPE_TASK_STORAGE ) ;
	__uint( map_flags , BPF_F_NO_PREALLOC         ) ;
	__type( key       , int                       ) ;
	__type( value     , Task                      ) ;
} tasks SEC(".maps") ;

typedef struct {
	struct dentry* dentry                           ;
	JobId          job_id                           ;
	char           pad[sizeof(void*)-sizeof(JobId)] ; // ensure no unitialized bits in key as hash is computed after raw bits
} RootKey ;
struct {                                              // state of the traced threads
	__uint( type        , BPF_MAP_TYPE_HASH ) ;
	__uint( max_entries , N_JOBS*N_ROOTS    ) ;
	__uint( map_flags   , BPF_F_NO_PREALLOC ) ;
	__type( key         , RootKey           ) ;
	__type( value       , RootId            ) ;       // if 0, do not report
} roots SEC(".maps") ;

//
// utils
//

static __always_inline bool _is_ok(long ret) {
	return (unsigned long)ret<=(unsigned long)-4096 ; // negative values -4095 to -1 are error codes
}

static __always_inline bool _is_retry(long ret) {
	if ( ret==-EAGAIN                             ) return true  ;
	if ( ret==-EINTR                              ) return true  ;
	if ( -ERESTART_MAX<=ret && ret<=-ERESTART_MIN ) return true  ;
	/**/                                            return false ;
}

static __always_inline bool _is_dir(uint32_t mode) {
	return (mode&EBPF_S_IFMT)!=EBPF_S_IFDIR ;
}

static __always_inline bool _statx_is_stat(uint mask) {
	return (mask&(EBPF_STATX_TYPE|EBPF_STATX_MODE|EBPF_STATX_SIZE))!=0 ; // these access parts of inode that are part of file crc : link/reg, exe bit, size
}

static __always_inline struct dentry* dentry_from_fd( struct task_struct* t , int fd ) {
	if (fd==AT_FDCWD) return BPF_CORE_READ( t , fs,pwd.dentry ) ;
	if (fd<0        ) return 0                                  ;
	//
	struct fdtable* fdt = BPF_CORE_READ( t   , files,fdt ) ;                                                    if (!fdt         ) return 0 ; // to please verifier
	uint            max = BPF_CORE_READ( fdt , max_fds   ) ;                                                    if ((uint)fd>=max) return 0 ; // fd is closed
	struct file*  * fda = BPF_CORE_READ( fdt , fd        ) ;                                                    if (!fda         ) return 0 ; // to please verifier
	struct file   * f   = 0                                ; bpf_probe_read_kernel( &f , sizeof(f) , fda+fd ) ; if (!f           ) return 0 ; // fd is closed
	//
	return BPF_CORE_READ( f , f_path.dentry ) ;
}

//
// emission
//

static __always_inline void _reset_ev(Task* tv) {
	EbpfEvent* ev  = &tv->ev          ;
	uint32_t   tid = ev->base.hdr.tid ;     // empty event, but keep tid
	ev->base         = (EbpfEventBase){0} ;
	ev->base.hdr.tid = tid                ;
	tv->file_len     = 0                  ;
}

// push tv->ev to the ring buffer of its job, if the job is gone, the event is dropped
static __always_inline bool/*ok*/ _emit(Task* tv) { //!                                            ok
	/**/                                                        if ( tv->is_tracer        ) return false ; // defensive
	uint32_t   jid = tv->job_id                               ; if ( !jid || jid>=N_JOBS  ) return false ; // cast to 32-bit so we can use it in bpf_map_lookup_elem
	JobInfo  * ji  = bpf_map_lookup_elem( &job_infos , &jid ) ; if ( !ji  || ji->overflow ) return false ;
	void     * rb  = bpf_map_lookup_elem( &ring_bufs , &jid ) ; if ( !rb                  ) return false ;
	EbpfEvent* ev  = &tv->ev                                  ;
	uint32_t   sz  ;
	//
	if (ev->base.hdr.kind==EBPF_NONE    ) goto Done ;
	if (ev->base.hdr.kind< EBPF_HAS_DATA) {
		sz = sizeof(EbpfEventHdr) ;
	} else {
		if (ev->base.hdr.file_kind<EBPF_FILE_EMIT) goto Done ;
		sz = sizeof(EbpfEventBase) + tv->file_len ;
		if (sz>=sizeof(EbpfEvent)) sz = sizeof(EbpfEvent) ;                                                // please verifier
		if (HAS_STAMPS) ev->base.stamp_ns = bpf_ktime_get_tai_ns() ;
	}
	if (bpf_ringbuf_output(rb,ev,sz,0/*flags*/)!=0) { ji->overflow = true ; return false/*ok*/ ; }         // dont emit any further, in particular dont emit job end
Done :
	_reset_ev(tv) ;
	return true/*ok*/ ;
}

static __always_inline void _panic( Task* tv , const char* reason ) {
	EbpfEvent* ev = &tv->ev ;
	int        n  ;
	ev->base.hdr.kind      = EBPF_PANIC     ;
	ev->base.hdr.file_kind = EBPF_FILE_USER ;
	for( n=0 ; n<256 && reason[n] ; n++ ) ev->data[n] = reason[n] ; // 256 must be low enough for the verifier, but large enough to hold any error message
	ev->data[n++] = 0 ;                                             // last char is root_id, none in our case
	tv->file_len = n ;
	_emit(tv) ;
}

//
// path resolution
//

//
// in-kernel "simple path" filter for paths that could not be resolved : drop accesses to system files before they reach the ring buffer
// /!\ must stay conservative : only drop what user-space Record::s_is_simple would also drop (else a real dep would be lost), when unsure, keep the event
//

typedef struct {
	const char* d         ;
	uint32_t    i         ;
	uint32_t    depth     ;
	uint8_t     dots_seen ;                                                                                  // d is empty or starts with /
	uint8_t     is_simple ;
} IsSimpleCtx ;
static long/*stop*/ _is_simple_dot_dot_step( uint64_t idx , void* ctx ) {
	IsSimpleCtx* c = ctx  ;
	uint32_t     i = c->i ; c->i++ ;
	char         v = 0    ; if (i<256) v = c->d[i] ;                                                         // i<256 is always holds, but this is necessary for the verifier
	switch (v) {
		case 0   :
		case '/' :
			switch (c->dots_seen) {
				case 0  :                                                                                    // empty : no depth updatew
				case 1  :                                                                            break ; // .     : no depth update
				case 2  : c->depth-- ; if (!c->depth) { c->is_simple=false ; return true/*stop*/ ; } break ; // ..    : if escaped system dir, not simple
				default : c->depth++ ;
			}
			if (!v) { c->is_simple=true ; return true/*stop*/ ; }                                            // not engough .. to escape system dir : simple
			c->dots_seen = 0 ;
		break ;
		case '.' :
			if (c->dots_seen<=2) c->dots_seen++ ;
		break ;
		default :
			c->dots_seen = 3 ;                                                                               // normal dir (i.e. neither . nor ..)
	}
	return false/*stop*/ ;
}
static __always_inline uint32_t _is_simple_match( const char* d , const char* p ) {
	for( uint32_t i=0 ; i<10 ; i++ ) {                                                                       // 10 : longer than prefix length
		char d_ = d[i] ;
		char p_ = p[i] ;
		if      (!p_   ) return d_==0 || d_=='/' ? i : 0 ;
		else if (d_!=p_) return 0                        ;
	}
	return 0 ;                                                                                               // defensive programming
}
static __always_inline uint32_t _is_simple_pfx(const char* d) {
	uint32_t pfx_len = 0 ;
	switch (*d) {
		case 'b' : pfx_len=_is_simple_match(d,"bin"   ) ;              break ;
		case 'e' : pfx_len=_is_simple_match(d,"etc"   ) ;              break ;
		case 'l' : pfx_len=_is_simple_match(d,"lib"   ) ; if (pfx_len) break ;
		/**/       pfx_len=_is_simple_match(d,"lib32" ) ; if (pfx_len) break ;
		/**/       pfx_len=_is_simple_match(d,"lib64" ) ; if (pfx_len) break ;
		/**/       pfx_len=_is_simple_match(d,"libx32") ;              break ;
		case 'o' : pfx_len=_is_simple_match(d,"opt"   ) ;              break ;
		case 'r' : pfx_len=_is_simple_match(d,"run"   ) ;              break ;
		case 's' : pfx_len=_is_simple_match(d,"sbin"  ) ;              break ;
		case 'u' : pfx_len=_is_simple_match(d,"usr"   ) ;              break ;
		case 'v' : pfx_len=_is_simple_match(d,"var"   ) ;              break ;
		default  : ;
	}
	return pfx_len ;
}
static __always_inline bool _is_simple( const char* d , uint32_t len , int deps_in_system ) {
	if (deps_in_system) return false ;                                                                       // if deps may lie in system dirs  : nothing is simple
	if (len<4         ) return false ;                                                                       // 4 is the min prefix len
	if (len>256       ) return false ;                                                                       // max system file length is about 173, so 256 is comfortable, even with a few ..
	if (*d!='/'       ) return false ;                                                                       // relative : not simple
	d += 1/* / */ ;
	uint32_t pfx_len = _is_simple_pfx(d) ; if (!pfx_len) return false ;
	d += pfx_len ;
	// check if enough .. to escape system dir
	IsSimpleCtx ctx = {
		.d         = d
	,	.i         = 0
	,	.depth     = 0
	,	.dots_seen = 3                                                                                       // evaluate in the context of a normal dir
	,	.is_simple = false                                                                                   // defensive programming
	} ;
	bpf_loop( 256 , _is_simple_dot_dot_step , &ctx , 0/*flags*/ ) ;
	return ctx.is_simple ;
}

#define WALK_MAX_DEPTH 256 // the kernel puts no limit on depth, paths deeper than that make the job untrustable
typedef struct {
	RootKey    root_key ;
	EbpfEvent* ev       ;
	uint32_t   off      ;
	bool       panic    ;
	bool       report   ;
} WalkCtx ;
// one step up, return 1 when done
// /!\ the loop is done with bpf_loop so that the verifier checks the body only once (an unrolled loop exceeds its budget)
static long/*stop*/ _walk_step( uint64_t idx , void* ctx_ ) { //!                                stop
	WalkCtx      * ctx     = ctx_                                           ; if (!ctx   ) return 1 ;                                                          // to please verifier
	EbpfEvent    * ev      = ctx->ev                                        ; if (!ev    ) return 1 ;                                                          // to please verifier
	struct dentry* dentry  = ctx->root_key.dentry                           ; if (!dentry) return 1 ;                                                          // to please verifier
	RootId       * root_id = bpf_map_lookup_elem( &roots , &ctx->root_key ) ;
	//
	if (root_id) {
		uint32_t off = ctx->off ; if (off>PATH_MAX) { ctx->panic = true ; off = PATH_MAX ; }                                                                   // to please verifier
		if (*root_id) {
			ev->data[off] = *root_id ; ctx->off++ ;                                                                                                            // last char identify the root
			ctx->report   = true     ;
		}
		return 1/*stop*/ ;
	}
	struct dentry* parent = BPF_CORE_READ( dentry , d_parent )                            ; if (parent==dentry   )                       return 1/*stop*/ ;    // reached top level, ignore
	uint32_t       off    = ctx->off                                                      ;
	const char*    name   = (const char*)BPF_CORE_READ( dentry , d_name.name )            ; if (!name            ) { ctx->panic = true ; return 1/*stop*/ ;  } // to please verifier
	uint32_t       len1   =              BPF_CORE_READ( dentry , d_name.len  )+1/*null*/  ; if (off+len1>PATH_MAX)                       return 1/*.   */ ;    // path too long    , ignore
	/**/                                                                                    if (off >PATH_MAX    ) { ctx->panic = true ; off  = PATH_MAX   ; } // to please verifier
	long           n1     = bpf_probe_read_kernel_str( ev->data+off , NAME_MAX+1 , name ) ; if (n1!=len1         ) { ctx->panic = true ; return 1/*stop*/ ;  } // defensive
	//
	ctx->off             = off + (uint32_t)n1 ; // terminating null acts as a component separator and will be converted into / (in reverse order) in userland
	ctx->root_key.dentry = parent             ;
	return 0/*stop*/ ;
}
static __always_inline bool/*ok*/ _walk( Task* tv , struct dentry* dentry ) {
	uint32_t   jid = tv->job_id                               ; if ( !jid || jid>=N_JOBS ) return false/*ok*/ ; // 32-bit so as to allow bpf_map_lookup_elem
	JobInfo  * ji  = bpf_map_lookup_elem( &job_infos , &jid ) ; if (!ji                  ) return false/*. */ ;
	EbpfEvent* ev  = &tv->ev                                  ;
	RootKey    rk  = {}                                       ; rk.dentry = dentry ; rk.job_id = (JobId)jid ;
	WalkCtx    ctx = {
		.root_key = rk
	,	.ev       = ev
	,	.off      = tv->file_len                                                                                // append to whatever is already there
	,	.panic    = false
	,	.report   = false
	} ;
	bpf_loop( WALK_MAX_DEPTH , _walk_step , &ctx , 0/*flags*/ ) ;
	if (ctx.panic  ) { _panic(tv,"cannot walk") ;                  return false/*ok*/ ; }
	if (!ctx.report) { ev->base.hdr.file_kind = EBPF_FILE_IGNORE ; return true /*. */ ; }
	tv->file_len =  ctx.off ;
	return true/*ok*/ ;
}

//
// helpers
//

// state of current thread if its accesses must be captured, i.e. it belongs to a job and executes a tracked syscall or an io_uring operation
static __always_inline Task* _capturing1(struct task_struct* t) {
	Task* tv = bpf_task_storage_get( &tasks , t , 0/*value*/ , 0/*flags*/ ) ; if (!tv            ) return 0 ;
	/**/                                                                      if (tv->is_tracer  ) return 0 ;
	/**/                                                                      if (tv->disable_cnt) return 0 ;
	return tv ;
}
static __always_inline Task* _capturing2() {
	return _capturing1(bpf_get_current_task_btf()) ;
}

// file info, as stat would report it
struct inode___v611 { __s64             i_mtime_sec ; uint32_t i_mtime_nsec ; } __attribute__((preserve_access_index)) ;                                             // 6.11 <= linux
struct inode___v66  { struct timespec64 __i_mtime   ;                         } __attribute__((preserve_access_index)) ;                                             // 6.6  <= linux < 6.11
struct inode___old  { struct timespec64 i_mtime     ;                         } __attribute__((preserve_access_index)) ;                                             //         linux < 6.6
static __noinline bool/*ok*/ _capture_solved( Task* tv , struct dentry* dentry ) { //!                                                                            ok
	EbpfEvent*    ev    = &tv->ev                           ; if (ev->base.hdr.file_kind                  )                                                return true  ;
	bool          ok    = _walk( tv , dentry )              ; if (!ok                                     )                                                return false ;
	/**/                                                      if (ev->base.hdr.file_kind==EBPF_FILE_IGNORE)                                                return true  ;
	struct inode* inode = BPF_CORE_READ( dentry , d_inode ) ; if (!inode                                  ) { ev->base.hdr.file_kind = EBPF_FILE_NO_FILE ; return true  ; }
	umode_t       mode  = BPF_CORE_READ( inode  , i_mode  ) ;
	//
	if      ((mode&S_IFMT)==S_IFREG) ev->base.hdr.file_kind = mode&S_IXALL ? EBPF_FILE_EXE : EBPF_FILE_REG ;
	else if ((mode&S_IFMT)==S_IFLNK) ev->base.hdr.file_kind = EBPF_FILE_LNK                                ;
	else                             ev->base.hdr.file_kind = EBPF_FILE_NO_FILE                            ;
	ev->base.size = BPF_CORE_READ( inode , i_size ) ;
	#define HAS_FIELD(s,f)        bpf_core_field_exists(((struct s*)0)->f)
	#define MTIME_NS(in,sec,nsec) (uint64_t)BPF_CORE_READ(in,sec)*1000000000 + BPF_CORE_READ(in,nsec)
	if      (HAS_FIELD(inode___v611,i_mtime_sec     )) ev->base.mtime_ns = MTIME_NS( (struct inode___v611*)inode , i_mtime_sec      , i_mtime_nsec      ) ;
	else if (HAS_FIELD(inode___v66 ,__i_mtime.tv_sec)) ev->base.mtime_ns = MTIME_NS( (struct inode___v66 *)inode , __i_mtime.tv_sec , __i_mtime.tv_nsec ) ;
	else if (HAS_FIELD(inode___old ,i_mtime.tv_sec  )) ev->base.mtime_ns = MTIME_NS( (struct inode___old *)inode , i_mtime.tv_sec   , i_mtime.tv_nsec   ) ;
	#undef HAS_FIELD
	#undef MTIME_NS
	//
	return true/*ok*/ ;
}

static __always_inline bool/*ok*/ _emit_solved( Task* tv , struct dentry* dentry ) {
	return _capture_solved(tv,dentry) && _emit(tv) ;
}

// capture a path that could not be resolved : the path as provided by the user and the dir it is relative to
static __noinline bool/*ok*/ _emit_user( struct task_struct* t , Task* tv , int dfd,const char* path , bool kernel ) {
	EbpfEvent* ev  = &tv->ev                                  ; if (ev->base.hdr.file_kind) goto Emit          ;       // file is already known
	uint32_t   jid = tv->job_id                               ; if (jid>=N_JOBS           ) return false/*ok*/ ;       // 32-bit so as to allow bpf_map_lookup_elem
	JobInfo*   ji  = bpf_map_lookup_elem( &job_infos , &jid ) ; if (!ji                   ) return false/*. */ ;
	//
	ev->base.hdr.file_kind = EBPF_FILE_USER ;
	//
	long n1 = 0 ;                                                                                                      // includes terminating null
	if (path) {
		if (kernel) n1 = bpf_probe_read_kernel_str( ev->data , PATH_MAX , path ) ;
		else        n1 = bpf_probe_read_user_str  ( ev->data , PATH_MAX , path ) ;
		if (n1<=0) {
			if ( kernel || (ev->base.hdr.flags&EBPF_F_OK) ) { _panic(tv,"cannot access path") ; return false/*ok*/ ; } // page is not resident any more, cannot do anything but report it
			return true/*ok*/ ;                                                                                        // no access if path was never accessible
		}
		if (ev->data[0]=='/') {                                                                                        // absolute path, no dfd interpretation
			if (_is_simple(ev->data,n1-1,ji->deps_in_system)) return true/*ok*/ ;
			tv->file_len = (uint16_t)n1 ;                                                                              // last char is root_id, 0 for absolute paths
			goto Emit ;
		}
	}
	// relative path, interpret dfd
	struct dentry* dentry = dentry_from_fd( t , dfd ) ; if (!dentry) return true/*ok*/ ;                               // dfd is closed
	tv->file_len = (uint16_t)n1 ;                                                                                      // keep terminating null which means a /
	bool ok = _walk( tv , dentry ) ; if (!ok) return false/*ok*/ ;
Emit :
	return _emit(tv) ;
}

//
// process tracking
//

// handle magic reqs
SEC("tp/syscalls/sys_enter_fsetxattr")
int on_sys_enter_fsetxattr(struct trace_event_raw_sys_enter* ctx) {
	int                 fd    = (int        )ctx->args[0]                                  ; if ( fd!=EBPF_MAGIC_FD        ) return 0 ;
	const char*         name  = (const char*)ctx->args[1]                                  ; if ( name                     ) return 0 ;   // no name, just a value
	void              * value = (void*      )ctx->args[2]                                  ;
	ulong               size  = (ulong      )ctx->args[3]                                  ; if (size!=sizeof(EbpfMagicReq)) return 0 ;
	struct task_struct* t     = bpf_get_current_task_btf()                                 ;
	EbpfMagicReq        req   ;
	long                rc    = bpf_probe_read_user( &req , sizeof(EbpfMagicReq) , value ) ; if (rc!=0                     ) return 0 ;
	if (req.proc==EBPF_MAGIC_REGISTER) {
		/**/                                                                                               if (req.mrkr!=Mrkr) return 0 ; // not the right filter
		uint32_t jid = req.job_id                                                                        ; if (jid>=N_JOBS   ) return 0 ; // 32-bit to allow bpf_map_lookup_elem
		Task   * tv  = bpf_task_storage_get( &tasks  , t , 0/*value*/ , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if (!tv           ) return 0 ;
		JobInfo* ji  = bpf_map_lookup_elem( &job_infos , &jid )                                          ; if (!ji           ) return 0 ;
		tv->job_id          = jid                  ;
		tv->is_tracer       = true                 ;                                                                                      // jobs are registered by tracer
		tv->ev.base.hdr.tid = BPF_CORE_READ(t,pid) ;                                                                                      // pid field is actually the tid
		ji->overflow        = false                ;
		ji->deps_in_system  = req.deps_in_system   ;
		return 0 ;
	}
	Task* tv = bpf_task_storage_get( &tasks , t , 0/*value*/ , 0/*flags*/ ) ; if (!tv) return 0 ; // note : do not consider disable_cnt to manipulate disable_cnt
	switch (req.proc) {
		case EBPF_MAGIC_DISABLE : if (tv->disable_cnt<(uint16_t)-1) tv->disable_cnt++ ; break ;
		case EBPF_MAGIC_ENABLE  : if (tv->disable_cnt>           0) tv->disable_cnt-- ; break ;
		case EBPF_MAGIC_JOB     :
			if (!tv->is_tracer) break ;                                                           // called from tracee, but before this call, it used to be tracer
			tv->is_tracer        = false          ;
			tv->is_job           = true           ;
			tv->ev.base.hdr.kind = EBPF_JOB_START ;
			_emit(tv) ;
		break ;
		case EBPF_MAGIC_ROOT : {
			/**/                                                                                                   if (!tv->is_tracer           ) break ;
			/**/                                                                                                   if (     req.root_id==0      ) break ; // root_id 0 is reserved for absolute paths
			/**/                                                                                                   if ((int)req.root_id>=N_ROOTS) break ;
			struct dentry* dentry = dentry_from_fd( t , req.root_fd )                                            ; if (!dentry                  ) break ;
			RootKey        rk     = { .dentry=dentry_from_fd( t , req.root_fd ) , .job_id=tv->job_id , .pad={} } ;
			bpf_map_update_elem( &roots , &rk , &req.root_id , BPF_NOEXIST ) ;
		} break ;
		default : ;
	}
    return 0 ;
}

static __always_inline int _new_task( struct task_struct* parent , struct task_struct* child , bool chk_new_proc ) {
	Task* ptv = bpf_task_storage_get( &tasks , parent , 0/*value*/ , 0/*flags*/                     ) ; if (!ptv)                                              return 0 ;
	Task* ctv = bpf_task_storage_get( &tasks , child  , 0/*.    */ , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if (!ctv) { _panic(ptv,"cannot create thread entry") ; return 0 ; }
	//
	ctv->job_id           = ptv->job_id       ;
	ctv->is_tracer        = ptv->is_tracer    ;
	ctv->disable_cnt      = ptv->disable_cnt  ;
	ctv->ev.base.hdr.kind = EBPF_THREAD_START ;
	if (
		chk_new_proc
	&&		BPF_CORE_READ(child ,group_leader,thread_pid)
		!=	BPF_CORE_READ(parent,group_leader,thread_pid)
	) ctv->ev.base.hdr.kind = EBPF_PROC_START ;
	_emit(ctv) ;
	return 0 ;
}

// a new thread or process inherits the job of its creator
// sched_process_fork is called once creation is sure to succeed, so there will be a corresponding sched_process_exit
SEC("tp_btf/sched_process_fork")
int BPF_PROG( on_fork , struct task_struct* parent , struct task_struct* child ) {
	return _new_task( parent , child , true/*chk_new_proc*/ ) ;
}

// io_uring workers are not created through fork, hence they are not seen above
// they are not announced as their creation may still fail (but their end is), they just need to belong to the job
SEC("tp_btf/task_newtask")
int BPF_PROG( on_newtask , struct task_struct* child , uint64_t clone_flags ) {
	if (!(BPF_CORE_READ(child,flags)&IO_WORKER)) return 0 ;
	struct task_struct* parent = bpf_get_current_task_btf() ;
	return _new_task( parent , child , false/*new_proc*/ ) ;
}

// if a thread is killed while processing a syscall, its captures are never closed : nobody can depend on such accesses
SEC("tp_btf/sched_process_exit")
int BPF_PROG( on_exit , struct task_struct* t ) {
	Task     * tv = bpf_task_storage_get( &tasks , t , 0/*value*/ , 0/*flags*/ ) ; if (!tv) return 0 ;
	EbpfEvent* ev = &tv->ev                                                      ;
	if      (tv->is_job                           ) ev->base.hdr.kind = EBPF_JOB_END    ;
	else if (BPF_CORE_READ(t,signal,nr_threads)==1) ev->base.hdr.kind = EBPF_PROC_END   ;
	else                                            ev->base.hdr.kind = EBPF_THREAD_END ;
	_emit(tv) ;
	if (tv->is_job) {
		uint32_t jid = tv->job_id                               ; if ( !jid || jid>=N_JOBS  ) return 0 ; // cast to 32-bit so we can use it in bpf_map_lookup_elem
		JobInfo* ji  = bpf_map_lookup_elem( &job_infos , &jid ) ; if ( !ji  || ji->overflow ) return 0 ;
		ji->overflow = true ;                                                                            // ensure no further messages
	}
	return 0 ;
}

//
// captures
//

// symlinks traversed during path resolution
// this is called for each traversed symlink, whether in rcu-walk or in ref-walk mode (a resolution may be restarted, in which case links are reported twice)
// the target is available from the inode for short links (those stored in the inode), else it is retrieved by the file system and consumer will have to read it
// note : vfs_get_link is not used by path resolution, which directly calls the file system
SEC("fentry/security_inode_follow_link")
int BPF_PROG( on_follow_link , struct dentry* dentry , struct inode* inode , bool rcu ) {
	Task      * tv     = _capturing2()                   ; if (!tv) return 0 ;
	EbpfEvent * ev     = &tv->ev                         ;
	const char* target = BPF_CORE_READ( inode , i_link ) ;
	if (target) {
		long n1 = bpf_probe_read_kernel_str( ev->data , PATH_MAX , target ) ; // includes terminating null
		if (n1>0) {
			tv->file_len           = n1             ;                         // keep terminating null to represent absolute path (not relative to any root)
			ev->base.hdr.kind      = EBPF_TARGET    ;
			ev->base.hdr.file_kind = EBPF_FILE_USER ;
			if (!_emit(tv)) return 0 ;
		}
	}
	bool    ok     = _capture_solved( tv , dentry ) ; if (!ok) return 0 ;
	uint8_t n_lnks = ev->base.hdr.n_lnks            ;                         // accumulate n_lnk, do not reset it
	ev->base.hdr.kind  = EBPF_PATH_RES            ;
	ev->base.hdr.flags = EBPF_F_DEP_LNK|EBPF_F_OK ;
	if (!_emit(tv)) return 0 ;
	ev->base.hdr.n_lnks = n_lnks+1 ;
	return 0 ;
}

// all the syscalls that just resolve a path go through filename_lookup : stat, access, readlink, chmod, truncate, chdir, source of link, ...
// when it succeeds, path is the resolved object, and nothing has been modified yet
SEC("fexit/filename_lookup")
int BPF_PROG( on_filename_lookup , int dfd , struct filename* name , unsigned flags , struct path* path , struct path* root , int ret ) {
	/**/                       if (ret!=0) return 0 ;                                                                                     // unresolved paths are handled at syscall exit
	Task* tv = _capturing2() ; if (!tv   ) return 0 ;
	_capture_solved( tv , BPF_CORE_READ(path,dentry) ) ;
	return 0 ;
}

// all the opens also go through do_filp_open, a global function called from other files (hence it cannot be inlined)
// it is used to know what is opened (with or without success) : the file of an open, but also the interpreter of a script upon execve, which is not an arg of any syscall
// hence, _emit is done here, not at sysexit time
SEC("fexit/do_filp_open")
int BPF_PROG( on_filp_open , int dfd , struct filename* pathname , const struct open_flags* op , struct file* ret ) {
	struct task_struct* t     = bpf_get_current_task_btf()  ;
	Task              * tv    = _capturing1(t)              ; if (!tv) return 0 ;
	EbpfEvent         * ev    = &tv->ev                     ;
	uint8_t             ok    = _is_ok((uint64_t)ret)       ;
	int                 flags = BPF_CORE_READ(op,open_flag) ;
	//
	ev->base.hdr.kind = EBPF_READ ;
	if ( _open_no_follow(flags)) ev->base.hdr.flags |= EBPF_F_NO_FOLLOW ;
	if (!_open_dir      (flags)) {
		if (_open_do_write(flags)) {
			/**/                      ev->base.hdr.kind   = EBPF_WRITE      ;
			if (_open_do_read(flags)) ev->base.hdr.flags |= EBPF_F_DEP_REG  ;
			if (_open_do_stat(flags)) ev->base.hdr.flags |= EBPF_F_DEP_STAT ;
		} else {
			ev->base.hdr.flags |= EBPF_F_DEP_ALL ; // a fstat can be performed on returned fd
		}
	}
	if (ok) { ev->base.hdr.flags |= EBPF_F_OK ; _emit_solved(     tv , BPF_CORE_READ(ret,f_path.dentry)                  ) ; }
	else                                        _emit_user  ( t , tv , dfd,BPF_CORE_READ(pathname,name) , true/*kernel*/ ) ;
	return 0 ;
}

static __always_inline int _do_nod1(struct dentry* dentry) {
	Task* tv = _capturing2() ; if (!tv) return 0 ;
	_capture_solved( tv , dentry ) ;
	return 0 ;
}
static __always_inline int _do_nod2( struct dentry* old_dentry , struct dentry* new_dentry , bool exchange ) {
	Task     * tv = _capturing2() ; if (!tv) return 0 ;
	EbpfEvent* ev = &tv->ev       ;
	_reset_ev(tv) ;                       // XXX : improve by using source resolution rather than redoing it
	ev->base.hdr.kind  = EBPF_TARGET    ;
	ev->base.hdr.flags = EBPF_F_DEP_ALL ;
	if (!_emit_solved   ( tv , new_dentry )) return 0 ;
	if (!_capture_solved( tv , old_dentry )) return 0 ;
	return 0 ;
}

SEC("fentry/security_path_mkdir"  ) int BPF_PROG( on_path_mkdir   , struct path* d ,struct dentry* e                                               ) { return _do_nod1(e                      ) ; }
SEC("fentry/security_path_mknod"  ) int BPF_PROG( on_path_mknod   , struct path* d ,struct dentry* e  , mode_t m                                   ) { return _do_nod1(e                      ) ; }
SEC("fentry/security_path_symlink") int BPF_PROG( on_path_symlink , struct path* d ,struct dentry* e                                               ) { return _do_nod1(e                      ) ; }
SEC("fentry/security_path_unlink" ) int BPF_PROG( on_path_unlink  , struct path* d ,struct dentry* e                                               ) { return _do_nod1(e                      ) ; }
SEC("fentry/security_path_rmdir"  ) int BPF_PROG( on_path_rmdir   , struct path* d ,struct dentry* e                                               ) { return _do_nod1(e                      ) ; }
SEC("fentry/security_path_link"   ) int BPF_PROG( on_path_link    ,                 struct dentry* oe , struct path* nd,struct dentry* ne          ) { return _do_nod2(oe,ne,false/*exchange*/) ; }
SEC("fentry/security_path_rename" ) int BPF_PROG( on_path_rename  , struct path* od,struct dentry* oe , struct path* nd,struct dentry* ne , uint f ) { return _do_nod2(oe,ne,f&RENAME_EXCHANGE) ; }

//
// end of syscalls
//

SEC("tp_btf/sys_exit")
int BPF_PROG( on_sys_exit , struct pt_regs* regs , long ret ) {
	long nr ;
	#if defined(__TARGET_ARCH_x86)
		nr = (long)regs->orig_ax ;
	#elif defined(__TARGET_ARCH_arm64)
		nr = (long)regs->syscallno ;
	#else
		#error "unsupported architecture"
	#endif
	nr &= ~X32_SYSCALL_BIT ;
	/**/                                                     if ( nr<0 || nr>=N_SYSCALLS ) return 0 ;                      // x32 has no effect on data structures we are sensitive to
	struct task_struct* t     = bpf_get_current_task_btf() ;
	Task              * tv    = _capturing1(t)             ; if ( !tv                    ) return 0 ;
	EbpfEvent         * ev    = &tv->ev                    ;
	bool                ok    = _is_ok(ret)                ;
	bool                is_32 = false                      ;
	#if HAS_32
		is_32 = (BPF_CORE_READ(t,thread_info.status)&SYSCALL_32)!=0 ;
	#endif
	//
	EbpfScEntry descr     = Syscalls[is_32][nr]      ; if (!descr        ) return 0 ;
	bool        report    = true                     ;
	uint8_t     sc_kind   = EBPF_SCE_KIND (descr)    ; if (_is_retry(ret)) sc_kind = EBPF_SC_NONE ;                        // error is external, no real dependency nor action
	uint8_t     arg_dfd1  = EBPF_SCE_DFD1 (descr)    ;
	uint8_t     arg_path1 = EBPF_SCE_PATH1(descr)    ;
	uint8_t     arg_dfd2  = EBPF_SCE_DFD2 (descr)    ;
	uint8_t     arg_path2 = EBPF_SCE_PATH2(descr)    ;
	uint8_t     arg_flgs  = EBPF_SCE_FLAGS(descr)    ;
	uint8_t     no_follow = EBPF_SCE_NF   (descr)    ;
	uint64_t    args[8]   = {0,0,0,0,0,0,0,AT_FDCWD} ;                                                                     // AT_FDCWD simplifies cases where there is no dfd
	#if defined(__TARGET_ARCH_x86)
		if (is_32) { args[0]=regs->bx      ; args[1]=regs->cx      ; args[2]=regs->dx      ; args[3]=regs->si      ; args[4]=regs->di      ; args[5]=regs->bp      ; }
		else       { args[0]=regs->di      ; args[1]=regs->si      ; args[2]=regs->dx      ; args[3]=regs->r10     ; args[4]=regs->r8      ; args[5]=regs->r9      ; }
	#elif defined(__TARGET_ARCH_arm64)
		/**/         args[0]=regs->orig_x0 ; args[1]=regs->regs[1] ; args[2]=regs->regs[2] ; args[3]=regs->regs[3] ; args[4]=regs->regs[4] ; args[5]=regs->regs[5] ;
	#else
		#error "unsupported architecture"
	#endif
	if (ok          ) ev->base.hdr.flags |= EBPF_F_OK        ;
	if (no_follow==1) ev->base.hdr.flags |= EBPF_F_NO_FOLLOW ;                                                             // no_follow>1 means value comes from syscall flags
	switch (sc_kind) {
		case EBPF_SC_FORBIDDEN : _panic(tv,"forbidden syscall") ;                                           return 0     ;
		case EBPF_SC_NONE      :                                                                            break        ;
		case EBPF_SC_OPEN      :                                                                            break        ; // already handled in do_filp_open
		case EBPF_SC_CREATE    : ev->base.hdr.kind = EBPF_WRITE   ; ev->base.hdr.flags |= EBPF_F_DEP_STAT ; goto Handle1 ;
		case EBPF_SC_PATH      : ev->base.hdr.kind = EBPF_READ    ;                                         goto Handle1 ;
		case EBPF_SC_READLINK  : ev->base.hdr.kind = EBPF_READ    ; ev->base.hdr.flags |= EBPF_F_DEP_LNK  ; goto Handle1 ;
		case EBPF_SC_STAT      : ev->base.hdr.kind = EBPF_READ    ; ev->base.hdr.flags |= EBPF_F_DEP_ALL  ; goto Handle1 ;
		case EBPF_SC_UPDATE    : ev->base.hdr.kind = EBPF_WRITE   ; ev->base.hdr.flags |= EBPF_F_DEP_ALL  ; goto Handle1 ;
		case EBPF_SC_UNLINK    : {
			if ( arg_flgs!=EBPF_SCE_NO_ARG && (args[arg_flgs]&AT_REMOVEDIR) )   ev->base.hdr.kind = EBPF_READ   ;
			else                                                              { ev->base.hdr.kind = EBPF_UNLINK ; ev->base.hdr.flags |= EBPF_F_DEP_STAT ; }
		} goto Handle1 ;
		Handle1 : {
			if ( no_follow>1 && arg_flgs!=EBPF_SCE_NO_ARG && (args[arg_flgs]&AT_SYMLINK_NOFOLLOW) ) ev->base.hdr.flags |= EBPF_F_NO_FOLLOW ;
			_emit_user( t , tv , arg_dfd1==EBPF_SCE_NO_ARG?AT_FDCWD:(int)args[arg_dfd1&7] , (const char*)args[arg_path1&7] , false/*kernel*/ ) ;
		} break ;
		case EBPF_SC_LINK   :
			ev->base.hdr.kind   = EBPF_LINK        ;
			ev->base.hdr.flags |= EBPF_F_NO_FOLLOW ;
			if ( no_follow>1 && arg_flgs!=EBPF_SCE_NO_ARG && (args[arg_flgs]&AT_SYMLINK_FOLLOW) ) ev->base.hdr.flags &= ~EBPF_F_NO_FOLLOW ;
		goto Handle2 ;
		case EBPF_SC_RENAME :
			if ( arg_flgs!=EBPF_SCE_NO_ARG && (args[arg_flgs]&RENAME_EXCHANGE) ) ev->base.hdr.kind = EBPF_EXCHANGE ;
			else                                                                 ev->base.hdr.kind = EBPF_RENAME   ;
		Handle2 : {
			if (!ev->base.hdr.file_kind) {
				uint8_t kind = ev->base.hdr.kind ;
				ev->base.hdr.kind = EBPF_TARGET ;
				if (!_emit_user( t , tv , arg_dfd2==EBPF_SCE_NO_ARG?AT_FDCWD:(int)args[arg_dfd2&7] , (const char*)args[arg_path2&7] , false/*kernel*/ )) return 0 ;
				ev->base.hdr.kind = kind ;
			}
			_emit_user( t , tv , arg_dfd1==EBPF_SCE_NO_ARG?AT_FDCWD:(int)args[arg_dfd1&7] , (const char*)args[arg_path1&7] , false/*kernel*/ ) ;
		} break ;
		case EBPF_SC_READDIR :
			ev->base.hdr.kind = EBPF_READDIR ;
			_emit_user( t , tv , (int)args[arg_dfd1&7] , 0/*path*/ , false/*kernel*/ ) ;
		break ;
		default : _panic(tv,"bad syscall kind") ; return 0 ;
	}
	return 0 ;
}

//
// io_uring : operations are executed either by the thread that submits them or by a worker, and their completion may be reported from yet another context
// so they are closed at the exit of their handler, which is the only place where we know to be in the context where captures were done
// args are laid out as for the equivalent syscall
//

static __always_inline Task* _capturing_io( struct task_struct* t , int ret ) {
	Task* tv = _capturing1(t) ; if (!tv           )                                                  return 0 ;
	/**/                        if (_is_retry(ret)) { tv->ev.base.hdr.kind = EBPF_NONE ; _emit(tv) ; return 0 ; } // request will be re-issued (typically from a worker)
	return tv ;
}

#define HANDLE_IO1( io_name , io_typ , kind_expr , flags_expr , dfd_field,file_field , no_follow_expr , active_expr ) \
	SEC( "fexit/" #io_name )                                                                             \
	int BPF_PROG( on_##io_name , struct io_kiocb* req_ , uint flags , int ret ) {                        \
		struct task_struct* t      = bpf_get_current_task_btf()             ;                            \
		Task              * tv     = _capturing_io(t,ret)                   ; if (!tv) return 0 ;        \
		EbpfEvent         * ev     = &tv->ev                                ;                            \
		struct io_typ     * req    = (struct io_typ*)req_                   ;                            \
		int                 dfd    = BPF_CORE_READ( req , dfd_field       ) ;                            \
		const char*         path   = BPF_CORE_READ( req , file_field,name ) ;                            \
		bool                active = active_expr                            ;                            \
		if (active        ) { ev->base.hdr.kind = kind_expr ; ev->base.hdr.flags |= flags_expr       ; } \
		else                  ev->base.hdr.kind = EBPF_READ ;                                            \
		if (no_follow_expr)                                   ev->base.hdr.flags |= EBPF_F_NO_FOLLOW ;   \
		if (_is_ok(ret)   )                                   ev->base.hdr.flags |= EBPF_F_OK        ;   \
		_emit_user( t , tv , dfd,path , true/*kernel*/ ) ;                                               \
		return 0 ;                                                                                       \
	}

#define HANDLE_IO2( io_name , io_typ , kind_expr , no_follow_expr ) \
	SEC( "fexit/" #io_name )                                                                  \
	int BPF_PROG( on_##io_name , struct io_kiocb* req_ , uint flags , int ret ) {             \
		struct task_struct* t     = bpf_get_current_task_btf()          ;                     \
		Task              * tv    = _capturing_io(t,ret)                ; if (!tv) return 0 ; \
		EbpfEvent         * ev    = &tv->ev                             ;                     \
		struct io_typ     * req   = (struct io_typ*)req_                ;                     \
		int                 dfd1  = BPF_CORE_READ( req , old_dfd      ) ;                     \
		const char*         path1 = BPF_CORE_READ( req , oldpath,name ) ;                     \
		int                 dfd2  = BPF_CORE_READ( req , new_dfd      ) ;                     \
		const char*         path2 = BPF_CORE_READ( req , newpath,name ) ;                     \
		if (!ev->base.hdr.file_kind) {                                                        \
			ev->base.hdr.kind = EBPF_TARGET ;                                                 \
			if (!_emit_user( t , tv , dfd2,path2 , true/*kernel*/ )) return 0 ;               \
		}                                                                                     \
		/**/                ev->base.hdr.kind   = kind_expr        ;                          \
		if (no_follow_expr) ev->base.hdr.flags |= EBPF_F_NO_FOLLOW ;                          \
		if (_is_ok(ret)   ) ev->base.hdr.flags |= EBPF_F_OK        ;                          \
		_emit_user( t , tv , dfd1,path1 , true/*kernel*/ ) ;                                  \
		return 0 ;                                                                            \
	}

HANDLE_IO1( io_mkdirat  , io_mkdir  , EBPF_READ   , 0/*flags*/      , dfd    ,filename , true/*no_follow*/                            , true/*active*/                           )
HANDLE_IO1( io_statx    , io_statx  , EBPF_READ   , EBPF_F_DEP_ALL  , dfd    ,filename , BPF_CORE_READ(req,flags)&AT_SYMLINK_NOFOLLOW , _statx_is_stat(BPF_CORE_READ(req,mask))  )
HANDLE_IO1( io_symlinkat, io_link   , EBPF_WRITE  , EBPF_F_DEP_STAT , new_dfd,newpath  , true/*no_follow*/                            , true/*active*/                           )
HANDLE_IO1( io_unlinkat , io_unlink , EBPF_UNLINK , EBPF_F_DEP_STAT , dfd    ,filename , true/*.        */                            , !(BPF_CORE_READ(req,flags)&AT_REMOVEDIR) )

HANDLE_IO2( io_linkat   , io_link   , EBPF_LINK                                         , !(BPF_CORE_READ(req,flags)&AT_SYMLINK_FOLLOW) )
HANDLE_IO2( io_renameat , io_rename , (flags&RENAME_EXCHANGE)?EBPF_RENAME:EBPF_EXCHANGE , true/*no_follow*/                             )
