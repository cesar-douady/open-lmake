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

#include "syscall_tab.h"

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

#define IO_WORKER    0x10 // task is an io_uring worker
#define ERESTART_MIN 512  // from 512 to 516, the syscall will be restarted
#define ERESTART_MAX 516

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
// globals
//

JobInfo job_infos[N_JOBS] ;

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

typedef enum {
	LOC_NONE
,	LOC_UNKNOWN
,	LOC_REPO                                          // repo root or a src dir
,	LOC_SUPER                                         // a super-dir of repo root or a src dir
,	LOC_EXTERN
} Loc ;
typedef struct {
	JobId     job_id       ;
	bool      is_tracer    ;                          // true for tracer that launches job
	bool      is_job       ;                          // true for root thread of job
	uint16_t  disable_cnt  ;                          // autodep is active if 0
	uint16_t  file_len     ;                          // len of file name in ev
	uint8_t   last_lnk_loc ;
	EbpfEvent ev           ;                          // event being built, off-stack as it is large
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
struct {
	__uint( type        , BPF_MAP_TYPE_HASH ) ;
	__uint( max_entries , N_JOBS*N_ROOTS    ) ;
	__uint( map_flags   , BPF_F_NO_PREALLOC ) ;
	__type( key         , RootKey           ) ;
	__type( value       , RootId            ) ;
} roots SEC(".maps") ;

typedef struct {
	uint64_t hash                                ;
	JobId    job_id                              ;
	char     pad[sizeof(uint64_t)-sizeof(JobId)] ; // ensure no unitialized bits in key as hash is computed after raw bits
} RootStrKey ;
struct {                                           // state of the traced threads
	__uint( type        , BPF_MAP_TYPE_HASH ) ;
	__uint( max_entries , N_JOBS*N_ROOTS    ) ;
	__uint( map_flags   , BPF_F_NO_PREALLOC ) ;
	__type( key         , RootStrKey        ) ;
	__type( value       , bool/*repo*/      ) ;    // if false, entry is a super-dir
} root_strs SEC(".maps") ;

//
// utils
//

static __always_inline int32_t _atomic_ld ( int32_t* p             ) { return __atomic_fetch_sub ( p , 0 , __ATOMIC_RELAXED ) ; } // XXX/ : __atomic_load_n  is broken in clang
static __always_inline void    _atomic_st ( int32_t* p , int32_t v ) {        __atomic_exchange_n( p , v , __ATOMIC_RELAXED ) ; } // XXX/ : __atomic_store_n is broken in clang
static __always_inline void    _atomic_sub( int32_t* p , int32_t v ) {        __atomic_fetch_sub ( p , v , __ATOMIC_RELAXED ) ; }

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
	struct fdtable* fdt = BPF_CORE_READ( t   , files,fdt ) ;
	uint            max = BPF_CORE_READ( fdt , max_fds   ) ;                                                    if ((uint)fd>=max) return 0 ; // fd is closed
	struct file*  * fda = BPF_CORE_READ( fdt , fd        ) ;
	struct file   * f   = 0                                ; bpf_probe_read_kernel( &f , sizeof(f) , fda+fd ) ; if (!f           ) return 0 ; // .
	//
	return BPF_CORE_READ( f , f_path.dentry ) ;
}

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

typedef struct {
	const char* file       ;
	bool        seen_plain ;
	uint8_t     n_dots     ;                                                          // number of . seen in current components (max 3)
	uint16_t    n_dot_dots ;
} NDotDotsCtx ;
static long/*stop*/ _n_dot_dots_step( uint64_t idx , void* ctx ) {
	NDotDotsCtx* c = ctx          ;
	/**/                            if (idx>=PATH_MAX) idx = PATH_MAX - 1 ;           // to please verifier
	char         v = c->file[idx] ;
	switch (v) {
		case 0   :
		case '/' :
			switch (c->n_dots) {
				case 0 :                                                              // empty : no update
				case 1 : break ;                                                      // .     : no update
				case 2 :                                                              // ..
					if (c->seen_plain) { c->n_dot_dots = -1 ; return true/*stop*/ ; } // .. after plain are too complex to track
					else                 c->n_dot_dots++ ;                            // initial ..
				break ;
				default : c->seen_plain = true ;
			}
			if (!v) return true/*stop*/ ;
			c->n_dots = 0 ;
		break ;
		case '.' :
			if (c->n_dots<=2) c->n_dots++ ;
		break ;
		default :
			c->n_dots = 3 ;                                                           // normal dir (i.e. neither . nor ..)
	}
	return false/*stop*/ ;
}
static __always_inline uint16_t _n_dot_dots(const char* file) {
	NDotDotsCtx ctx = { file } ;
	bpf_loop( PATH_MAX , _n_dot_dots_step , &ctx , 0/*flags*/ ) ;
	return ctx.n_dot_dots ;
}

