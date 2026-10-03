// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// BPF program for the ebpf autodep method.
// It observes file accesses of the traced process sub-tree, including those submitted through io_uring (which bypass syscalls),
// and pushes one event per access into a ring buffer. The user-space consumer (ebpf.cc) replays each event through the same
// SyscallDescr machinery as ptrace/seccomp, so that deps and targets are computed identically.
//
// Path strings are captured from the kernel copy made by getname_flags (avoids user page faults and is syscall-agnostic).
// io_uring path operations are captured on the io_* handlers and translated into the equivalent syscall.

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "ebpf_event.h"

char LICENSE[] SEC("license") = "GPL" ; // required to use GPL-only helpers

// x86_64 syscall numbers we need to recognize by hand (io_uring ops are translated to these) ; other syscalls are selected through the `wanted` map
#define NR_readlinkat 267
#define NR_openat2    437
#define NR_statx      332
#define NR_renameat2  316
#define NR_unlinkat   263
#define NR_mkdirat    258
#define NR_linkat     265
#define NR_symlinkat  266

#define AT_FDCWD_     (-100)
#define MAGIC_FD      (AT_FDCWD_-100) // Backdoor::MagicFd    : begin backdoor local-processing suspension window
#define MAGIC_FD_END  (AT_FDCWD_-101) // Backdoor::MagicFdEnd : end   backdoor local-processing suspension window

#define NSYSCALLS EBPF_NSYSCALLS // >= SyscallDescr::NSyscalls ; sizes the wanted array
#define MAX_JOBS  4096

// the programs are loaded once by the root daemon and shared by all jobs on the machine ; each job is identified by a job_id and has :
// - its own ring buffer (job_rbs[job_id]) so a consumer never sees another job's (possibly another user's) accesses
// - its own pid namespace (job_cfg[job_id]) so reported tids are resolvable by that job's consumer through /proc

struct job_cfg_val {
	__u64 pidns_dev      ; // (dev,ino) of the job consumer's pid namespace, so we can report tids as it sees them (bpf_get_ns_current_pid_tgid)
	__u64 pidns_ino      ;
	__u32 deps_in_system ; // if 0, system files are simple and dropped in-kernel
} ;

struct {                                         // job_id -> per-job configuration ; filled by the daemon at job registration
	__uint( type        , BPF_MAP_TYPE_HASH  ) ;
	__uint( max_entries , MAX_JOBS           ) ;
	__type( key         , __u32              ) ;
	__type( value       , struct job_cfg_val ) ;
} job_cfg SEC(".maps") ;

struct inner_rb {                                  // template describing the per-job ring buffers stored in job_rbs
	__uint( type        , BPF_MAP_TYPE_RINGBUF ) ;
	__uint( max_entries , 4<<20                ) ; // 4MB per job ; an overflow makes that job fail (never a silent dep loss)
} inner_rb SEC(".maps") ;

struct {                                                 // job_id -> that job's ring buffer ; the daemon creates the inner maps and inserts them
	__uint ( type        , BPF_MAP_TYPE_HASH_OF_MAPS ) ;
	__uint ( max_entries , MAX_JOBS                  ) ;
	__type ( key         , __u32                     ) ;
	__array( values      , struct inner_rb           ) ;
} job_rbs SEC(".maps") ;

// tid/tgid of the current task in the given job's pid namespace (so /proc/<tid> is resolvable by that job's consumer, even under WSL where bpf global pids differ)
static __always_inline void _ns_ids( __u32 job_id , __u32* /*out*/ tid , __u32* /*out*/ tgid ) {
	*tid  = 0 ;
	*tgid = 0 ;
	struct job_cfg_val*   jc = bpf_map_lookup_elem(&job_cfg,&job_id) ; if (!jc) return ;
	struct bpf_pidns_info ns = {}                                    ;
	if (bpf_get_ns_current_pid_tgid( jc->pidns_dev , jc->pidns_ino , &ns , sizeof(ns) )==0) {
		*tid  = ns.pid  ;
		*tgid = ns.tgid ;
	}
}

