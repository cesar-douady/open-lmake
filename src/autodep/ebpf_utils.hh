// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "sys/time.h"

#include <mutex>

#include "src/autodep/ebpf.skel.h" // auto-generated, so must include dir so the -M flag works

#include "ebpf_daemon.hh"
#include "ebpf_event.h"

static constexpr uint32_t RingBufSz = 4u<<20 ; // comfortable : events are small as paths are relative to roots and uninteresting accesses are filtered out in the kernel ...
/**/                                           // ... /!\ this memory is allocated in the kernel for each running job

extern struct ebpf* g_skel      ;
extern std::mutex   g_reg_mutex ; // root registration and syscall table are unique resources
extern int          g_log_fd    ;

inline void vlog( bool with_errno , bool with_nl , const char* fmt , va_list ap ) {
	static constexpr size_t N = 1024 ; // buf size
	int            en      = errno ;
	struct timeval tv      ;         ::gettimeofday(&tv,nullptr/*timezone*/)  ;
	struct tm      tm      ;         ::localtime_r(&tv.tv_sec,&tm)            ;
	char           ts [32] ;         ::strftime(ts,sizeof(ts),"%H:%M:%S",&tm) ;
	char           buf[N ] ;
	size_t         i       = 0     ;
	//
	if (g_log_fd<0) {
		g_log_fd = ::open( EbpfDaemon::get_file_name("log").c_str() , O_WRONLY|O_APPEND|O_CREAT , 0020/*mode*/ ) ;
		if (g_log_fd<0) {
			fprintf(stderr,"cannot open log file : %s\n",::strerror(errno)) ;
			exit(2) ;
		}
	}
	//
	/**/              i += ::snprintf ( buf+i , N-i , "lmake_bpfd %s.%03d : ",ts,int(tv.tv_usec/1000) ) ; if (i>=N) return ;
	/**/              i += ::vsnprintf( buf+i , N-i , fmt                    ,ap                      ) ; if (i>=N) return ;
	if (with_errno) { i += ::snprintf ( buf+i , N-i , " : %s"                ,::strerror(en)          ) ; if (i>=N) return ; }
	if (with_nl   ) { i += ::snprintf ( buf+i , N-i , "\n"                                            ) ; if (i>=N) return ; }
	::write( g_log_fd , buf , i ) ;
}
//                                                                                   with_errno with_nl
inline void log      ( const char* fmt , ... ) { va_list ap ; va_start(ap,fmt) ; vlog( false   , true , fmt , ap ) ; va_end(ap) ; }
inline void log_errno( const char* fmt , ... ) { va_list ap ; va_start(ap,fmt) ; vlog( true    , true , fmt , ap ) ; va_end(ap) ; }

inline void reset_log() {
	::ftruncate( g_log_fd , 0/*lenght*/ ) ;
}

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
		std::lock_guard<std::mutex> lock     { g_reg_mutex }                                                ;
		int                         fd       = bpf_map__fd(g_skel->maps.root_reg)                           ;
		uint32_t                    zero     = 0                                                            ;
		RootReg                     rr       { .job_id=job_id , .idx=idx , .armed=1 , .done=0 , .dentry=0 } ;
		char                        path[64]                                                                ; snprintf(path,sizeof(path),"/proc/self/fd/%d",dir_fd) ;
		//
		if (::bpf_map_update_elem(fd,&zero,&rr,BPF_ANY)!=0) {
			log_errno("job %u : cannot update map",job_id) ;
			return false ;
		}
		::syscall( SYS_faccessat , EBPF_ROOT_FD , path , F_OK ) ;                                                       // dir fd is ignored as path is absolute, it is just a marker
		if (::bpf_map_lookup_elem(fd,&zero,&rr)!=0) { log_errno("job %u : cannot lookup map",job_id) ; return false ; }
		if (!rr.done                              ) { log_errno("job %u : cannot retrieve"  ,job_id) ; return false ; }
		root_keys.push_back({ .dentry=rr.dentry , .job_id=job_id , .pad=0 }) ;
		return true ;
	}
	// create the ring buffer of job and register it in the kernel, root_fds are those of hello
	// in case of error, caller must release job, as when it is done
	bool/*ok*/ mk_job(EbpfDaemon::Hello const& hello) {
		rb_fd = ::bpf_map_create( BPF_MAP_TYPE_RINGBUF , nullptr/*map_name*/ , 0/*key_size*/ , 0/*value_size*/ , RingBufSz , nullptr/*opts*/ ) ;
		if (rb_fd<0) {
			log_errno("job %u : cannot create ring buffer",job_id) ;
			return false ;
		}
		JobCfg cfg { .pidns_dev=hello.pidns_dev , .pidns_ino=hello.pidns_ino , .seq=0 , .lost=0 , .deps_in_system=hello.deps_in_system } ;
		if (
			::bpf_map_update_elem( bpf_map__fd(g_skel->maps.job_rbs) , &job_id , &rb_fd , BPF_ANY )!=0
		||	::bpf_map_update_elem( bpf_map__fd(g_skel->maps.job_cfg) , &job_id , &cfg   , BPF_ANY )!=0
		) {
			log_errno("job %u : cannot register",job_id) ;
			return false ;
		}
		if (hello.n_roots!=root_fds.size()) { log("job %u : internal error (size mismatch %u!=%u)",job_id,hello.n_roots,root_fds.size()) ; return false ; }
		if (hello.n_roots>EBPF_MAX_ROOTS  ) { log("job %u : internal error (root overflow)"       ,job_id                              ) ; return false ; }
		for( uint32_t i=0 ; i<root_fds.size() ; i++ )
			if (!register_root( i , root_fds[i] )) { log("job %u : cannot register root %u",job_id,i) ; return false ; }
		return true ;
	}
	// data
	int                  conn      = -1 ;
	int                  rb_fd     = -1 ;
	uint32_t             job_id    =  0 ;
	std::vector<int>     root_fds  = {} ; // roots are identified by their dentry in the kernel, keep them open so dentries stay valid
	std::vector<RootKey> root_keys = {} ; // what has been registered in the kernel
} ;