typedef struct {
	const char* file         ;
	RootStrKey  root_str_key ;
	bool        localize     ;
	uint8_t     n_dots       ;                                               // number of . seen in current components (if 3, not empty, . or ..)
	uint8_t     loc          ;
} LocateCtx ;
static __always_inline void _locate_fnv_add( LocateCtx* ctx , char v ) {
	uint64_t* hash = &ctx->root_str_key.hash ;
	*hash = (*hash^v) * 0x00000100000001b3 ;                                 // FNV-64 prime
}
static long/*stop*/ _locate_step( uint64_t idx , void* ctx ) {
	LocateCtx* c = ctx          ;
	/**/                          if (idx>=PATH_MAX) idx = PATH_MAX - 1 ;    // to please verifier
	char       v = c->file[idx] ;
	if (!idx) {
		c->root_str_key.hash = 0xcbf29ce484222325 ;                          // FNV-64 offset
		c->n_dots            = 3                  ;                          // process initial /
	}
	switch (v) {
		case 0   :
		case '/' : {
			uint8_t n_dots = c->n_dots ;
			c->n_dots = 0 ;
			switch (n_dots) {
				case 0 :                                                     // empty : no update
				case 1 : return false/*stop*/ ;                              // .     : no update
				case 2 :
					if (c->localize) c->loc               = LOC_UNKNOWN ;    // .. is too complex to compute hash on canonic name
					else             c->root_str_key.hash = -1          ;
					return true /*stop*/ ;
				default : ;
			}
			if (c->localize) {
				bool* l = bpf_map_lookup_elem( &root_strs , &c->root_str_key ) ;
				if (!l) { c->loc = LOC_EXTERN ; return true/*stop*/ ; }
				if (*l) { c->loc = LOC_REPO   ; return true/*.   */ ; }
				if (!v) { c->loc = LOC_SUPER  ; return true/*.   */ ; }
			} else {
				if (!v) return true/*stop*/ ;
			}
			_locate_fnv_add( c , '/' ) ;
		} break ;
		case '.' :
			if (c->n_dots<=1) { c->n_dots++ ; return false/*stop*/ ; }
		[[fallthrough]] ;
		default :
			if (c->n_dots<3) {
				if (c->n_dots>=1) _locate_fnv_add( c , '.' ) ;
				if (c->n_dots>=2) _locate_fnv_add( c , '.' ) ;
			}
			_locate_fnv_add( c , v ) ;
			c->n_dots = 3 ;
	}
	return false/*stop*/ ;
}
static __always_inline Loc _locate( const char* file , JobId job_id ) {
	LocateCtx ctx = { .file=file , .root_str_key={job_id} , true/*localize*/ } ;
	bpf_loop( PATH_MAX , _locate_step , &ctx , 0/*flags*/ ) ;
	return ctx.loc ;
}
static __always_inline uint64_t _hash(const char* file) {
	LocateCtx ctx = { .file=file , .root_str_key={0} , false/*localize*/ } ; // no need for job_id when not localizing
	bpf_loop( PATH_MAX , _locate_step , &ctx , 0/*flags*/ ) ;
	return ctx.root_str_key.hash ;
}

static __always_inline pid_t _get_ptid_in_ns( struct task_struct *t , bool get_pid ) {
	struct pid* pid ;
	if (get_pid) pid = BPF_CORE_READ( t , group_leader,thread_pid ) ;
	else         pid = BPF_CORE_READ( t ,              thread_pid ) ;
	if (!pid) return 0 ;
	uint level = BPF_CORE_READ( pid , level ) ;
	return BPF_CORE_READ( pid , numbers[level].nr ) ;
} //!                                                                                           get_pid
static __always_inline pid_t _get_pid_in_ns(struct task_struct *t) { return _get_ptid_in_ns( t , true  ) ; }
static __always_inline pid_t _get_tid_in_ns(struct task_struct *t) { return _get_ptid_in_ns( t , false ) ; }

//
// emission
//

static __always_inline void _reset_ev(Task* tv) {
	EbpfEvent* ev  = &tv->ev     ;
	uint32_t   tid = ev->hdr.tid ;     // empty event, but keep tid
	ev->hdr      = (EbpfEventHdr){0} ;
	ev->hdr.tid  = tid               ;
	tv->file_len = 0                 ;
}

// push tv->ev to the ring buffer of its job, if the job is gone, the event is dropped
static __always_inline bool/*ok*/ _emit( struct task_struct* t , Task* tv ) { //!                                  ok
	/**/                                                        if ( tv->is_tracer                        ) return false ; // defensive
	uint32_t   jid = tv->job_id                               ; if ( !jid || jid>=N_JOBS                  ) return false ; // 32-bits to access ring_bufs
	JobInfo*   ji  = job_infos + jid                          ;
	/**/                                                        if ( _atomic_ld(&ji->overflow) ) return false ;
	void     * rb  = bpf_map_lookup_elem( &ring_bufs , &jid ) ; if ( !rb                                  ) return false ;
	EbpfEvent* ev  = &tv->ev                                  ;
	uint32_t   sz  = sizeof(EbpfEventHdr)                     ;
	bool       ok  = true                                     ;
	//
	if ( ev->hdr.kind==EBPF_NONE                                            ) goto Done ;
	if ( ev->hdr.kind>=EBPF_HAS_DATA && ev->hdr.root_id<EBPF_ROOT_ID_REPORT ) goto Done ;
	//
	if (!( ev->hdr.flags & EBPF_F_STOPPED )) {                                                                             // force stop if ring buf has not enough room
		int32_t room = _atomic_ld(&ji->room) ;
		if (room<RING_BUF_MARGIN) {
			int room2 = RING_BUF_SZ - (int32_t)bpf_ringbuf_query( rb , BPF_RB_AVAIL_DATA ) ;
			_atomic_st( &ji->room , room2 ) ;
			room = RING_BUF_SZ - (int32_t)bpf_ringbuf_query( rb , BPF_RB_AVAIL_DATA ) ;                                    // ensure we do not leave an optimistic room
			if (room<room2) _atomic_sub( &ji->room , room2-room ) ;                                                        // cancel optimism
		}
		if (room<RING_BUF_MARGIN) ev->hdr.flags |= EBPF_F_STOPPED ;
	}
	if ( ev->hdr.flags & EBPF_F_STOPPED ) {
		if ( BPF_CORE_READ(t,flags) & IO_WORKER ) ev->hdr.flags &= ~EBPF_F_STOPPED ;                                       // io_uring worker threads cannot be stopped
		else                                      bpf_send_signal_thread(SIGSTOP) ;
	}
	if (ev->hdr.kind>=EBPF_HAS_DATA) {
		sz += tv->file_len ;
		if (sz>=sizeof(EbpfEvent)) return false ;                                                                          // please verifier
		if (HAS_STAMPS           ) ev->hdr.stamp_ns = bpf_ktime_get_tai_ns() ;
	}
	ok = bpf_ringbuf_output( rb , ev , sz , 0/*flags*/ )==0 ;
	if (ok) {
		_atomic_sub( &ji->room , (int32_t)sz ) ;
	} else {
		if ( ev->hdr.flags & EBPF_F_STOPPED ) bpf_send_signal_thread(SIGCONT) ;                                            // restart thread as userland has not been told to do so
		_atomic_st( &ji->overflow , true ) ;                                                                               // dont emit any further, in particular dont emit job end
	}
Done :
	_reset_ev(tv) ;
	return ok ;
}