struct traced_val {
	__u32 job_id         ; // job this task belongs to : selects the ring buffer and the pid namespace
	__u8  deps_in_system ; // cached from the job config : whether system files may be deps (else dropped in-kernel)
	__u8  enabled        ; // mirrors the autodep enable bit (toggled through the backdoor), inherited by children
	__u8  suspend        ; // if set, this thread is inside the backdoor local-processing fallback : do not record its accesses
} ;

struct {                                        // filters events to the traced process sub-trees ; keyed by global tid
	__uint( type        , BPF_MAP_TYPE_HASH ) ;
	__uint( max_entries , 65536             ) ;
	__uint( map_flags   , BPF_F_NO_PREALLOC ) ; // only live threads consume memory
	__type( key         , __u32             ) ;
	__type( value       , struct traced_val ) ;
} traced SEC(".maps") ;

struct {                                         // syscalls to record : wanted[nr]!=0 ; filled by user space from SyscallDescr::s_tab
	__uint( type        , BPF_MAP_TYPE_ARRAY ) ;
	__uint( max_entries , NSYSCALLS          ) ;
	__type( key         , __u32              ) ;
	__type( value       , __u8               ) ;
} wanted SEC(".maps") ;


struct {                                        // partial event being built for an in-progress syscall ; keyed by tid , alive between sys_enter and sys_exit
	__uint( type        , BPF_MAP_TYPE_HASH ) ;
	__uint( max_entries , 8192              ) ;
	__uint( map_flags   , BPF_F_NO_PREALLOC ) ; // value is large (~12KB) : only in-flight syscalls consume memory
	__type( key         , __u32             ) ;
	__type( value       , struct ebpf_event ) ;
} events SEC(".maps") ;

struct {                                                // always-zero template used to (re)initialize an events entry without putting a huge value on the BPF stack
	__uint( type        , BPF_MAP_TYPE_PERCPU_ARRAY ) ;
	__uint( max_entries , 1                         ) ;
	__type( key         , __u32                     ) ;
	__type( value       , struct ebpf_event         ) ;
} zero SEC(".maps") ;

struct {                                                // scratch to build an io_uring event off-stack (io_uring ops have no sys_enter/sys_exit pairing)
	__uint( type        , BPF_MAP_TYPE_PERCPU_ARRAY ) ;
	__uint( max_entries , 1                         ) ;
	__type( key         , __u32                     ) ;
	__type( value       , struct ebpf_event         ) ;
} io_scratch SEC(".maps") ;

static __always_inline __u32 _hdr_sz(__u32 n_blobs) {
	return offsetof(struct ebpf_event,blobs) + n_blobs*sizeof(struct ebpf_blob) ;
}

// push an event to the ring buffer of its job (routed through the map-of-maps) ; if the job is gone, the event is dropped
static __always_inline void _emit( __u32 job_id , struct ebpf_event* ev , __u32 n_blobs ) {
	void* rb = bpf_map_lookup_elem( &job_rbs , &job_id ) ;
	if (!rb                   ) return ;
	if (n_blobs>EBPF_MAX_BLOBS) n_blobs = EBPF_MAX_BLOBS ;
	bpf_ringbuf_output( rb , ev , _hdr_sz(n_blobs) , 0/*flags*/ ) ;
}

// append a captured kernel string as the next blob ; the slot is picked by a switch so its offset is constant (the verifier rejects a variable-offset destination for a large write)
static __always_inline void _add_blob( struct ebpf_event* ev , __u64 key , const char* name ) {
	__u32             n = ev->n_blobs ;
	struct ebpf_blob* b ;
	switch (n) {
		case 0  : b = &ev->blobs[0] ; break ;
		case 1  : b = &ev->blobs[1] ; break ;
		case 2  : b = &ev->blobs[2] ; break ;
		default : return ;
	}
	long len = bpf_probe_read_kernel_str( b->data , EBPF_BLOB_MAX , name ) ;
	if (len<=0) return ;
	b->addr     = key   ;
	b->len      = len   ;
	ev->n_blobs = n + 1 ;
}

