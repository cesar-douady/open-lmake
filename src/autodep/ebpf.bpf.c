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

#include "ebpf_event.h"

char LICENSE[] SEC("license") = "GPL" ; // required to use GPL-only helpers

#define AT_FDCWD_      (-100)
#define NSYSCALLS      EBPF_NSYSCALLS
#define MAX_JOBS       4096
#define PF_IO_WORKER_  0x00000010 // task is an io_uring worker
#define FMODE_CREATED_ 0x100000   // file has been created by open
#define EAGAIN_        11
#define ERESTARTSYS_   512        // from 512 to 516, the syscall will be restarted
#define ERESTART_MAX_  516

#if HAS_32
	#define TS_COMPAT_       0x0002     // 32-bit syscall active (thread_info.status)
	#define X32_SYSCALL_BIT_ 0x40000000 // syscall is done with the x32 ABI
#endif
#if defined(__TARGET_ARCH_x86)
	#define SC_NR(regs) ((long)(regs)->orig_ax  )
#elif defined(__TARGET_ARCH_arm64)
	#define SC_NR(regs) ((long)(regs)->syscallno)
#else
	#error "unsupported architecture"
#endif

//
// maps
//

struct {                                        // job_id -> per-job configuration ; filled by the daemon at job registration
	__uint( type        , BPF_MAP_TYPE_HASH ) ;
	__uint( max_entries , MAX_JOBS          ) ;
	__type( key         , __u32             ) ;
	__type( value       , struct JobCfg     ) ;
} job_cfg SEC(".maps") ;

struct InnerRb {                                   // template describing the per-job ring buffers stored in job_rbs
	__uint( type        , BPF_MAP_TYPE_RINGBUF ) ;
	__uint( max_entries , 4<<20                ) ; // actual size is decided by the daemon
} inner_rb SEC(".maps") ;

struct {                                                 // job_id -> that job's ring buffer, so a consumer never sees another job's (possibly another user's) accesses
	__uint ( type        , BPF_MAP_TYPE_HASH_OF_MAPS ) ;
	__uint ( max_entries , MAX_JOBS                  ) ;
	__type ( key         , __u32                     ) ;
	__array( values      , struct InnerRb            ) ;
} job_rbs SEC(".maps") ;

struct {
	__uint( type        , BPF_MAP_TYPE_HASH       ) ;
	__uint( max_entries , MAX_JOBS*EBPF_MAX_ROOTS ) ;
	__type( key         , struct RootKey          ) ;
	__type( value       , __u32                   ) ; // index of the root
} roots SEC(".maps") ;

struct {
	__uint( type        , BPF_MAP_TYPE_ARRAY ) ;
	__uint( max_entries , 1                  ) ;
	__type( key         , __u32              ) ;
	__type( value       , struct RootReg     ) ;
} root_reg SEC(".maps") ;

struct {                                           // syscalls[is32*NSYSCALLS+nr] is the EBPF_SC descriptor of syscall nr, 0 if syscall is not tracked
	__uint( type        , BPF_MAP_TYPE_ARRAY   ) ; // it is the same for all jobs and is filled by user space from SyscallDescr::s_tab
	__uint( max_entries , (1+HAS_32)*NSYSCALLS ) ;
	__type( key         , __u32                ) ;
	__type( value       , __u32                ) ;
} syscalls SEC(".maps") ;

#define SEEN_OPEN 0x01 // an open hook has been called during the current syscall, even if nothing was reported
#define SEEN_RES  0x02 // a path has been resolved      during the current syscall, even if nothing was reported
#define SEEN_PAIR 0x04 // a pair of paths has been resolved (rename)
#define SEEN_RAW  0x08 // an open has failed during the current syscall, its path has been dealt with
#define SEEN_FAIL 0x10 // an open has failed during the current syscall, its path is an arg of the syscall and must be dealt with at exit

struct Rep {                                          // what identifies a failed syscall that designates a single path, all what is reported is derived from it
	__u64 path[8] ;                                   // path as provided (null padded), longer paths are not candidate
	__u64 args[6] ;
	__u64 dir     ;                                   // dentry of the dir path is relative to
	__s64 rc      ;
	__u32 nr      ;
	__u32 is32    ;
} ;
struct Task {
	__u32            job_id         ;                 // job this task belongs to, 0 if none
	__u32            tid            ;                 // ids in the client's pid namespace
	__u32            pid            ;
	__u32            pending        ;                 // number of captures emitted since last End
	__u8             deps_in_system ;                 // cached from the job config
	__u8             enabled        ;                 // mirrors the autodep enable bit (toggled through the backdoor), inherited by children
	__u8             suspend        ;                 // if set, this thread is processing a backdoor call by itself : do not record its accesses
	__u8             seen           ;
	__u8             cur_kind       ;                 // EBPF_SCK_* of the syscall being executed, as of last call to _capturing
	__u8             rep_valid      ;                 // rep[0] describes the last syscall reported by this thread
	struct Rep       rep[2]         ;                 // last failed syscall that has been reported and candidate, so as not to report repeated failures
	__u64            io_req         ;                 // last io_uring request closed by io_openat2, so that io_openat does not close it again
	volatile __u64   ptr[2]         ;                 // global functions only accept scalars, kernel pointers passed to them go through here to make the verifier forget their type
	struct EbpfEvent ev             ;                 // event being built, off-stack as it is large
} ;
struct {                                              // state of the traced threads
	__uint( type      , BPF_MAP_TYPE_TASK_STORAGE ) ;
	__uint( map_flags , BPF_F_NO_PREALLOC         ) ;
	__type( key       , int                       ) ;
	__type( value     , struct Task               ) ;
} tasks SEC(".maps") ;

//
// utils
//

static __always_inline uid_t _get_current_euid() {
	struct task_struct* task = (struct task_struct *)bpf_get_current_task() ;
	return BPF_CORE_READ( task , cred , euid.val ) ;
}

//
// emission
//

static __always_inline void _lost(struct Task* tv) {
	__u32          jid = tv->job_id                             ;
	struct JobCfg* jc  = bpf_map_lookup_elem( &job_cfg , &jid ) ; if (!jc) return ;
	__sync_fetch_and_add( &jc->lost , 1 ) ;
}

// push tv->ev to the ring buffer of its job, if the job is gone, the event is dropped
static __always_inline void _emit( struct Task* tv , __u32 tid , __u32 pid , __u32 sz ) {
	__u32          jid = tv->job_id                           ;
	struct JobCfg* jc  = bpf_map_lookup_elem( &job_cfg , &jid ) ; if (!jc) return ;
	void*          rb  = bpf_map_lookup_elem( &job_rbs , &jid ) ; if (!rb) return ;
	tv->ev.seq = __sync_fetch_and_add( &jc->seq , 1 ) ;
	tv->ev.tid = tid                                  ;
	tv->ev.pid = pid                                  ;
	if (sz>sizeof(struct EbpfEvent)) sz = sizeof(struct EbpfEvent) ;
	if (bpf_ringbuf_output(rb,&tv->ev,sz,0/*flags*/)!=0) __sync_fetch_and_add( &jc->lost , 1 ) ; // ring buffer is full, consumer will know through seq and will ask the daemon in the end
}

static __always_inline void _emit_capture( struct Task* tv , __u32 len ) {
	_emit( tv , tv->tid , tv->pid , EBPF_HDR_SZ+(len&(EBPF_DATA_SZ-1)) ) ;
	tv->pending++     ;
	tv->rep_valid = 0 ; // what is reported is no more the repetition of anything
}

//
// path resolution
//