static __always_inline void _panic( Task* tv , const char* reason ) {
	EbpfEvent* ev = &tv->ev ;
	int        n  ;
	ev->hdr.kind = EBPF_PANIC ;
	for( n=0 ; n<256 && reason[n] ; n++ ) ev->data[n] = reason[n] ; // 256 must be low enough for the verifier, but large enough to hold any error message
	tv->file_len = n ;
	_emit( bpf_get_current_task_btf() , tv ) ;
}

//
// process tracking
//

// handle magic reqs
SEC("tp/syscalls/sys_enter_fsetxattr")
int on_sys_enter_fsetxattr(struct trace_event_raw_sys_enter* ctx) {
	int                 fd    = (int        )ctx->args[0]                                  ; if ( fd!=EBPF_MAGIC_FD        ) return 0 ;
	const char*         name  = (const char*)ctx->args[1]                                  ;
	void              * value = (void*      )ctx->args[2]                                  ;
	ulong               size  = (ulong      )ctx->args[3]                                  ; if (size!=sizeof(EbpfMagicReq)) return 0 ;
	struct task_struct* t     = bpf_get_current_task_btf()                                 ;
	EbpfMagicReq        req   ;
	long                rc    = bpf_probe_read_user( &req , sizeof(EbpfMagicReq) , value ) ; if (rc!=0                     ) return 0 ;
	if (req.proc==EBPF_MAGIC_REGISTER) {
		/**/                                                                                               if ( req.mrkr!=Mrkr      ) return 0 ; // not the right filter
		uint32_t jid = req.job_id                                                                        ; if ( !jid || jid>=N_JOBS ) return 0 ; // XXX/ : clang seems to bug with 16-bits indexes
		JobInfo* ji  = job_infos + jid                                                                   ;
		Task   * tv  = bpf_task_storage_get( &tasks  , t , 0/*value*/ , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if ( !tv                 ) return 0 ;
		tv->job_id     = jid                  ;
		tv->is_tracer  = true                 ;                                                                                                  // jobs are registered by tracer
		tv->ev.hdr.tid = BPF_CORE_READ(t,pid) ;                                                                                                  // pid field is actually the tid
		_atomic_st( &ji->overflow , false       ) ;
		_atomic_st( &ji->room     , RING_BUF_SZ ) ;                                                                                              // when job registers, no events have been posted yet
		return 0 ;
	}
	Task* tv = bpf_task_storage_get( &tasks , t , 0/*value*/ , 0/*flags*/ ) ; if (!tv) return 0 ; // note : do not consider disable_cnt to manipulate disable_cnt
	switch (req.proc) {
		case EBPF_MAGIC_DISABLE : if (tv->disable_cnt<(uint16_t)-1) tv->disable_cnt++ ; break ;
		case EBPF_MAGIC_ENABLE  : if (tv->disable_cnt>           0) tv->disable_cnt-- ; break ;
		case EBPF_MAGIC_JOB     :
			if (!tv->is_tracer) break ;                                                           // called from tracee, but before this call, it used to be tracer
			tv->is_tracer   = false          ;
			tv->is_job      = true           ;
			tv->ev.hdr.kind = EBPF_JOB_START ;
			_emit( t , tv ) ;
		break ;
		case EBPF_MAGIC_ROOT : {
			if (!tv->is_tracer) break ;
			{	// register in dentry based map
				struct dentry* dentry = dentry_from_fd( t , req.fd )                       ; if (!dentry) break ;
				RootKey        rk     = { .dentry=dentry , .job_id=tv->job_id , .pad={0} } ;
				bpf_map_update_elem( &roots , &rk , &req.root_id , BPF_NOEXIST ) ;
			}
			bool is_repo = req.root_id>=EBPF_ROOT_ID_PLAIN ;
			if ( is_repo || req.root_id==EBPF_ROOT_ID_SUPER ) {                                   // not existing in root_strs means extern
				// register in path based map
				// /!\ : hash is simple but not robust (FNV), we must ensure clashes are handled pessimistically
				long       n1 = bpf_probe_read_user_str( tv->ev.data , PATH_MAX , name )     ; if (n1<=0                )                                           return 0 ;
				RootStrKey rk = { .hash=_hash(tv->ev.data) , .job_id=tv->job_id , .pad={0} } ; if (rk.hash==(uint64_t)-1) { _panic(tv,"root dir must be canonic") ; return 0 ; }
				//
				bpf_map_update_elem( &root_strs  , &rk , &is_repo , is_repo?0:BPF_NOEXIST ) ;     // repos and src dirs have priority of super-dirs
			}
		} break ;
		default : ;
	}
    return 0 ;
}

static __always_inline int _new_task( struct task_struct* parent , struct task_struct* child , bool chk_new_proc ) {
	Task* ptv = bpf_task_storage_get( &tasks , parent , 0/*value*/ , 0/*flags*/                     ) ; if (!ptv)                                              return 0 ;
	Task* ctv = bpf_task_storage_get( &tasks , child  , 0/*.    */ , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if (!ctv) { _panic(ptv,"cannot create thread entry") ; return 0 ; }
	//
	ctv->job_id           = ptv->job_id              ;
	ctv->is_tracer        = ptv->is_tracer           ;
	ctv->disable_cnt      = ptv->disable_cnt         ;
	ctv->ev.hdr.tid       = BPF_CORE_READ(child,pid) ; // pid field is actually the tid
	ctv->ev.hdr.kind      = EBPF_THREAD_START        ;
	ctv->ev.hdr.pid_in_ns = _get_pid_in_ns(child)    ;
	ctv->ev.hdr.tid_in_ns = _get_tid_in_ns(child)    ;
	if (
		chk_new_proc
	&&		BPF_CORE_READ( child  , group_leader,thread_pid )
		!=	BPF_CORE_READ( parent , group_leader,thread_pid )
	) ctv->ev.hdr.kind = EBPF_PROC_START ;
	//
	_emit( child , ctv ) ;
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
	if      (tv->is_job                           ) ev->hdr.kind = EBPF_JOB_END    ;
	else if (BPF_CORE_READ(t,signal,nr_threads)==1) ev->hdr.kind = EBPF_PROC_END   ;
	else                                            ev->hdr.kind = EBPF_THREAD_END ;
	_emit( t , tv ) ;
	if (tv->is_job) {
		uint32_t jid = tv->job_id ; if ( !jid || jid>=N_JOBS ) return 0 ; // XXX/ : clang seems to bug with 16-bits indexes
		_atomic_st( &job_infos[jid].overflow , true ) ;                   // ensure no further messages
	}
	return 0 ;
}

//
// path resolution
//

#define WALK_MAX_DEPTH 256 // the kernel puts no limit on depth, paths deeper than that make the job untrustable
typedef struct {
	RootKey    root_key     ;
	EbpfEvent* ev           ;
	int16_t    n_components ;
	uint16_t   off          ;
	bool       ok           ;
} WalkCtx ;
static long/*stop*/ _walk_step( uint64_t idx , void* ctx_ ) { //!                                                                                                     stop
	WalkCtx      * ctx     = ctx_                                                          ; if (!ctx             )                                            return true ;   // to please verifier
	EbpfEvent    * ev      = ctx->ev                                                       ; if (!ev              )                                            return true ;   // .
	struct dentry* dentry  = ctx->root_key.dentry                                          ; if (!dentry          )                                            return true ;   // .
	RootId*        root_id = bpf_map_lookup_elem( &roots , &ctx->root_key )                ; if (root_id          ) { ev->hdr.root_id = *root_id             ; return true ; }
	struct dentry* parent  = BPF_CORE_READ( dentry , d_parent )                            ; if (parent==dentry   ) { ev->hdr.root_id = EBPF_ROOT_ID_EXTERN  ; return true ; } // unknown mount root
	uint32_t       off     = ctx->off                                                      ; if (off     >PATH_MAX)                                            return true ;   // to please verifier
	const char*    name    = (const char*)BPF_CORE_READ( dentry , d_name.name )            ;
	uint32_t       len1    =              BPF_CORE_READ( dentry , d_name.len  )+1/*null*/  ; if (off+len1>PATH_MAX) { ev->hdr.root_id = EBPF_ROOT_ID_EXTERN  ; return true ; } // path too long
	long           n1      = bpf_probe_read_kernel_str( ev->data+off , NAME_MAX+1 , name ) ; if (n1!=len1         ) { ev->hdr.root_id = EBPF_ROOT_ID_UNKNOWN ; return true ; } // defensive
	//
	ctx->n_components++ ;
	ctx->off             = off + (uint32_t)n1 ; // terminating null acts as a component separator and will be converted into / (in reverse order) in userland
	ctx->root_key.dentry = parent             ;
	return false/*stop*/ ;
}
static __always_inline int16_t/*n_components*/ _walk( Task* tv , struct dentry* dentry ) {
	EbpfEvent* ev = &tv->ev ;
	ev->hdr.root_id = EBPF_ROOT_ID_UNKNOWN ;
	WalkCtx ctx = {
		.root_key = { .dentry=dentry , .job_id=tv->job_id , .pad={0} }
	,	.ev           = ev
	,	.n_components = 0
	,	.off          = tv->file_len            // append to whatever is already there
	} ;
	bpf_loop( WALK_MAX_DEPTH , _walk_step , &ctx , 0/*flags*/ ) ;
	if (ev->hdr.root_id==EBPF_ROOT_ID_UNKNOWN) { _panic(tv,"cannot walk") ; return -1/*n_components*/ ; }
	tv->file_len = ctx.off ;
	return ctx.n_components ;
}

// file info, as stat would report it
struct inode___v611 { __s64             i_mtime_sec ; uint32_t i_mtime_nsec ; } __attribute__((preserve_access_index)) ; // 6.11 <= linux
struct inode___v66  { struct timespec64 __i_mtime   ;                         } __attribute__((preserve_access_index)) ; // 6.6  <= linux < 6.11
struct inode___old  { struct timespec64 i_mtime     ;                         } __attribute__((preserve_access_index)) ; //         linux < 6.6
static __always_inline void _fill_ev_inode( EbpfEvent* ev , struct dentry* dentry ) {
	struct inode* inode = BPF_CORE_READ( dentry , d_inode ) ; if (!inode) { ev->hdr.file_kind = EBPF_FILE_NO_FILE ; return ; }
	umode_t       mode  = BPF_CORE_READ( inode  , i_mode  ) ;
	//
	if      ((mode&S_IFMT)==S_IFREG) ev->hdr.file_kind = mode&S_IXALL ? EBPF_FILE_EXE : EBPF_FILE_REG ;
	else if ((mode&S_IFMT)==S_IFLNK) ev->hdr.file_kind = EBPF_FILE_LNK                                ;
	else                             ev->hdr.file_kind = EBPF_FILE_NO_FILE                            ;
	ev->hdr.size = BPF_CORE_READ( inode , i_size ) ;
	#define HAS_FIELD(s,f)        bpf_core_field_exists(((struct s*)0)->f)
	#define MTIME_NS(in,sec,nsec) (uint64_t)BPF_CORE_READ(in,sec)*1000000000 + BPF_CORE_READ(in,nsec)
	if      (HAS_FIELD(inode___v611,i_mtime_sec     )) ev->hdr.mtime_ns = MTIME_NS( (struct inode___v611*)inode , i_mtime_sec      , i_mtime_nsec      ) ;
	else if (HAS_FIELD(inode___v66 ,__i_mtime.tv_sec)) ev->hdr.mtime_ns = MTIME_NS( (struct inode___v66 *)inode , __i_mtime.tv_sec , __i_mtime.tv_nsec ) ;
	else if (HAS_FIELD(inode___old ,i_mtime.tv_sec  )) ev->hdr.mtime_ns = MTIME_NS( (struct inode___old *)inode , i_mtime.tv_sec   , i_mtime.tv_nsec   ) ;
	#undef HAS_FIELD
	#undef MTIME_NS
}

//
// captures
//

static __noinline bool/*ok*/ _capture_solved( Task* tv , struct dentry* dentry ) {
	EbpfEvent* ev           = &tv->ev              ; if (ev->hdr.file_kind) return true /*ok*/ ; // already solved
	int16_t    n_components = _walk( tv , dentry ) ; if (n_components<0   ) return false/*. */ ; // panic
	//
	if (ev->hdr.root_id>=EBPF_ROOT_ID_PLAIN) _fill_ev_inode( ev , dentry ) ;
	return true/*ok*/ ;
}

// capture a path that could not be resolved : the path as provided by the user and the dir it is relative to
static __noinline bool/*ok*/ _capture_user( struct task_struct* t , Task* tv , int dfd,const char* path , bool kernel ) {
	EbpfEvent* ev  = &tv->ev         ; if ( ev->hdr.file_kind   ) return true /*ok*/ ;                                    // file is already known
	uint32_t   jid = tv->job_id      ; if ( !jid || jid>=N_JOBS ) return false/*. */ ;                                    // XXX/ : clang seems to bug with 16-bits indexes
	JobInfo*   ji  = job_infos + jid ;
	//
	if (tv->last_lnk_loc==LOC_EXTERN) { ev->hdr.root_id = EBPF_ROOT_ID_UNKNOWN ; return true/*ok*/ ; }
	//
	ev->hdr.file_kind = EBPF_FILE_USER ;
	//
	long n1 = 0 ;                                                                                                         // includes terminating null
	if (!path ) { ev->hdr.root_id = EBPF_ROOT_ID_UNKNOWN                                    ; return true/*ok*/ ; }
	if (kernel)   n1              = bpf_probe_read_kernel_str( ev->data , PATH_MAX , path ) ;
	else          n1              = bpf_probe_read_user_str  ( ev->data , PATH_MAX , path ) ;
	if (n1<=0) {
		if ( kernel || (ev->hdr.flags&EBPF_F_OK) ) { _panic(tv,"cannot access path") ; return false/*ok*/ ; }             // page is not resident any more, cannot do anything but report it
		return true/*ok*/ ;                                                                                               // no access if path was never accessible
	}
	if (ev->data[0]=='/') {                                                                                               // absolute path, no dfd interpretation
		if (tv->last_lnk_loc==LOC_NONE) {
			Loc l = _locate(ev->data,jid) ;
			if ( l==LOC_EXTERN || l==LOC_SUPER ) { ev->hdr.kind = EBPF_NONE ; return true/*ok*/ ; }
		}
		ev->hdr.root_id = EBPF_ROOT_ID_ABS ;
		tv->file_len    = (uint16_t)n1-1   ;                                                                              // exclude terminating null
		return true ;
	} else {
		// relative path, interpret dfd
		struct dentry* dentry = dentry_from_fd( t , dfd ) ; if (!dentry) return true/*ok*/ ;                              // dfd is closed
		tv->file_len = (uint16_t)n1 ;                                                                                     // keep terminating null which means a /
		return _walk( tv , dentry )>=0 ;
	}
}

static __always_inline bool/*ok*/ _emit_user( struct task_struct* t , Task* tv , int dfd,const char* path , bool kernel ) {
	if (!_capture_user(t,tv,dfd,path,kernel)) return false/*ok*/ ;
	//
	EbpfEvent* ev = &tv->ev ;
	if ( ev->hdr.flags & EBPF_F_SEEN_LNK ) ev->hdr.flags |= EBPF_F_STOPPED ; // real path reconstruction is not trivial
	return _emit( t , tv ) ;
}
static __always_inline bool/*ok*/ _emit_solved( struct task_struct* t , Task* tv , struct dentry* dentry ) {
	return _capture_solved(tv,dentry) && _emit( t , tv ) ;
}

// symlinks traversed during path resolution
// this is called for each traversed symlink, whether in rcu-walk or in ref-walk mode (a resolution may be restarted, in which case links are reported twice)
// the target is available from the inode for short links (those stored in the inode), else it is retrieved by the file system and consumer will have to read it
// note : vfs_get_link is not used by path resolution, which directly calls the file system
SEC("fentry/security_inode_follow_link")
int BPF_PROG( on_follow_link , struct dentry* dentry , struct inode* inode , bool rcu ) {
	struct task_struct* t            = bpf_get_current_task_btf() ;
	Task              * tv           = _capturing1(t)             ; if (!tv           ) return 0 ;
	int16_t             n_components = _walk( tv , dentry )       ; if (n_components<0) return 0 ;
	EbpfEvent         * ev           = &tv->ev                    ;
	RootId              root_id      = ev->hdr.root_id            ;
	//
	EbpfFlags flags = ev->hdr.flags & EBPF_F_SEEN_PROC ; // keep some flags until end of syscall
	if (root_id>=EBPF_ROOT_ID_PLAIN) {
		ev->hdr.kind  = EBPF_PATH_RES            ;
		ev->hdr.flags = EBPF_F_DEP_LNK|EBPF_F_OK ;
		_fill_ev_inode( ev , dentry ) ;
		if (!_emit( t , tv )) return 0 ;
	} else {
		if (root_id==EBPF_ROOT_ID_PROC) flags |= EBPF_F_SEEN_PROC ;
		_reset_ev(tv) ;
	}
	flags         |= EBPF_F_SEEN_LNK ;
	ev->hdr.flags |= flags           ;
	//
	const char* target     = BPF_CORE_READ( inode , i_link )                       ; if (!target                          ) { tv->last_lnk_loc = LOC_UNKNOWN                  ; return 0 ; }
	char*       data       = ev->data                                              ;
	long        n1         = bpf_probe_read_kernel_str( data , PATH_MAX , target ) ; if (n1<=0                            ) { tv->last_lnk_loc = LOC_UNKNOWN                  ; return 0 ; }
	/**/                                                                             if (data[0]=='/'                     ) { tv->last_lnk_loc = _locate( data , tv->job_id ) ; return 0 ; }
	uint16_t    n_dot_dots = _n_dot_dots(ev->data)                                 ; if (n_dot_dots>(uint16_t)n_components) { tv->last_lnk_loc = LOC_UNKNOWN                  ; return 0 ; }
	/**/                                                                             if (root_id>=EBPF_ROOT_ID_PLAIN      ) { tv->last_lnk_loc = LOC_REPO                     ; return 0 ; }
	/**/                                                                             if (root_id==EBPF_ROOT_ID_SUPER      ) { tv->last_lnk_loc = LOC_SUPER                    ; return 0 ; }
	/**/                                                                                                                      tv->last_lnk_loc = LOC_EXTERN                   ; return 0 ;
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
	ev->hdr.kind = EBPF_READ ;
	if ( _open_no_follow(flags)) ev->hdr.flags |= EBPF_F_NO_FOLLOW ;
	if (!_open_dir      (flags)) {
		if (_open_do_write(flags)) {
			/**/                      ev->hdr.kind   = EBPF_WRITE      ;
			if (_open_do_read(flags)) ev->hdr.flags |= EBPF_F_DEP_REG  ;
			if (_open_do_stat(flags)) ev->hdr.flags |= EBPF_F_DEP_STAT ;
		} else {
			ev->hdr.flags |= EBPF_F_DEP_ALL ; // a fstat can be performed on returned fd
		}
	}
	if (ok) { ev->hdr.flags |= EBPF_F_OK ; _emit_solved( t , tv , BPF_CORE_READ(ret,f_path.dentry)                  ) ; }
	else                                   _emit_user  ( t , tv , dfd,BPF_CORE_READ(pathname,name) , true/*kernel*/ ) ;
	return 0 ;
}

static __always_inline int _do_nod1(struct dentry* dentry) {
	Task * tv = _capturing2() ; if (!tv) return 0 ;
	_capture_solved( tv , dentry ) ;
	return 0 ;
}
static __always_inline int _do_nod2( struct dentry* old_dentry , struct dentry* new_dentry ) {
	struct task_struct* t  = bpf_get_current_task_btf() ;
	Task              * tv = _capturing1(t)             ; if (!tv) return 0 ;
	EbpfEvent         * ev = &tv->ev                    ;
	_reset_ev(tv) ;                                       // XXX : improve by using source resolution rather than redoing it
	ev->hdr.kind  = EBPF_DST       ;
	ev->hdr.flags = EBPF_F_DEP_ALL ;
	//
	if (!_capture_solved(tv,new_dentry) ) return 0 ;
	if (ev->hdr.file_kind==EBPF_FILE_DIR) ev->hdr.flags |= EBPF_F_STOPPED ;
	if (!_emit(t,tv)                    ) return 0 ;
	//
	_capture_solved( tv , old_dentry ) ;
	return 0 ;
}

SEC("fentry/security_path_mkdir"  ) int BPF_PROG( on_path_mkdir   , struct path* d ,struct dentry* e                                               ) { return _do_nod1( e       ) ; }
SEC("fentry/security_path_mknod"  ) int BPF_PROG( on_path_mknod   , struct path* d ,struct dentry* e  , mode_t m                                   ) { return _do_nod1( e       ) ; }
SEC("fentry/security_path_symlink") int BPF_PROG( on_path_symlink , struct path* d ,struct dentry* e                                               ) { return _do_nod1( e       ) ; }
SEC("fentry/security_path_unlink" ) int BPF_PROG( on_path_unlink  , struct path* d ,struct dentry* e                                               ) { return _do_nod1( e       ) ; }
SEC("fentry/security_path_rmdir"  ) int BPF_PROG( on_path_rmdir   , struct path* d ,struct dentry* e                                               ) { return _do_nod1( e       ) ; }
SEC("fentry/security_path_link"   ) int BPF_PROG( on_path_link    ,                 struct dentry* oe , struct path* nd,struct dentry* ne          ) { return _do_nod2( oe , ne ) ; }
SEC("fentry/security_path_rename" ) int BPF_PROG( on_path_rename  , struct path* od,struct dentry* oe , struct path* nd,struct dentry* ne , uint f ) { return _do_nod2( oe , ne ) ; }

//
// end of syscalls
//

static __always_inline uint64_t get_arg( uint64_t args[8] , int i , uint64_t dflt ) {
	if (i==EBPF_SCE_NO_ARG) return dflt        ;
	else                    return args[i&0x7] ;
}

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
	/**/                                                     if ( nr<0 || nr>=N_SYSCALLS ) return 0 ;       // x32 has no effect on data structures we are sensitive to
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
	uint8_t     sc_kind   = EBPF_SCE_KIND (descr)    ; if (_is_retry(ret)) sc_kind = EBPF_SC_NONE ;         // error is external, no real dependency nor action
	uint8_t     arg_dfd1  = EBPF_SCE_DFD1 (descr)    ;
	uint8_t     arg_path1 = EBPF_SCE_PATH1(descr)    ;
	uint8_t     arg_dfd2  = EBPF_SCE_DFD2 (descr)    ;
	uint8_t     arg_path2 = EBPF_SCE_PATH2(descr)    ;
	uint8_t     arg_mod   = EBPF_SCE_MOD  (descr)    ;
	uint8_t     arg_flgs  = EBPF_SCE_FLAGS(descr)    ;
	uint8_t     no_follow = EBPF_SCE_NF   (descr)    ;
	uint64_t    args[8]   = {0,0,0,0,0,0,0,AT_FDCWD} ;                                                      // AT_FDCWD simplifies cases where there is no dfd
	#if defined(__TARGET_ARCH_x86)
		if (is_32) { args[0]=regs->bx      ; args[1]=regs->cx      ; args[2]=regs->dx      ; args[3]=regs->si      ; args[4]=regs->di      ; args[5]=regs->bp      ; }
		else       { args[0]=regs->di      ; args[1]=regs->si      ; args[2]=regs->dx      ; args[3]=regs->r10     ; args[4]=regs->r8      ; args[5]=regs->r9      ; }
	#elif defined(__TARGET_ARCH_arm64)
		/**/         args[0]=regs->orig_x0 ; args[1]=regs->regs[1] ; args[2]=regs->regs[2] ; args[3]=regs->regs[3] ; args[4]=regs->regs[4] ; args[5]=regs->regs[5] ;
	#else
		#error "unsupported architecture"
	#endif
	if ( sc_kind>=EBPF_SC_HAS_DST && !ev->hdr.file_kind ) {
		ev->hdr.kind = EBPF_DST ;
		if (!_emit_user( t , tv , (int)get_arg(args,arg_dfd2,AT_FDCWD) , (const char*)args[arg_path2&7] , false/*kernel*/ )) return 0 ;
	}
	switch (sc_kind) {
		case EBPF_SC_OPEN     :                                                                             // already handled in do_filp_open
		case EBPF_SC_PATH     :                                                                  return 0 ;
		case EBPF_SC_READDIR  : ev->hdr.kind = EBPF_READDIR ;                                    break    ;
		case EBPF_SC_READLINK : ev->hdr.kind = EBPF_READ    ; ev->hdr.flags |= EBPF_F_DEP_LNK  ; break    ;
		case EBPF_SC_SENSE    : ev->hdr.kind = EBPF_READ    ; ev->hdr.flags |= EBPF_F_DEP_STAT ; break    ;
		case EBPF_SC_STAT     : ev->hdr.kind = EBPF_READ    ; ev->hdr.flags |= EBPF_F_DEP_ALL  ; break    ;
		case EBPF_SC_UPDATE   : ev->hdr.kind = EBPF_WRITE   ; ev->hdr.flags |= EBPF_F_DEP_STAT ; break    ;
		case EBPF_SC_CREATE :
			switch (get_arg(args,arg_mod,S_IFLNK)) {                                                        // default value must generate a (any kind of) file and in practice is symlink
				case S_IFREG :
				case S_IFLNK : ev->hdr.kind = EBPF_WRITE ; break ;
				default      : ev->hdr.kind = EBPF_READ  ;                                                  // no managed file is created
			}
			ev->hdr.flags |= EBPF_F_DEP_STAT ;                                                              // sensitive to file existence (i.e. must not pre-exist)
		break ;
		case EBPF_SC_UNLINK :
			if ( get_arg(args,arg_flgs,0/*dflt*/) & AT_REMOVEDIR ) return 0 ;
			ev->hdr.kind   = EBPF_UNLINK     ;
			ev->hdr.flags |= EBPF_F_DEP_STAT ;
		break ;
		case EBPF_SC_LINK :
			/**/                            ev->hdr.kind   = EBPF_LINK      ;
			if ( !ev->hdr.file_kind && ok ) ev->hdr.flags |= EBPF_F_STOPPED ;                               // in case it is a dir
		break ;
		case EBPF_SC_RENAME :
			if ( get_arg(args,arg_flgs,0/*dflt*/) & RENAME_EXCHANGE ) ev->hdr.kind = EBPF_EXCHANGE ;
			else                                                      ev->hdr.kind = EBPF_RENAME   ;
		break ;
		case EBPF_SC_FORBIDDEN : _panic(tv,"forbidden syscall") ; return 0 ;
		default                : _panic(tv,"bad syscall kind" ) ; return 0 ;
	}
	switch (no_follow) {
		 case 0                     :                                                                                                      break ;
		 case 1                     :                                                                  ev->hdr.flags |= EBPF_F_NO_FOLLOW ; break ;
		 case EBPF_SC_MAY_NO_FOLLOW : if (   get_arg(args,arg_flgs,0/*dflt*/) & AT_SYMLINK_NOFOLLOW  ) ev->hdr.flags |= EBPF_F_NO_FOLLOW ; break ;
		 case EBPF_SC_MAY_FOLLOW    : if (!( get_arg(args,arg_flgs,0/*dflt*/) & AT_SYMLINK_FOLLOW   )) ev->hdr.flags |= EBPF_F_NO_FOLLOW ; break ;
	}
	if (ok) ev->hdr.flags |= EBPF_F_OK ;
	_emit_user( t , tv , (int)get_arg(args,arg_dfd1,AT_FDCWD) , (const char*)get_arg(args,arg_path1,0/*dflt*/) , false/*kernel*/ ) ;
	return 0 ;
}

//
// io_uring : operations are executed either by the thread that submits them or by a worker, and their completion may be reported from yet another context
// so they are closed at the exit of their handler, which is the only place where we know to be in the context where captures were done
// args are laid out as for the equivalent syscall
//

static __always_inline Task* _capturing_io( struct task_struct* t , int ret ) {
	Task* tv = _capturing1(t) ; if (!tv           )                   return 0 ;
	/**/                        if (_is_retry(ret)) { _reset_ev(tv) ; return 0 ; } // request will be re-issued (typically from a worker)
	return tv ;
}

#define HANDLE_IO1( io_name , io_typ , kind_expr , flags_expr , dfd_field,file_field , no_follow_expr , active_expr ) \
	SEC( "fexit/"#io_name )                                                                       \
	int BPF_PROG( on_##io_name , struct io_kiocb* req_ , uint flags , int ret ) {                 \
		struct task_struct* t      = bpf_get_current_task_btf()             ;                     \
		Task              * tv     = _capturing_io(t,ret)                   ; if (!tv) return 0 ; \
		EbpfEvent         * ev     = &tv->ev                                ;                     \
		struct io_typ     * req    = (struct io_typ*)req_                   ;                     \
		int                 dfd    = BPF_CORE_READ( req , dfd_field       ) ;                     \
		const char*         path   = BPF_CORE_READ( req , file_field,name ) ;                     \
		bool                active = active_expr                            ;                     \
		if (active        ) { ev->hdr.kind = kind_expr ; ev->hdr.flags |= flags_expr       ; }    \
		else                  ev->hdr.kind = EBPF_READ ;                                          \
		if (no_follow_expr)                              ev->hdr.flags |= EBPF_F_NO_FOLLOW ;      \
		if (_is_ok(ret)   )                              ev->hdr.flags |= EBPF_F_OK        ;      \
		_emit_user( t , tv , dfd,path , true/*kernel*/ ) ;                                        \
		return 0 ;                                                                                \
	}

#define HANDLE_IO2( io_name , io_typ , kind_expr , no_follow_expr , maybe_dir_expr ) \
	SEC( "fexit/"#io_name )                                                                   \
	int BPF_PROG( on_##io_name , struct io_kiocb* req_ , uint flags , int ret ) {             \
		struct task_struct* t     = bpf_get_current_task_btf()          ;                     \
		Task              * tv    = _capturing_io(t,ret)                ; if (!tv) return 0 ; \
		EbpfEvent         * ev    = &tv->ev                             ;                     \
		struct io_typ     * req   = (struct io_typ*)req_                ;                     \
		int                 dfd1  = BPF_CORE_READ( req , old_dfd      ) ;                     \
		const char*         path1 = BPF_CORE_READ( req , oldpath,name ) ;                     \
		int                 dfd2  = BPF_CORE_READ( req , new_dfd      ) ;                     \
		const char*         path2 = BPF_CORE_READ( req , newpath,name ) ;                     \
		if (!ev->hdr.file_kind) {                                                             \
			ev->hdr.kind = EBPF_DST ;                                                         \
			if (!_emit_user( t , tv , dfd2,path2 , true/*kernel*/ )) return 0 ;               \
		}                                                                                     \
		/**/                ev->hdr.kind   = kind_expr        ;                               \
		if (no_follow_expr) ev->hdr.flags |= EBPF_F_NO_FOLLOW ;                               \
		if (_is_ok(ret)   ) ev->hdr.flags |= EBPF_F_OK        ;                               \
		if (maybe_dir_expr) ev->hdr.flags |= EBPF_F_STOPPED   ;                               \
		_emit_user( t , tv , dfd1,path1 , true/*kernel*/ ) ;                                  \
		return 0 ;                                                                            \
	}

HANDLE_IO1( io_mkdirat  , io_mkdir  , EBPF_READ   , 0/*flags*/      , dfd    ,filename , true/*no_follow*/                            , true/*active*/                           )
HANDLE_IO1( io_statx    , io_statx  , EBPF_READ   , EBPF_F_DEP_ALL  , dfd    ,filename , BPF_CORE_READ(req,flags)&AT_SYMLINK_NOFOLLOW , _statx_is_stat(BPF_CORE_READ(req,mask))  )
HANDLE_IO1( io_symlinkat, io_link   , EBPF_WRITE  , EBPF_F_DEP_STAT , new_dfd,newpath  , true/*no_follow*/                            , true/*active*/                           )
HANDLE_IO1( io_unlinkat , io_unlink , EBPF_UNLINK , EBPF_F_DEP_STAT , dfd    ,filename , true/*.        */                            , !(BPF_CORE_READ(req,flags)&AT_REMOVEDIR) )

HANDLE_IO2( io_linkat   , io_link   , EBPF_LINK                                         , !(BPF_CORE_READ(req,flags)&AT_SYMLINK_FOLLOW) , false/*maybe_dir_expr*/ )
HANDLE_IO2( io_renameat , io_rename , (flags&RENAME_EXCHANGE)?EBPF_RENAME:EBPF_EXCHANGE , true/*no_follow*/                             , true/*.              */ )