// resolve the current task's working directory in kernel context, storing its components (leaf first, NUL-separated) into ev->cwd
// done at access time (the task is current and alive), so the consumer never needs /proc (which is gone once a short-lived process exited)
#define CWD_MAX_DEPTH 40
static __always_inline void _fill_cwd(struct ebpf_event* ev) {
	ev->cwd_len = 0 ;
	struct task_struct* t      = bpf_get_current_task_btf()                                               ;
	struct fs_struct  * fs     = BPF_CORE_READ( t  , fs         )                                         ; if (!fs) return ;
	struct dentry     * dentry = BPF_CORE_READ( fs , pwd.dentry )                                         ;
	struct vfsmount   * vmnt   = BPF_CORE_READ( fs , pwd.mnt    )                                         ;
	struct mount      * mnt    = (struct mount*)( (char*)vmnt - bpf_core_field_offset(struct mount,mnt) ) ;
	__u32               off    = 0                                                                        ;
	for( int i=0 ; i<CWD_MAX_DEPTH ; i++ ) {
		struct dentry* parent   = BPF_CORE_READ( dentry , d_parent    ) ;
		struct dentry* mnt_root = BPF_CORE_READ( mnt    , mnt.mnt_root) ;
		if (dentry==mnt_root) {                                                                                                 // reached the root of the current mount : cross to the parent mount
			struct mount* mp = BPF_CORE_READ( mnt , mnt_parent ) ; if (mp==mnt) break ;                                         // global root reached
			dentry = BPF_CORE_READ( mnt , mnt_mountpoint ) ;
			mnt    = mp                                    ;
			continue ;
		}
		/**/                                                                                  if (dentry==parent      ) break ; // safety : detached dentry
		const char* name = (const char*)BPF_CORE_READ( dentry , d_name.name )               ; if (!name               ) break ;
		/**/                                                                                  if (off>=EBPF_BLOB_MAX/2) break ; // limit cwd to ~half the buffer, leaving room for a 256-byte component
		__u32       woff = off & (EBPF_BLOB_MAX/2-1)                                        ;                                   // mask for verifier static analysis
		long        n    = bpf_probe_read_kernel_str( &ev->cwd[woff] , 256/*size*/ , name ) ; if (n<=0                ) break ;
		off    = woff + (__u32)n ;                                                                                              // n includes the terminating NUL : components stay NUL-separated
		dentry = parent          ;
	}
	ev->cwd_len = off ;
}

//
// in-kernel "simple path" filter : drop accesses to system files before they reach the ring buffer (perf : avoids flooding it with /usr, /lib, ... opens)
// /!\ must stay conservative : only drop what user-space Record::s_is_simple would also drop (else a real dep would be lost) ; when unsure, keep the event
//