// walk up from (dentry,vfsmount), storing components (leaf first, each followed by a null) into dst :
// - if a root of the job is met, stop there : path is relative to this root
// - else, if want_abs, stop at the root of the current task (as d_path does) : path is absolute as seen by the job
// - else path is not of interest
// vfsmount may be null if unknown, in which case mount points cannot be crossed
// what matters is where the file physically lies : a file seen through a bind mount of a sub-dir (this is what views and tmp_view are) is first looked for by walking up
// its physical dirs, and only if no root is met this way, through the mount point
// most accesses are of no interest, so the walk is first done without copying names, which is half of its cost, then done again once it is known to succeed
// /!\ this is a global (not inlined) function so that the verifier checks it once, on its own (when inlined at each call site, the verifier budget is exceeded)
// hence its args must be verifiable on their own : a pointer to a fixed size buffer (nullable) and scalars
#define WALK_MAX_DEPTH 256                                                                               // the kernel puts no limit on depth, paths deeper than that make the job untrustable
#define WALK_MAX_LEN   (EBPF_PATH_MAX-256)                                                               // room is left for a last component
#define WALK_OK        (1u<<31)
#define WALK_TRUNC     (1u<<30)                                                                          // path is too deep or too long, we do not know whether it is of interest
#define WALK_ROOT(r)   (((r)>>16)&0xff)
#define WALK_LEN(r)    ( (r)     &0xffff)
struct WalkData {
	__u8 data[EBPF_DATA_SZ] ;
} ;
struct WalkCtx {
	struct WalkData* dst          ;
	struct dentry  * dentry       ;
	struct mount   * mnt          ;                                                                      // null if unknown
	struct dentry  * troot        ;                                                                      // root of the task, null if not looked for
	struct vfsmount* troot_mnt    ;
	struct dentry  * saved_dentry ;                                                                      // where to resume from if the physical walk meets no root
	struct mount   * saved_mnt    ;
	__u64            mnt_off      ;
	struct RootKey   rk           ;
	__u32            saved_off    ;
	__u32            saved        ;                                                                      // 0 : nothing saved, 1 : walking physically, 2 : resumed
	__u32            copy         ;                                                                      // if 0, only look for the root
	__u32            off          ;
	__u32            res          ;
} ;
// one step up, return 1 when done
// /!\ the loop is done with bpf_loop so that the verifier checks the body only once (an unrolled loop exceeds its budget)
static long/*stop*/ _walk_step( __u64 idx , void* ctx ) {
	struct WalkCtx * c      = ctx       ; if (idx==WALK_MAX_DEPTH-1) { c->res=WALK_TRUNC ; return 1/*stop*/ ; }
	struct dentry  * dentry = c->dentry ; if (!dentry              )                       return 1/*stop*/ ;
	struct WalkData* dst    = c->dst    ; if (!dst                 )                       return 1/*stop*/ ;
	struct mount   * mnt    = c->mnt    ;
	//
	c->rk.dentry = (__u64)dentry ;
	__u32* r = bpf_map_lookup_elem( &roots , &c->rk ) ;
	if (r) {
		__u32 ri = *r&0xff ;
		if      (ri!=EBPF_ROOT_EXCLUDE) { c->res = WALK_OK | ri<<16 | c->off ; return 1/*stop*/ ; }      // root found, most common case for a path of interest
		else if (!c->troot            ) {                                      return 1/*stop*/ ; }      // not of interest, but go on if the absolute path is looked for
	}
	if ( mnt && dentry==c->troot && (struct vfsmount*)((char*)mnt+c->mnt_off)==c->troot_mnt ) goto Abs ; // root of the task reached
	struct dentry* parent = BPF_CORE_READ( dentry , d_parent ) ;
	if (mnt) {
		struct dentry* mnt_root = BPF_CORE_READ( mnt , mnt.mnt_root ) ;
		if (dentry==mnt_root) {                                                                          // reached the root of the current mount
			struct mount* mp = BPF_CORE_READ( mnt , mnt_parent ) ; if (mp==mnt) goto Abs ;               // global root reached (task root is not reachable)
			if ( dentry!=parent && !c->troot && !c->saved ) {                                            // a sub-dir is mounted : first look where it physically lies
				c->saved_dentry = BPF_CORE_READ( mnt , mnt_mountpoint ) ;
				c->saved_mnt    = mp                                    ;
				c->saved_off    = c->off                                ;
				c->saved        = 1                                     ;
				c->mnt          = 0                                     ;
			} else {                                                                                     // cross to the parent mount
				c->dentry = BPF_CORE_READ( mnt , mnt_mountpoint ) ;
				c->mnt    = mp                                    ;
				return 0/*stop*/ ;
			}
		}
	}
	if (dentry==parent) {                                                                                // fs root reached without mount info, or detached dentry
		if (c->saved!=1) return 1/*stop*/ ;
		c->dentry = c->saved_dentry ;                                                                    // no root met physically, go through the mount point
		c->mnt    = c->saved_mnt    ;
		c->off    = c->saved_off    ;
		c->saved  = 2               ;
		return 0/*stop*/ ;
	}
	if (c->copy) {
		const char* name = (const char*)BPF_CORE_READ( dentry , d_name.name )                ; if (!name            )                         return 1/*stop*/ ;
		__u32       off  = c->off                                                            ; if (off>=WALK_MAX_LEN) { c->res = WALK_TRUNC ; return 1/*stop*/ ; }
		long        n    = bpf_probe_read_kernel_str( &dst->data[off] , 256/*size*/ , name ) ; if (n<=0             )                         return 1/*stop*/ ;
		c->off = off + (__u32)n ;                                                                                                                                // n includes the terminating null
	}
	c->dentry = parent ;
	return 0/*stop*/ ;
Abs :
	if (c->troot) c->res = WALK_OK | EBPF_ROOT_ABS<<16 | c->off ;
	return 1/*stop*/ ;
}
__noinline __u32 _walk( struct WalkData* dst , __u64 dentry , __u64 vmnt , __u32 job_id , __u32 want_abs ) {
	if (!dst) return 0 ;
	__u64          mnt_off = bpf_core_field_offset(struct mount,mnt) ;
	struct WalkCtx c       = {
		.dst          = dst
	,	.dentry       = (struct dentry*)dentry
	,	.mnt          = vmnt ? (struct mount*)(vmnt-mnt_off) : 0
	,	.troot        = 0
	,	.troot_mnt    = 0
	,	.saved_dentry = 0
	,	.saved_mnt    = 0
	,	.mnt_off      = mnt_off
	,	.rk           = { .dentry=0 , .job_id=job_id , .pad=0 }
	,	.saved_off    = 0
	,	.saved        = 0
	,	.copy         = want_abs // when the absolute path is looked for, the walk always succeeds
	,	.off          = 0
	,	.res          = 0
	} ;
	if (want_abs) {
		struct task_struct* t  = bpf_get_current_task_btf() ;
		struct fs_struct*   fs = BPF_CORE_READ( t , fs )    ; if (!fs) return 0 ;
		c.troot     = BPF_CORE_READ( fs , root.dentry ) ;
		c.troot_mnt = BPF_CORE_READ( fs , root.mnt    ) ;
	}
	bpf_loop( WALK_MAX_DEPTH , _walk_step , &c , 0/*flags*/ ) ;
	if ( c.copy || !(c.res&WALK_OK) ) return c.res ;
	// a root has been found, do it again to get the names
	c.dentry = (struct dentry*)dentry                   ;
	c.mnt    = vmnt ? (struct mount*)(vmnt-mnt_off) : 0 ;
	c.saved  = 0                                        ;
	c.copy   = 1                                        ;
	c.off    = 0                                        ;
	c.res    = 0                                        ;
	bpf_loop( WALK_MAX_DEPTH , _walk_step , &c , 0/*flags*/ ) ;
	return c.res ;
}

//
// in-kernel "simple path" filter for paths that could not be resolved : drop accesses to system files before they reach the ring buffer
// /!\ must stay conservative : only drop what user-space Record::s_is_simple would also drop (else a real dep would be lost), when unsure, keep the event
//