static __always_inline bool _pfx( const __u8* d , const char* p , int n ) {                // d starts with p (n chars) and the next char is a component boundary
	for( int i=0 ; i<n ; i++ )                                                             // (d has at least EBPF_BLOB_MAX bytes and is NUL-terminated, so reads are in-bounds and stop at the NUL)
		if (d[i]!=(__u8)p[i]) return false ;
	__u8 b = d[n] ;
	return b=='/' || b==0 ;
}
static __always_inline bool _has_dotdot( const __u8* d , __u32 len ) {                     // true if the path contains a ".." component (then we keep the event and let user-space resolve it)
	if (len>256) len = 256 ;
	for( int i=1 ; i<255 ; i++ ) {
		if (   (__u32)(i+1)>=len                        ) break       ;
		if (!( d[i-1]=='/' && d[i]=='.' && d[i+1]=='.' )) continue    ;
		if (   (__u32)(i+2)>=len                        ) return true ;
		__u8 nxt = d[i+2] ;
		if ( nxt=='/' || nxt==0                         ) return true ;
	}
	return false ;
}
static __always_inline bool _is_simple( const __u8* d , __u32 len , int deps_in_system ) {
	if ( len==0 || d[0]!='/' ) return false ;                                              // relative or empty : not simple (keep)
	if ( _has_dotdot(d,len)  ) return false ;                                              // may escape its top dir : keep and let user-space decide
	int special = 0 ;                                                                      // simple regardless of deps_in_system
	int sys     = 0 ;                                                                      // simple only if !deps_in_system
	if (_pfx(d,"/dev",4)) {                                                                // /dev, with the exceptions that are actually processed
		special = 1 ;
		if (d[4]=='/') {
			if (
				_pfx(d,"/dev/fd"     , 7)
			||	_pfx(d,"/dev/random" ,11)
			||	_pfx(d,"/dev/urandom",12)
			||	_pfx(d,"/dev/stderr" ,11)
			||	_pfx(d,"/dev/stdin"  ,10)
			||	_pfx(d,"/dev/stdout" ,11)
			) return false ;
		}
	} else if ( _pfx(d,"/proc",5) ) {
		special = 1 ;                                                                      // /proc, except /proc/<pid> and /proc/self
		if (d[5]=='/') {
			__u8 c = d[6] ;
			if ( c>='0' && c<='9'        ) return false ;
			if ( _pfx(d,"/proc/self",10) ) return false ;
		}
	}
	else if ( _pfx(d,"/sys"  ,4) ) special = 1 ;
	else if ( _pfx(d,"/run"  ,4) ) special = 1 ;
	else if ( _pfx(d,"/var"  ,4) ) special = 1 ;
	else if ( _pfx(d,"/usr"  ,4) ) sys     = 1 ;
	else if ( _pfx(d,"/bin"  ,4) ) sys     = 1 ;
	else if ( _pfx(d,"/etc"  ,4) ) sys     = 1 ;
	else if ( _pfx(d,"/opt"  ,4) ) sys     = 1 ;
	else if ( _pfx(d,"/sbin" ,5) ) sys     = 1 ;
	else if ( _pfx(d,"/lib32",6) ) sys     = 1 ;
	else if ( _pfx(d,"/lib64",6) ) sys     = 1 ;
	else if ( _pfx(d,"/lib"  ,4) ) sys     = 1 ;
	else                           return false ;                                          // unknown top dir : keep
	if (special                  ) return true  ;
	if (!deps_in_system          ) return true  ;                                          // system files are not deps for this job
	/**/                           return false ;                                          // deps_in_system : keep system files
}

//
// syscalls
//

SEC("tracepoint/raw_syscalls/sys_enter")
int on_sys_enter(struct trace_event_raw_sys_enter* ctx) {
	__u32 tid = (__u32)bpf_get_current_pid_tgid() ;
	long  id  = ctx->id                           ;
	// self-registration marker : the child announces itself (and its job_id, carried in arg 3) as the root of a traced sub-tree, in the namespace we see here
	if ( id==NR_readlinkat && (int)ctx->args[0]==EBPF_REGISTER_FD ) {
		__u32               job_id = (__u32)ctx->args[3]                                                                            ;
		struct job_cfg_val* jc     = bpf_map_lookup_elem(&job_cfg,&job_id)                                                          ;
		struct traced_val   v      = { .job_id=job_id , .deps_in_system=(__u8)(jc?jc->deps_in_system:0) , .enabled=1 , .suspend=0 } ;
		bpf_map_update_elem( &traced , &tid , &v , BPF_ANY ) ;
		return 0 ;
	}
	struct traced_val* tv = bpf_map_lookup_elem(&traced,&tid) ;
	if ( !tv                   ) return 0 ;                                         // not part of the traced sub-tree
	if ( id<0 || id>=NSYSCALLS ) return 0 ;
	// backdoor suspension window : the magic readlinkat calls delimit the local-processing fallback whose own syscalls must not be recorded
	if (id==NR_readlinkat) {
		int dfd = (int)ctx->args[0] ;
		if (dfd==MAGIC_FD    ) { tv->suspend = 1 ; return 0 ; }
		if (dfd==MAGIC_FD_END) { tv->suspend = 0 ; return 0 ; }
	}
	if (tv->suspend) return 0 ;
	__u32 wk = id                                   ;
	__u8* w  = bpf_map_lookup_elem( &wanted , &wk ) ; if ( !w || !*w ) return 0 ;   // syscall not recorded by this method
	// (re)initialize the per-tid event from the zero template
	__u32              zk = 0                              ;
	struct ebpf_event* z  = bpf_map_lookup_elem(&zero,&zk) ; if (!z) return 0 ;
	bpf_map_update_elem(&events,&tid,z,BPF_ANY) ;
	struct ebpf_event* ev = bpf_map_lookup_elem(&events,&tid) ; if (!ev) return 0 ;
	_ns_ids( tv->job_id , /*out*/&ev->tid , /*out*/&ev->pid ) ;                     // tids as this job's consumer sees them (for /proc resolution)
	ev->nr       = id                      ;
	ev->is32     = 0                       ;
	ev->n_blobs  = 0                       ;
	ev->stamp_ns = bpf_ktime_get_boot_ns() ;
	_fill_cwd(ev) ;
	for( int i=0 ; i<6 ; i++ ) ev->args[i]  = ((volatile struct trace_event_raw_sys_enter*)ctx)->args[i] ;
	// openat2 : the open_how struct is not seen by getname, capture it directly from user space
	if (id==NR_openat2) {
		struct ebpf_blob* b = &ev->blobs[0] ;
		b->addr = ctx->args[2]            ;
		b->len  = sizeof(struct open_how) ;
		if ( bpf_probe_read_user(b->data,sizeof(struct open_how),(void*)ctx->args[2])==0 ) ev->n_blobs = 1 ;
	}
	return 0 ;
}

SEC("tracepoint/raw_syscalls/sys_exit")
int on_sys_exit(struct trace_event_raw_sys_exit* ctx) {
	__u32              tid = (__u32)bpf_get_current_pid_tgid() ;
	struct ebpf_event* ev  = bpf_map_lookup_elem(&events,&tid) ; if (!ev) return 0 ;
	struct traced_val* tv  = bpf_map_lookup_elem(&traced,&tid) ;
	ev->rc = ctx->ret ;
	// drop single-path accesses to system files in-kernel (they are dropped by user-space anyway) ; multi-path ops (rename/link) and fd-based ops are kept
	bool drop = ev->n_blobs==1 && tv && _is_simple( ev->blobs[0].data , ev->blobs[0].len , tv->deps_in_system ) ;
	if ( tv && !drop ) _emit( tv->job_id , ev , ev->n_blobs ) ;
	bpf_map_delete_elem(&events,&tid) ;
	return 0 ;
}

// getname_flags copies the user path into a kernel buffer for (almost) every path-taking syscall : capture it, keyed by the user pointer
SEC("fexit/getname_flags")
int BPF_PROG( on_getname , const char* user , int flags , int empty_all , struct filename* ret ) {
	__u32              tid  = (__u32)bpf_get_current_pid_tgid()  ;
	struct ebpf_event* ev   = bpf_map_lookup_elem(&events,&tid)  ; if ( !ev                                   ) return 0 ;
	/**/                                                           if ( ret==0 || (unsigned long)ret>=-4095UL ) return 0 ; // IS_ERR : getname failed
	const char*        name =        BPF_CORE_READ( ret , name ) ; if ( !name                                 ) return 0 ;
	__u64              uptr = (__u64)BPF_CORE_READ( ret , uptr ) ;
	_add_blob( ev , uptr?uptr:(__u64)name , name ) ;                                                                       // key must match the syscall argument, which is the user pointer
	return 0 ;
}

//
// process tracking
//