struct IsSimpleCtx {
	__u8 const* d         ;
	__u32       i         ;
	__u32       depth     ;
	__u8        dots_seen ; // d is empty or starts with /
	__u8        is_simple ;

} ;
static long/*stop*/ _is_simple_dot_dot_step( __u64 idx , void* ctx ) {
	struct IsSimpleCtx* c = ctx  ;
	__u32               i = c->i ; c->i++ ;
	__u8                v = 0    ; if (i<256) v = c->d[i] ;                                                  // i<256 is always holds, but this is necessary for the verifier
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
static __always_inline __u32 _is_simple_pfx( const __u8* d , const char* p ) {
	for( __u32 i=0 ; i<20 ; i++ ) {                                                                          // 20 : longer than prefix length
		__u8 d_ = d[i] ;
		__u8 p_ = p[i] ;
		if      (!p_   ) return d_==0 || d_=='/' ? i : 0 ;
		else if (d_!=p_) return 0                        ;
	}
	return 0 ;                                                                                               // defensive programming
}
static __always_inline bool _is_simple( const __u8* d , __u32 len , int deps_in_system ) {
	if (deps_in_system) return false ;                                                                       // if deps may lie in system dirs  : nothing is simple
	if (len<4         ) return false ;                                                                       // 4 is the min prefix len
	if (len>256       ) return false ;                                                                       // max system file length is about 173, so 256 is comfortable, even with a few ..
	if (*d!='/'       ) return false ;                                                                       // relative : not simple
	d += 1/* / */ ;
	__u32 pfx_len = 0 ;
	switch (*d) {
		case 'b' : pfx_len=_is_simple_pfx(d,"bin"   ) ; if (pfx_len) break ; return false ;                  // not in a system dir
		case 'e' : pfx_len=_is_simple_pfx(d,"etc"   ) ; if (pfx_len) break ; return false ;                  // .
		case 'l' : pfx_len=_is_simple_pfx(d,"lib"   ) ; if (pfx_len) break ;
		/**/       pfx_len=_is_simple_pfx(d,"lib32" ) ; if (pfx_len) break ;
		/**/       pfx_len=_is_simple_pfx(d,"lib64" ) ; if (pfx_len) break ;
		/**/       pfx_len=_is_simple_pfx(d,"libx32") ; if (pfx_len) break ; return false ;                  // .
		case 'o' : pfx_len=_is_simple_pfx(d,"opt"   ) ; if (pfx_len) break ; return false ;                  // .
		case 'r' : pfx_len=_is_simple_pfx(d,"run"   ) ; if (pfx_len) break ; return false ;                  // .
		case 's' : pfx_len=_is_simple_pfx(d,"sbin"  ) ; if (pfx_len) break ; return false ;                  // .
		case 'u' : pfx_len=_is_simple_pfx(d,"usr"   ) ; if (pfx_len) break ; return false ;                  // .
		case 'v' : pfx_len=_is_simple_pfx(d,"var"   ) ; if (pfx_len) break ; return false ;                  // .
		default  :                                                           return false ;                  // .
	}
	d += pfx_len ;
	// check if enough .. to escape system dir
	struct IsSimpleCtx ctx = {
		.d         = d
	,	.i         = 0
	,	.depth     = 0
	,	.dots_seen = 3                                                                                       // evaluate in the context of a normal dir
	,	.is_simple = false                                                                                   // defensive programming
	} ;
	bpf_loop( 256 , _is_simple_dot_dot_step , &ctx , 0/*flags*/ ) ;
	return ctx.is_simple ;
}

//
// helpers
//

// state of current thread if its accesses must be captured, i.e. it belongs to a job and executes a tracked syscall or an io_uring operation
static __always_inline struct Task* _capturing() {
	struct task_struct* t  = bpf_get_current_task_btf()                 ;
	struct Task       * tv = bpf_task_storage_get( &tasks , t , 0 , 0 ) ; if ( !tv || !tv->job_id ) return 0 ;
	//
	if ( tv->suspend || !tv->enabled          )                                      return 0  ;
	if ( BPF_CORE_READ(t,flags)&PF_IO_WORKER_ ) { tv->cur_kind = EBPF_SCK_IO_URING ; return tv ; } // io_uring workers only execute io_uring operations
	//
	struct pt_regs* regs = (struct pt_regs*)bpf_task_pt_regs(t) ; if ( !regs                 ) return 0 ;
	long            nr   = SC_NR(regs)                          ; if ( nr<0 || nr>=NSYSCALLS ) return 0 ;
	__u32           k    = nr                                   ;
	#if HAS_32
		if (BPF_CORE_READ(t,thread_info.status)&TS_COMPAT_) k += NSYSCALLS ;
	#endif
	__u32* d = bpf_map_lookup_elem( &syscalls , &k ) ; if ( !d || !*d ) return 0 ;                 // captures of untracked syscalls would never be closed
	tv->cur_kind = EBPF_SC_KIND(*d) ;
	return tv ;
}

// file info, as stat would report it
struct inode___v611 { __s64             i_mtime_sec ; __u32 i_mtime_nsec ; } __attribute__((preserve_access_index)) ; // linux >= 6.11
struct inode___v66  { struct timespec64 __i_mtime   ;                      } __attribute__((preserve_access_index)) ; // linux >= 6.6
struct inode___old  { struct timespec64 i_mtime     ;                      } __attribute__((preserve_access_index)) ;
static __always_inline void _fill_info( struct EbpfEvent* /*inout*/ ev , struct inode* inode ) {
	if (!inode) return ;
	ev->mode = BPF_CORE_READ( inode , i_mode ) ;
	ev->size = BPF_CORE_READ( inode , i_size ) ;
	if (bpf_core_field_exists(((struct inode___v611*)0)->i_mtime_sec)) {
		ev->mtime_s  = BPF_CORE_READ( (struct inode___v611*)inode , i_mtime_sec  ) ;
		ev->mtime_ns = BPF_CORE_READ( (struct inode___v611*)inode , i_mtime_nsec ) ;
	} else if (bpf_core_field_exists(((struct inode___v66*)0)->__i_mtime)) {
		ev->mtime_s  = BPF_CORE_READ( (struct inode___v66*)inode , __i_mtime.tv_sec  ) ;
		ev->mtime_ns = BPF_CORE_READ( (struct inode___v66*)inode , __i_mtime.tv_nsec ) ;
	} else {
		ev->mtime_s  = BPF_CORE_READ( (struct inode___old*)inode , i_mtime.tv_sec  ) ;
		ev->mtime_ns = BPF_CORE_READ( (struct inode___old*)inode , i_mtime.tv_nsec ) ;
	}
	ev->flags |= EBPF_F_EXISTS ;
}

// capture a resolved object if it is of interest
// caller must have set ev->flags and specific fields
static __always_inline bool/*emitted*/ _capture_obj( struct Task* tv , __u8 kind , struct dentry* dentry , struct vfsmount* vmnt ) {
	tv->ptr[0] = (__u64)dentry ;
	tv->ptr[1] = (__u64)vmnt   ;
	__u32 r = _walk( (struct WalkData*)tv->ev.data , tv->ptr[0] , tv->ptr[1] , tv->job_id , 0/*want_abs*/ ) ;
	if (  r&WALK_TRUNC ) { _lost(tv) ; return false ; }
	if (!(r&WALK_OK   )) {             return false ; }
	tv->ev.kind   = kind         ;
	tv->ev.root   = WALK_ROOT(r) ;
	tv->ev.len[0] = WALK_LEN (r) ;
	tv->ev.len[1] = 0            ;
	_fill_info( /*inout*/&tv->ev , BPF_CORE_READ(dentry,d_inode) ) ;
	_emit_capture( tv , WALK_LEN(r) ) ;
	return true ;
}

static __always_inline struct file* _fd_file( struct task_struct* t , int fd ) {
	/**/                                                             if (fd<0                 ) return 0 ;
	struct files_struct * files = BPF_CORE_READ( t     , files   ) ; if (!files               ) return 0 ;
	struct fdtable      * fdt   = BPF_CORE_READ( files , fdt     ) ; if (!fdt                 ) return 0 ;
	unsigned int          max   = BPF_CORE_READ( fdt   , max_fds ) ; if ((unsigned int)fd>=max) return 0 ;
	struct file        ** fda   = BPF_CORE_READ( fdt   , fd      ) ; if (!fda                 ) return 0 ;
	struct file         * f     = 0                                ; bpf_probe_read_kernel( &f , sizeof(f) , &fda[fd] ) ;
	return f ;
}

// capture a path that could not be resolved : the path as provided by the user and the dir it is relative to
#define CAPTURE_DST        0x1
#define CAPTURE_KERNEL_STR 0x2
static __noinline void _capture_raw( struct task_struct* t , struct Task* tv , __u64 ptr , int dfd , __u32 flags ) {
	if (!ptr) return ;
	struct EbpfEvent* ev = &tv->ev ;
	long              n  ;
	if (flags&CAPTURE_KERNEL_STR) n = bpf_probe_read_kernel_str( ev->data , EBPF_PATH_MAX , (const void*)ptr ) ;
	else                          n = bpf_probe_read_user_str  ( ev->data , EBPF_PATH_MAX , (const void*)ptr ) ;
	if (n<=0) { _lost(tv) ; return ; }                                                                           // page is not resident any more, cannot do anything but report it
	if (n==1)               return ;                                                                             // empty path : access is through a fd, not through a path
	__u32 len0 = 0 ;
	if (ev->data[0]=='/') {
		if (_is_simple(ev->data,n,tv->deps_in_system)) return ;
		ev->root = EBPF_ROOT_ABS ;
	} else {
		struct dentry*   dentry ;
		struct vfsmount* vmnt   ;
		if (dfd==AT_FDCWD_) {
			struct fs_struct* fs = BPF_CORE_READ( t , fs ) ; if (!fs) return ;
			dentry = BPF_CORE_READ( fs , pwd.dentry ) ;
			vmnt   = BPF_CORE_READ( fs , pwd.mnt    ) ;
		} else {
			struct file* f = _fd_file(t,dfd) ; if (!f) return ;                                                  // bad dir fd, no access
			dentry = BPF_CORE_READ( f , f_path.dentry ) ;
			vmnt   = BPF_CORE_READ( f , f_path.mnt    ) ;
		}
		tv->ptr[0] = (__u64)dentry ;
		tv->ptr[1] = (__u64)vmnt   ;
		__u32 r = _walk( (struct WalkData*)ev->data , tv->ptr[0] , tv->ptr[1] , tv->job_id , 1/*want_abs*/ ) ;
		if (!(r&WALK_OK)) { _lost(tv) ; return ; }
		ev->root = WALK_ROOT(r) ;
		len0     = WALK_LEN (r) ;
		__u8* dst = &ev->data[len0&(EBPF_PATH_MAX-1)] ;                                                          // path has been overwritten, read it again after dir
		if (flags&CAPTURE_KERNEL_STR) n = bpf_probe_read_kernel_str( dst , EBPF_PATH_MAX , (const void*)ptr ) ;
		else                          n = bpf_probe_read_user_str  ( dst , EBPF_PATH_MAX , (const void*)ptr ) ;
		if (n<=0) { _lost(tv) ; return ; }
	}
	ev->kind   = EBPF_RAW               ;
	ev->flags  = 0                      ;
	ev->nr     = (flags&CAPTURE_DST)!=0 ;
	ev->len[0] = len0                   ;
	ev->len[1] = n                      ;
	_emit_capture( tv , len0+(__u32)n ) ;
}

// close captures
static __always_inline void _emit_end( struct Task* tv , __u8 flags , __s32 nr , __u8 is32 , __s64 rc ) {
	struct EbpfEvent* ev = &tv->ev ;
	ev->kind   = EBPF_END ;
	ev->root   = 0        ;
	ev->flags  = flags    ;
	ev->is32   = is32     ;
	ev->nr     = nr       ;
	ev->rc     = rc       ;
	ev->len[0] = 0        ;
	ev->len[1] = 0        ;
	if (bpf_core_enum_value_exists(enum bpf_func_id,BPF_FUNC_ktime_get_tai_ns)) ev->stamp_ns = bpf_ktime_get_tai_ns() ;
	else                                                                        ev->stamp_ns = 0                      ;
	_emit( tv , tv->tid , tv->pid , EBPF_HDR_SZ ) ;
	tv->pending = 0 ;
	tv->seen    = 0 ;
}

//
// process tracking
//

// id of pid in the pid namespace whose inode is ns_ino (which may be an ancestor of the active namespace of the task), 0 if not found
static __always_inline __u32 _ns_nr( struct pid* pid , __u64 ns_ino ) {
	if (!pid) return 0 ;
	unsigned int level = BPF_CORE_READ( pid , level ) ;
	for( unsigned int i=0 ; i<8 ; i++ ) {               // namespaces deeper than that are not supported
		if (i>level) break ;
		struct upid u = {} ;
		bpf_probe_read_kernel( &u , sizeof(u) , (char*)pid + bpf_core_field_offset(struct pid,numbers) + i*bpf_core_type_size(struct upid) ) ;
		if ((__u64)BPF_CORE_READ(u.ns,ns.inum)==ns_ino) return u.nr ;
	}
	return 0 ;
}

static __always_inline void _proc_start( struct Task* emitter , struct Task* tv , __u32 parent ) {
	struct EbpfEvent* ev = &emitter->ev ;
	ev->kind   = EBPF_PROC_START ;
	ev->flags  = 0               ;
	ev->nr     = parent          ;
	ev->len[0] = 0               ;
	ev->len[1] = 0               ;
	_emit( emitter , tv->tid , tv->pid , EBPF_HDR_SZ ) ;
}

// a new thread or process inherits the job of its creator
// sched_process_fork is called once creation is sure to succeed, so there will be a corresponding sched_process_exit
SEC("tp_btf/sched_process_fork")
int BPF_PROG( on_fork , struct task_struct* parent , struct task_struct* child ) {
	struct Task*   ptv = bpf_task_storage_get( &tasks , parent , 0 , 0                              ) ; if ( !ptv || !ptv->job_id )                return 0 ;
	struct Task*   ctv = bpf_task_storage_get( &tasks , child  , 0 , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if ( !ctv                 ) { _lost(ptv) ; return 0 ; }
	__u32          jid = ptv->job_id                                                                  ;
	struct JobCfg* jc  = bpf_map_lookup_elem( &job_cfg , &jid )                                       ; if ( !jc                  )                return 0 ; // job is gone
	ctv->job_id         = jid                 ;
	ctv->deps_in_system = ptv->deps_in_system ;
	ctv->enabled        = ptv->enabled        ;
	ctv->suspend        = 0                   ;
	ctv->pending        = 0                   ;
	ctv->seen           = 0                   ;
	ctv->tid            = _ns_nr( BPF_CORE_READ(child,thread_pid)              , jc->pidns_ino ) ;
	ctv->pid            = _ns_nr( BPF_CORE_READ(child,group_leader,thread_pid) , jc->pidns_ino ) ;
	_proc_start( ptv , ctv , ptv->pid ) ;
	return 0 ;
}

// io_uring workers are not created through fork, hence they are not seen above
// they are not announced as their creation may still fail (but their end is), they just need to belong to the job
SEC("tp_btf/task_newtask")
int BPF_PROG( on_newtask , struct task_struct* p , __u64 clone_flags ) {
	if (!(BPF_CORE_READ(p,flags)&PF_IO_WORKER_)) return 0 ;
	struct task_struct* t   = bpf_get_current_task_btf()                                              ;
	struct Task       * ptv = bpf_task_storage_get( &tasks , t , 0 , 0                              ) ; if ( !ptv || !ptv->job_id )                return 0 ;
	struct Task       * ctv = bpf_task_storage_get( &tasks , p , 0 , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if ( !ctv                 ) { _lost(ptv) ; return 0 ; }
	ctv->job_id         = ptv->job_id         ;
	ctv->deps_in_system = ptv->deps_in_system ;
	ctv->enabled        = ptv->enabled        ;
	ctv->suspend        = 0                   ;
	ctv->pending        = 0                   ;
	ctv->seen           = 0                   ;
	// a worker runs concurrently with the thread that created it and with other workers, so it needs its own id for its captures to be told apart
	__u32          jid = ptv->job_id                            ;
	struct JobCfg* jc  = bpf_map_lookup_elem( &job_cfg , &jid ) ;
	ctv->tid = jc ? _ns_nr( BPF_CORE_READ(p,thread_pid) , jc->pidns_ino ) : 0 ;
	if (!ctv->tid) ctv->tid = BPF_CORE_READ(p,pid) ;                            // any id will do as long as it is unique
	ctv->pid = ptv->pid ;
	return 0 ;
}

// if a thread is killed while processing a syscall, its captures are never closed : nobody can depend on such accesses
SEC("tp_btf/sched_process_exit")
int BPF_PROG( on_exit , struct task_struct* p ) {
	struct Task     * tv = bpf_task_storage_get( &tasks , p , 0 , 0 ) ; if ( !tv || !tv->job_id ) return 0 ;
	struct EbpfEvent* ev = &tv->ev                                    ;
	ev->kind   = EBPF_PROC_END ;
	ev->flags  = 0             ;
	ev->nr     = 0             ;
	ev->len[0] = 0             ;
	ev->len[1] = 0             ;
	_emit( tv , tv->tid , tv->pid , EBPF_HDR_SZ ) ;
	tv->job_id = 0 ;
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
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	// the hook does not provide the mount the link lies in, which is necessary if it is not the one of the root (overlay view, file system mounted in the repo)
	// it is that of the dir being walked, a link cannot be a mount point
	// if not available, links are only seen if they lie in the same file system as their root
	tv->ptr[0] = (__u64)dentry ;
	tv->ptr[1] = 0             ;
	if (bpf_core_field_exists(((struct nameidata*)0)->path)) {
		struct task_struct* task = bpf_get_current_task_btf()        ;
		struct nameidata*   nd   = BPF_CORE_READ( task , nameidata ) ;
		if (nd) tv->ptr[1] = (__u64)BPF_CORE_READ( nd , path.mnt ) ;
	}
	__u32 r = _walk( (struct WalkData*)tv->ev.data , tv->ptr[0] , tv->ptr[1] , tv->job_id , 0/*want_abs*/ ) ;
	if (r&WALK_TRUNC) { _lost(tv) ; return 0 ; }
	if (!(r&WALK_OK)) {             return 0 ; }
	struct EbpfEvent* ev   = &tv->ev     ;
	__u32             len0 = WALK_LEN(r) ;
	__u32             len1 = 0           ;
	ev->kind  = EBPF_LINK    ;
	ev->root  = WALK_ROOT(r) ;
	ev->flags = 0            ;
	_fill_info( /*inout*/ev , inode ) ;
	const char* target = BPF_CORE_READ( inode , i_link ) ;
	if (target) {
		long n = bpf_probe_read_kernel_str( &ev->data[len0&(EBPF_PATH_MAX-1)] , EBPF_PATH_MAX , target ) ;
		if (n>0) { len1 = n ; ev->flags |= EBPF_F_TARGET ; }
	}
	ev->len[0] = len0 ;
	ev->len[1] = len1 ;
	_emit_capture( tv , len0+len1 ) ;
	return 0 ;
}

// all the syscalls that just resolve a path go through filename_lookup : stat, access, readlink, chmod, truncate, chdir, source of link, ...
// when it succeeds, path is the resolved object, and nothing has been modified yet
SEC("fexit/filename_lookup")
int BPF_PROG( on_filename_lookup , int dfd , struct filename* name , unsigned flags , struct path* path , struct path* root , int ret ) {
	if (ret!=0) return 0 ;                                                                                                                // unresolved paths are handled at syscall exit
	if (dfd==EBPF_ROOT_FD) {                                                                                                              // the daemon registers a root
		if (_get_current_euid()!=0) return 0 ;
		__u32           z  = 0                                 ;
		struct RootReg* rr = bpf_map_lookup_elem(&root_reg,&z) ; if ( !rr || !rr->armed ) return 0 ;
		struct RootKey  rk = { .dentry=(__u64)BPF_CORE_READ(path,dentry) , .job_id=rr->job_id , .pad=0 } ;
		__u32           ri = rr->idx                           ;
		if (bpf_map_update_elem(&roots,&rk,&ri,BPF_ANY)==0) { rr->done = 1 ; rr->dentry = rk.dentry ; }
		rr->armed = 0 ;
		return 0 ;
	}
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES ;
	tv->ev.flags  = 0        ;
	_capture_obj( tv , EBPF_LOOKUP , BPF_CORE_READ(path,dentry) , BPF_CORE_READ(path,mnt) ) ;
	return 0 ;
}

// all the opens go through security_file_open, including execve and io_uring, once file is resolved and before it is truncated
// it is not called with O_PATH, which is handled at syscall exit
SEC("fentry/security_file_open")
int BPF_PROG( on_file_open , struct file* file ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen          |= SEEN_OPEN|SEEN_RES                                               ;
	tv->ev.flags       = (BPF_CORE_READ(file,f_mode)&FMODE_CREATED_) ? EBPF_F_CREATED : 0 ;
	tv->ev.open_flags  = BPF_CORE_READ(file,f_flags)                                      ;
	_capture_obj( tv , EBPF_OPEN , BPF_CORE_READ(file,f_path.dentry) , BPF_CORE_READ(file,f_path.mnt) ) ;
	return 0 ;
}

// all the opens also go through do_filp_open, a global function called from other files (hence it cannot be inlined)
// it is used to know what could not be opened : the file of an open, but also the interpreter of a script upon execve, which is not an arg of any syscall
SEC("fexit/do_filp_open")
int BPF_PROG( on_filp_open , int dfd , struct filename* pathname , const struct open_flags* op , struct file* ret ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->ptr[0] = (__u64)ret ;
	if (tv->ptr[0]<(__u64)-4095) return 0 ;                                                                    // open is ok, it is dealt with by on_file_open
	__u8 k = tv->cur_kind ;
	if ( k==EBPF_SCK_OPEN || k==EBPF_SCK_CREAT || k==EBPF_SCK_OPENAT2 ) { tv->seen |= SEEN_FAIL ; return 0 ; } // path is an arg of the syscall, it is captured at exit, where repetitions are filtered
	tv->seen |= SEEN_RAW ;
	struct task_struct* t = bpf_get_current_task_btf() ;
	_capture_raw( t , tv , (__u64)BPF_CORE_READ(pathname,name) , dfd , CAPTURE_KERNEL_STR ) ;
	return 0 ;
}

// creations : dentry is the (not yet existing) object within dir
// security_path_mknod is also called when a file is created by open
SEC("fentry/security_path_mknod")
int BPF_PROG( on_path_mknod , struct path* dir , struct dentry* dentry ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES ;
	tv->ev.flags  = 0        ;
	_capture_obj( tv , EBPF_CREATE , dentry , BPF_CORE_READ(dir,mnt) ) ;
	return 0 ;
}
SEC("fentry/security_path_mkdir")
int BPF_PROG( on_path_mkdir , struct path* dir , struct dentry* dentry ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES ;
	tv->ev.flags  = 0        ;
	_capture_obj( tv , EBPF_CREATE , dentry , BPF_CORE_READ(dir,mnt) ) ;
	return 0 ;
}
SEC("fentry/security_path_symlink")
int BPF_PROG( on_path_symlink , struct path* dir , struct dentry* dentry ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES ;
	tv->ev.flags  = 0        ;
	_capture_obj( tv , EBPF_CREATE , dentry , BPF_CORE_READ(dir,mnt) ) ;
	return 0 ;
}
SEC("fentry/security_path_link")
int BPF_PROG( on_path_link , struct dentry* old_dentry , struct path* new_dir , struct dentry* new_dentry ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES|SEEN_PAIR ;
	tv->ev.flags  = 0                  ;
	_capture_obj( tv , EBPF_CREATE , new_dentry , BPF_CORE_READ(new_dir,mnt) ) ;
	return 0 ;
}

// removals : dentry is the object within dir, before it is removed
SEC("fentry/security_path_unlink")
int BPF_PROG( on_path_unlink , struct path* dir , struct dentry* dentry ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES ;
	tv->ev.flags  = 0        ;
	_capture_obj( tv , EBPF_UNLINK , dentry , BPF_CORE_READ(dir,mnt) ) ;
	return 0 ;
}
SEC("fentry/security_path_rmdir")
int BPF_PROG( on_path_rmdir , struct path* dir , struct dentry* dentry ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen     |= SEEN_RES ;
	tv->ev.flags  = 0        ;
	_capture_obj( tv , EBPF_UNLINK , dentry , BPF_CORE_READ(dir,mnt) ) ;
	return 0 ;
}

// rename : both objects before rename occurs
SEC("fentry/security_path_rename")
int BPF_PROG( on_path_rename , struct path* old_dir , struct dentry* old_dentry , struct path* new_dir , struct dentry* new_dentry , unsigned int flags ) {
	struct Task* tv = _capturing() ; if (!tv) return 0 ;
	tv->seen |= SEEN_RES|SEEN_PAIR ;
	// a side which is not of interest is not reported, so each side also carries the mode of the other one (0 if it does not exist)
	struct inode* old_inode = BPF_CORE_READ( old_dentry , d_inode )          ;
	struct inode* new_inode = BPF_CORE_READ( new_dentry , d_inode )          ;
	__u32         old_mode  = old_inode ? BPF_CORE_READ(old_inode,i_mode) : 0 ;
	__u32         new_mode  = new_inode ? BPF_CORE_READ(new_inode,i_mode) : 0 ;
	tv->ev.flags      = 0        ;
	tv->ev.open_flags = new_mode ;
	_capture_obj( tv , EBPF_RENAME_SRC , old_dentry , BPF_CORE_READ(old_dir,mnt) ) ;
	tv->ev.flags      = 0        ;
	tv->ev.open_flags = old_mode ;
	_capture_obj( tv , EBPF_RENAME_DST , new_dentry , BPF_CORE_READ(new_dir,mnt) ) ;
	return 0 ;
}

//
// end of syscalls
//

static __always_inline void _register( struct task_struct* t , __u32 job_id ) {
	struct JobCfg* jc = bpf_map_lookup_elem( &job_cfg , &job_id                                      ) ; if (!jc)                                       return ;
	struct Task  * tv = bpf_task_storage_get( &tasks  , t       , 0 , BPF_LOCAL_STORAGE_GET_F_CREATE ) ; if (!tv) { __sync_fetch_and_add(&jc->lost,1) ; return ; }
	tv->job_id         = job_id                                                             ;
	tv->deps_in_system = jc->deps_in_system                                                 ;
	tv->enabled        = 1                                                                  ;
	tv->suspend        = 0                                                                  ;
	tv->pending        = 0                                                                  ;
	tv->seen           = 0                                                                  ;
	tv->tid            = _ns_nr( BPF_CORE_READ(t,thread_pid             ) , jc->pidns_ino ) ; // the job may run in a pid namespace nested in that of the client
	tv->pid            = _ns_nr( BPF_CORE_READ(t,group_leader,thread_pid) , jc->pidns_ino ) ;
	_proc_start( tv , tv , 0/*parent*/ ) ;
}

// a job that waits for a file by polling repeats the same failing syscall again and again, and several threads doing so are faster than the consumer, which finally makes the job fail
// there is no point in reporting more than the first one : the same file is seen the same way
// return true if syscall is the same as the last one reported by this thread : same syscall, same args, same path relative to the same dir, same error
// else record it as a candidate, to be validated once reported
// /!\ dir is identified by its dentry, which is not strictly safe if it is released and its address reused by another dir, this would need the dir to be changed by another thread
static __always_inline bool _is_repeat( struct task_struct* t , struct Task* tv , __u32 nr , __u32 is32 , long ret , const __u64* args , __u64 path , bool has_dfd , int dfd ) {
	struct Rep* last      = &tv->rep[0]   ;
	struct Rep* cand      = &tv->rep[1]   ;
	bool        was_valid = tv->rep_valid ;
	tv->rep_valid = 0 ;
	for( int i=0 ; i<8 ; i++ ) cand->path[i] = 0 ;
	long n = bpf_probe_read_user_str( cand->path , sizeof(cand->path) , (const void*)path ) ;
	if ( n<=1 || n>=(long)sizeof(cand->path) ) return false ;                                 // unreadable, empty or too long (it may have been truncated)
	struct dentry* dir = 0 ;
	if ( !has_dfd || dfd==AT_FDCWD_ ) { struct fs_struct* fs = BPF_CORE_READ( t , fs ) ; if (fs) dir = BPF_CORE_READ( fs , pwd.dentry    ) ; }
	else                              { struct file*      f  = _fd_file(t,dfd)        ; if (f ) dir = BPF_CORE_READ( f  , f_path.dentry ) ; }
	for( int i=0 ; i<6 ; i++ ) cand->args[i] = args[i] ;
	cand->dir  = (__u64)dir ;
	cand->rc   = ret        ;
	cand->nr   = nr         ;
	cand->is32 = is32       ;
	bool same = was_valid && cand->dir==last->dir && cand->rc==last->rc && cand->nr==last->nr && cand->is32==last->is32 ;
	for( int i=0 ; i<8 ; i++ ) same &= cand->path[i]==last->path[i] ;
	for( int i=0 ; i<6 ; i++ ) same &= cand->args[i]==last->args[i] ;
	if (same) tv->rep_valid = 1 ;
	return same ;
}

SEC("tp_btf/sys_exit")
int BPF_PROG( on_sys_exit , struct pt_regs* regs , long ret ) {
	long                nr   = SC_NR(regs)                ;
	struct task_struct* t    = bpf_get_current_task_btf() ;
	__u32               is32 = 0                          ;
	#if HAS_32
		if ( nr>=0 && (nr&X32_SYSCALL_BIT_) ) {                                                         // x32 ABI is not supported, such a syscall makes the job untrustable
			struct Task* tv = bpf_task_storage_get( &tasks , t , 0 , 0 ) ;
			if ( tv && tv->job_id ) _lost(tv) ;
			return 0 ;
		}
	#endif
	if ( nr<0 || nr>=NSYSCALLS ) return 0 ;
	#if HAS_32
		is32 = (BPF_CORE_READ(t,thread_info.status)&TS_COMPAT_)!=0 ;
		if (is32) {                                                                                     // exceptional, so we can afford to look for the task
			struct Task* tv = bpf_task_storage_get( &tasks , t , 0 , 0 ) ;
			if ( tv && tv->job_id && !HAS_32 ) { _lost(tv) ; return 0 ; }                               // nothing can be said about what this job does
		}
	#endif
	// first filter on syscall for fast elimination of uninteresting syscalls
	__u32  k = is32*NSYSCALLS + nr                    ;
	__u32* d = bpf_map_lookup_elem( &syscalls , &k ) ; if ( !d || !*d ) return 0 ;
	__u32  descr = *d                   ;
	__u32  kind  = EBPF_SC_KIND (descr) ;
	__u32  path1 = EBPF_SC_PATH1(descr) ;
	__u32  dfd1  = EBPF_SC_DFD1 (descr) ;
	__u32  path2 = EBPF_SC_PATH2(descr) ;
	__u32  dfd2  = EBPF_SC_DFD2 (descr) ;
	__u32  fd    = EBPF_SC_FD   (descr) ;
	__u32  flg   = EBPF_SC_FLAGS(descr) ;
	__u64  args[8] = {} ;                                                                               // 8 so that index can be masked rather than tested (7 means none)
	#if defined(__TARGET_ARCH_x86)
		if (is32) { args[0]=regs->bx ; args[1]=regs->cx ; args[2]=regs->dx ; args[3]=regs->si  ; args[4]=regs->di ; args[5]=regs->bp ; }
		else      { args[0]=regs->di ; args[1]=regs->si ; args[2]=regs->dx ; args[3]=regs->r10 ; args[4]=regs->r8 ; args[5]=regs->r9 ; }
	#elif defined(__TARGET_ARCH_arm64)
		args[0]=regs->orig_x0 ; args[1]=regs->regs[1] ; args[2]=regs->regs[2] ; args[3]=regs->regs[3] ; args[4]=regs->regs[4] ; args[5]=regs->regs[5] ;
	#endif
	// markers
	bool is_marker = kind==EBPF_SCK_READLINK && dfd1==0 ;
	if ( is_marker && (int)args[0]==EBPF_REGISTER_FD ) { _register( t , (__u32)args[3] ) ; return 0 ; }
	// then filter on tid
	struct Task* tv = bpf_task_storage_get( &tasks , t , 0 , 0 ) ; if ( !tv || !tv->job_id ) return 0 ;
	if (is_marker) {
		switch ((int)args[0]) {
			case EBPF_MAGIC_FD        : if (ret<0) tv->suspend = 1          ; goto Discard ;            // backdoor is not processed by anybody, job will do it by itself
			case EBPF_MAGIC_FD_END    :            tv->suspend = 0          ; goto Discard ;
			case EBPF_MAGIC_FD_ENABLE :            tv->enabled = args[3]!=0 ; goto Discard ;
		}
	}
	bool rep_cand = false ;
	if ( tv->suspend || !tv->enabled               ) goto Discard ;
	if ( ret<=-ERESTARTSYS_ && ret>=-ERESTART_MAX_ ) goto Discard ;                                     // syscall will be restarted
	//
	if (ret>=0) {
		// dir is designated by a fd
		if (fd!=EBPF_SC_NONE) {
			struct file* f = _fd_file( t , (int)args[fd&7] ) ;
			if (f) { tv->ev.flags = 0 ; _capture_obj( tv , EBPF_DIR , BPF_CORE_READ(f,f_path.dentry) , BPF_CORE_READ(f,f_path.mnt) ) ; }
		}
		// with O_PATH, the open hook is not called, but the fd designates the file
		if ( ( kind==EBPF_SCK_OPEN || kind==EBPF_SCK_OPENAT2 ) && !(tv->seen&SEEN_OPEN) ) {
			struct file* f = _fd_file( t , (int)ret ) ;
			if (f) {
				tv->ev.flags      = 0                           ;
				tv->ev.open_flags = BPF_CORE_READ( f , f_flags ) ;
				_capture_obj( tv , EBPF_OPEN , BPF_CORE_READ(f,f_path.dentry) , BPF_CORE_READ(f,f_path.mnt) ) ;
			}
		}
	} else {
		// paths that could not be resolved, the kernel has just read them, so the page is resident
		if (path2==EBPF_SC_NONE) {
			if ( path1!=EBPF_SC_NONE && ( (tv->seen&SEEN_FAIL) || !(tv->seen&(SEEN_RES|SEEN_RAW)) ) ) {
				if ( !tv->pending && kind!=EBPF_SCK_EXEC && kind!=EBPF_SCK_OPENAT2 ) {                  // syscall is fully described by its args and its path (openat2 has indirect flags)
					if (_is_repeat( t , tv , nr , is32 , ret , args , args[path1&7] , dfd1!=EBPF_SC_NONE , (int)args[dfd1&7] )) { tv->seen = 0 ; return 0 ; }
					rep_cand = true ;
				}
				_capture_raw( t , tv , args[path1&7] , dfd1!=EBPF_SC_NONE?(int)args[dfd1&7]:AT_FDCWD_ , 0/*flags*/ ) ;
			}
		} else {
			if (!(tv->seen&SEEN_PAIR)) {
				_capture_raw( t , tv , args[path1&7] , dfd1!=EBPF_SC_NONE?(int)args[dfd1&7]:AT_FDCWD_ , 0/*flags*/  ) ;
				_capture_raw( t , tv , args[path2&7] , dfd2!=EBPF_SC_NONE?(int)args[dfd2&7]:AT_FDCWD_ , CAPTURE_DST ) ;
			}
		}
	}
	if (!tv->pending) { tv->seen = 0 ; return 0 ; }                                                     // nothing of interest, this is the most common case
	//
	for( int i=0 ; i<6 ; i++ ) tv->ev.args[i] = args[i] ;
	switch (kind) {
		case EBPF_SCK_CREAT   : tv->ev.open_flags = 01|0100|01000/*O_WRONLY|O_CREAT|O_TRUNC*/ ; break ;
		case EBPF_SCK_OPENAT2 : {
			struct open_how how = {} ;
			bpf_probe_read_user( &how , sizeof(how) , (const void*)args[flg&7] ) ;
			tv->ev.open_flags = how.flags ;
		} break ;
		default               : tv->ev.open_flags = flg!=EBPF_SC_NONE ? args[flg&7] : 0 ;
	}
	_emit_end( tv , 0/*flags*/ , nr , is32 , ret ) ;
	if (rep_cand) { tv->rep[0] = tv->rep[1] ; tv->rep_valid = 1 ; }                                     // syscall has been reported, it need not be if it is repeated
	return 0 ;
Discard :
	if (tv->pending) _emit_end( tv , EBPF_F_DISCARD , nr , is32 , ret ) ;
	tv->seen = 0 ;
	return 0 ;
}

//
// io_uring : operations are executed either by the thread that submits them or by a worker, and their completion may be reported from yet another context
// so they are closed at the exit of their handler, which is the only place where we know to be in the context where captures were done
// args are laid out as for the equivalent syscall
//

static __always_inline struct Task* _io_begin( struct io_kiocb* req , int ret ) {
	struct task_struct* t  = bpf_get_current_task_btf()                 ;
	struct Task       * tv = bpf_task_storage_get( &tasks , t , 0 , 0 ) ; if ( !tv || !tv->job_id ) return 0 ;
	if ( tv->suspend || !tv->enabled ) {
		tv->seen = 0 ;
		return 0 ;
	}
	if (ret==-EAGAIN_) {                                                                                            // request will be re-issued (typically from a worker)
		if (tv->pending) _emit_end( tv , EBPF_F_IO|EBPF_F_DISCARD , BPF_CORE_READ(req,opcode) , 0/*is32*/ , ret ) ;
		tv->seen = 0 ;
		return 0 ;
	}
	for( int i=0 ; i<6 ; i++ ) tv->ev.args[i] = 0 ;
	tv->ev.open_flags = 0 ;
	return tv ;
}
static __always_inline void _io_raw( struct Task* tv , struct filename* fn , int dfd , __u32 flags ) {
	if (!fn) return ;
	struct task_struct* t = bpf_get_current_task_btf() ;
	_capture_raw( t , tv , (__u64)BPF_CORE_READ(fn,name) , dfd , flags|CAPTURE_KERNEL_STR ) ;
}
static __always_inline void _io_end( struct Task* tv , struct io_kiocb* req ) {
	if (!tv->pending) { tv->seen = 0 ; return ; }
	_emit_end( tv , EBPF_F_IO , BPF_CORE_READ(req,opcode) , 0/*is32*/ , BPF_CORE_READ(req,cqe.res) ) ;
}

static __always_inline int _io_open( struct io_kiocb* req , int ret , bool outer ) {
	struct Task* tv = _io_begin(req,ret)   ; if (!tv) return 0 ;
	if (outer) {                                                 // io_openat calls io_openat2, which has already done the job if it is not inlined
		bool done = tv->io_req==(__u64)req ;
		tv->io_req = 0 ;
		if (done) return 0 ;
	} else {
		tv->io_req = (__u64)req ;
	}
	struct io_open*  op = (struct io_open*)req ;                 // per-op data overlays the head of io_kiocb
	int   dfd   = BPF_CORE_READ(op,dfd      ) ;
	__u64 flags = BPF_CORE_READ(op,how.flags) ;
	// if open failed, path has been captured by on_filp_open
	tv->ev.args[0]    = (__u64)dfd ;
	tv->ev.args[1]    = 0          ; tv->ev.args[2] = 0 ; tv->ev.args[3] = 0 ; tv->ev.args[4] = 0 ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = flags      ;
	_io_end(tv,req) ;
	return 0 ;
}
// io_openat merely calls io_openat2, so both are hooked in case one is inlined in the other
SEC("fexit/io_openat2") int BPF_PROG( on_io_openat2 , struct io_kiocb* req , unsigned int issue_flags , int ret ) { return _io_open(req,ret,false/*outer*/) ; }
SEC("fexit/io_openat" ) int BPF_PROG( on_io_openat  , struct io_kiocb* req , unsigned int issue_flags , int ret ) { return _io_open(req,ret,true /*outer*/) ; }

SEC("fexit/io_statx")
int BPF_PROG( on_io_statx , struct io_kiocb* req , unsigned int issue_flags , int ret ) {
	struct Task    * tv = _io_begin(req,ret)    ; if (!tv) return 0 ;
	struct io_statx* op = (struct io_statx*)req ;
	int   dfd   = BPF_CORE_READ(op,dfd  ) ;
	__u64 flags = BPF_CORE_READ(op,flags) ;
	__u64 mask  = BPF_CORE_READ(op,mask ) ;
	if ( BPF_CORE_READ(req,cqe.res)<0 && !(tv->seen&SEEN_RES) ) _io_raw( tv , BPF_CORE_READ(op,filename) , dfd , 0/*flags*/ ) ;
	tv->ev.args[0] = (__u64)dfd ; tv->ev.args[1] = 0 ; tv->ev.args[2] = flags ; tv->ev.args[3] = mask ; tv->ev.args[4] = 0 ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = 0 ;
	_io_end(tv,req) ;
	return 0 ;
}

SEC("fexit/io_renameat")
int BPF_PROG( on_io_renameat , struct io_kiocb* req , unsigned int issue_flags , int ret ) {
	struct Task     * tv = _io_begin(req,ret)     ; if (!tv) return 0 ;
	struct io_rename* op = (struct io_rename*)req ;
	int   old_dfd = BPF_CORE_READ(op,old_dfd) ;
	int   new_dfd = BPF_CORE_READ(op,new_dfd) ;
	__u64 flags   = BPF_CORE_READ(op,flags  ) ;
	if ( BPF_CORE_READ(req,cqe.res)<0 && !(tv->seen&SEEN_PAIR) ) {
		_io_raw( tv , BPF_CORE_READ(op,oldpath) , old_dfd , 0/*flags*/  ) ;
		_io_raw( tv , BPF_CORE_READ(op,newpath) , new_dfd , CAPTURE_DST ) ;
	}
	tv->ev.args[0] = (__u64)old_dfd ; tv->ev.args[1] = 0 ; tv->ev.args[2] = (__u64)new_dfd ; tv->ev.args[3] = 0 ; tv->ev.args[4] = flags ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = 0 ;
	_io_end(tv,req) ;
	return 0 ;
}

SEC("fexit/io_unlinkat")
int BPF_PROG( on_io_unlinkat , struct io_kiocb* req , unsigned int issue_flags , int ret ) {
	struct Task     * tv = _io_begin(req,ret)     ; if (!tv) return 0 ;
	struct io_unlink* op = (struct io_unlink*)req ;
	int   dfd   = BPF_CORE_READ(op,dfd  ) ;
	__u64 flags = BPF_CORE_READ(op,flags) ;
	if ( BPF_CORE_READ(req,cqe.res)<0 && !(tv->seen&SEEN_RES) ) _io_raw( tv , BPF_CORE_READ(op,filename) , dfd , 0/*flags*/ ) ;
	tv->ev.args[0] = (__u64)dfd ; tv->ev.args[1] = 0 ; tv->ev.args[2] = flags ; tv->ev.args[3] = 0 ; tv->ev.args[4] = 0 ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = 0 ;
	_io_end(tv,req) ;
	return 0 ;
}

SEC("fexit/io_mkdirat")
int BPF_PROG( on_io_mkdirat , struct io_kiocb* req , unsigned int issue_flags , int ret ) {
	struct Task    * tv = _io_begin(req,ret)    ; if (!tv) return 0 ;
	struct io_mkdir* op = (struct io_mkdir*)req ;
	int dfd = BPF_CORE_READ(op,dfd) ;
	if ( BPF_CORE_READ(req,cqe.res)<0 && !(tv->seen&SEEN_RES) ) _io_raw( tv , BPF_CORE_READ(op,filename) , dfd , 0/*flags*/ ) ;
	tv->ev.args[0] = (__u64)dfd ; tv->ev.args[1] = 0 ; tv->ev.args[2] = 0 ; tv->ev.args[3] = 0 ; tv->ev.args[4] = 0 ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = 0 ;
	_io_end(tv,req) ;
	return 0 ;
}

SEC("fexit/io_linkat")
int BPF_PROG( on_io_linkat , struct io_kiocb* req , unsigned int issue_flags , int ret ) {
	struct Task   * tv      = _io_begin(req,ret)        ; if (!tv) return 0 ;
	struct io_link* op      = (struct io_link*)req      ;
	int             old_dfd = BPF_CORE_READ(op,old_dfd) ;
	int             new_dfd = BPF_CORE_READ(op,new_dfd) ;
	__u64           flags   = BPF_CORE_READ(op,flags  ) ;
	if ( BPF_CORE_READ(req,cqe.res)<0 && !(tv->seen&SEEN_PAIR) ) {
		_io_raw( tv , BPF_CORE_READ(op,oldpath) , old_dfd , 0/*flags*/  ) ;
		_io_raw( tv , BPF_CORE_READ(op,newpath) , new_dfd , CAPTURE_DST ) ;
	}
	tv->ev.args[0] = (__u64)old_dfd ; tv->ev.args[1] = 0 ; tv->ev.args[2] = (__u64)new_dfd ; tv->ev.args[3] = 0 ; tv->ev.args[4] = flags ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = 0 ;
	_io_end(tv,req) ;
	return 0 ;
}

SEC("fexit/io_symlinkat")
int BPF_PROG( on_io_symlinkat , struct io_kiocb* req , unsigned int issue_flags , int ret ) {
	struct Task   * tv      = _io_begin(req,ret)        ; if (!tv) return 0 ;
	struct io_link* op      = (struct io_link*)req      ;                     // symlinkat uses struct io_link in io_uring
	int             new_dfd = BPF_CORE_READ(op,new_dfd) ;
	if ( BPF_CORE_READ(req,cqe.res)<0 && !(tv->seen&SEEN_RES) ) _io_raw( tv , BPF_CORE_READ(op,newpath) , new_dfd , 0/*flags*/ ) ;
	tv->ev.args[0] = 0 ; tv->ev.args[1] = (__u64)new_dfd ; tv->ev.args[2] = 0 ; tv->ev.args[3] = 0 ; tv->ev.args[4] = 0 ; tv->ev.args[5] = 0 ;
	tv->ev.open_flags = 0 ;
	_io_end(tv,req) ;
	return 0 ;
}