SEC("tp_btf/task_newtask")
int BPF_PROG( on_newtask , struct task_struct* p , __u64 clone_flags ) {
	__u32              ptid = (__u32)bpf_get_current_pid_tgid()  ;
	struct traced_val* ptv  = bpf_map_lookup_elem(&traced,&ptid) ; if (!ptv) return 0 ; // parent is not traced
	__u32              ctid = BPF_CORE_READ(p,pid)               ;                      // note : io-wq / SQPOLL workers are created here too (via create_io_thread), so io_uring is covered
	//
	struct traced_val cv = { .job_id=ptv->job_id , .deps_in_system=ptv->deps_in_system , .enabled=ptv->enabled , .suspend=0 } ;
	bpf_map_update_elem(&traced,&ctid,&cv,BPF_ANY) ;
	return 0 ;
}

SEC("tp_btf/sched_process_exit")
int BPF_PROG( on_task_exit , struct task_struct* p ) {
	__u32 tid = BPF_CORE_READ( p , pid ) ;
	bpf_map_delete_elem( &traced , &tid ) ;
	bpf_map_delete_elem( &events , &tid ) ;
	return 0 ;
}

//
// io_uring : translate each path operation into the equivalent syscall event
//

static __always_inline struct ebpf_event* _io_begin(__u32* /*out*/ job_id) {
	*job_id = 0 ;
	__u32              tid = (__u32)bpf_get_current_pid_tgid()     ;
	struct traced_val* tv  = bpf_map_lookup_elem( &traced , &tid ) ; if ( !tv || tv->suspend ) return 0 ;
	*job_id = tv->job_id ;
	__u32              sk = 0                                        ;
	struct ebpf_event* ev = bpf_map_lookup_elem( &io_scratch , &sk ) ; if (!ev) return 0 ;
	// initialize only the fields the io_* handlers do not set themselves (cannot memcpy the whole header : it is too large for the bpf inliner)
	_ns_ids( tv->job_id , /*out*/&ev->tid , /*out*/&ev->pid ) ; // tids as this job's consumer sees them (for /proc resolution)
	for( int i=0 ; i<6 ; i++ ) ev->args[i]  = 0                       ;
	/**/                       ev->n_blobs  = 0                       ;
	/**/                       ev->is32     = 0                       ;
	/**/                       ev->stamp_ns = bpf_ktime_get_boot_ns() ;
	_fill_cwd(ev) ;
	return ev ;
}

// append the path referenced by a struct filename* to the event, and return the key to store in the matching args slot
static __always_inline __u64 _io_path( struct ebpf_event* ev , struct filename* fn ) {
	if (!fn) return 0 ;
	const char* name = (const char*)BPF_CORE_READ( fn , name ) ; if (!name) return 0 ;
	__u64       uptr = (__u64)BPF_CORE_READ(fn,uptr)           ;
	__u64       key  = uptr ? uptr : (__u64)name               ;
	_add_blob( ev , key , name ) ;
	return key ;
}

static __always_inline void _io_emit( __u32 job_id , struct ebpf_event* ev , long res ) {
	if (res==-11/*-EAGAIN*/) return ;                                                     // request will be re-issued (typically from an io-wq worker), record it there
	ev->rc = res ;
	_emit( job_id , ev , ev->n_blobs ) ;
}

SEC("fexit/io_openat2")
int BPF_PROG( on_io_openat2 , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_open   * op  = (struct io_open*)req   ;                     // per-op data overlays the head of io_kiocb
	ev->nr      = NR_openat2                                  ;
	ev->args[0] = (__u64)(int)   BPF_CORE_READ(op, dfd    )   ;
	ev->args[1] = _io_path( ev , BPF_CORE_READ(op,filename) ) ;
	// how : append as a second blob and point args[2] at it
	__u32 n = ev->n_blobs ;
	if (n<EBPF_MAX_BLOBS) {
		struct ebpf_blob* b = &ev->blobs[n] ;
		b->addr = (__u64)(unsigned long)req + 1 ;                         // synthetic, unique per request
		b->len  = sizeof(struct open_how)       ;
		struct open_how how = {} ; BPF_CORE_READ_INTO( &how , op , how ) ;
		__builtin_memcpy( b->data , &how , sizeof(how) ) ;
		ev->args[2] = b->addr                 ;
		ev->args[3] = sizeof(struct open_how) ;
		ev->n_blobs = n+1                     ;
	}
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}

SEC("fexit/io_statx")
int BPF_PROG( on_io_statx , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_statx  * op  = (struct io_statx*)req  ;
	ev->nr      = NR_statx                                    ;
	ev->args[0] = (__u64)(int)BPF_CORE_READ( op , dfd )       ;
	ev->args[1] = _io_path( ev , BPF_CORE_READ(op,filename) ) ;
	ev->args[2] =                BPF_CORE_READ(op, flags  )   ;
	ev->args[3] =                BPF_CORE_READ(op, mask   )   ;
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}

SEC("fexit/io_renameat")
int BPF_PROG( on_io_renameat , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_rename * op  = (struct io_rename*)req ;
	ev->nr      = NR_renameat2                               ;
	ev->args[0] = (__u64)(int)   BPF_CORE_READ(op,old_dfd)   ;
	ev->args[1] = _io_path( ev , BPF_CORE_READ(op,oldpath) ) ;
	ev->args[2] = (__u64)(int)   BPF_CORE_READ(op,new_dfd)   ;
	ev->args[3] = _io_path( ev , BPF_CORE_READ(op,newpath) ) ;
	ev->args[4] =                BPF_CORE_READ(op,flags  )   ;
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}

SEC("fexit/io_unlinkat")
int BPF_PROG( on_io_unlinkat , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_unlink * op  = (struct io_unlink*)req ;
	ev->nr      = NR_unlinkat                                 ;
	ev->args[0] = (__u64)(int)   BPF_CORE_READ(op,dfd     )   ;
	ev->args[1] = _io_path( ev , BPF_CORE_READ(op,filename) ) ;
	ev->args[2] =                BPF_CORE_READ(op,flags   )   ;
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}

SEC("fexit/io_mkdirat")
int BPF_PROG( on_io_mkdirat , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_mkdir  * op  = (struct io_mkdir*)req  ;
	ev->nr      = NR_mkdirat                                  ;
	ev->args[0] = (__u64)(int)   BPF_CORE_READ(op, dfd    )   ;
	ev->args[1] = _io_path( ev , BPF_CORE_READ(op,filename) ) ;
	ev->args[2] =                BPF_CORE_READ(op, mode   )   ;
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}

SEC("fexit/io_linkat")
int BPF_PROG( on_io_linkat , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_link   * op  = (struct io_link*)req   ;
	ev->nr      = NR_linkat                                  ;
	ev->args[0] = (__u64)(int)   BPF_CORE_READ(op,old_dfd)   ;
	ev->args[1] = _io_path( ev , BPF_CORE_READ(op,oldpath) ) ;
	ev->args[2] = (__u64)(int)   BPF_CORE_READ(op,new_dfd)   ;
	ev->args[3] = _io_path( ev , BPF_CORE_READ(op,newpath) ) ;
	ev->args[4] =                BPF_CORE_READ(op,flags  )   ;
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}

SEC("fexit/io_symlinkat")
int BPF_PROG( on_io_symlinkat , struct io_kiocb* req ) {
	__u32              jid ;
	struct ebpf_event* ev  = _io_begin(/*out*/&jid) ; if (!ev) return 0 ;
	struct io_link   * op  = (struct io_link*)req   ;                     // symlinkat uses struct io_link in io_uring
	ev->nr      = NR_symlinkat                               ;
	ev->args[0] = 0                                          ;            // target string : not resolved by Record::Symlink , unused in replay
	ev->args[1] = (__u64)(int)   BPF_CORE_READ(op,new_dfd)   ;
	ev->args[2] = _io_path( ev , BPF_CORE_READ(op,newpath) ) ;            // the link path (the file being created)
	_io_emit( jid , ev , BPF_CORE_READ(req,cqe.res) ) ;
	return 0 ;
}
