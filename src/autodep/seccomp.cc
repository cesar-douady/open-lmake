// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <err.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include "disk.hh"
#include "non_portable.hh"
#include "process.hh"

#include "syscall_tab.hh"
#include "record.hh"

#include "seccomp.hh"

using namespace Disk ;

template<class T> static typename ::umap<pid_t,T>::const_iterator _get_ppid( pid_t pid , ::umap<pid_t,T> const& tab ) {
	try {
		/**/                         if (!Record::s_enable_was_modified) return tab.end()      ;
		pid_t ppid = get_ppid(pid) ; if (ppid<=1                       ) return tab.end()      ;
		/**/                                                             return tab.find(ppid) ;
	} catch (::string const&) {
		return tab.end() ;
	}
}

struct PidInfoBase {
	// cxtors & co
	PidInfoBase() = default ;
	PidInfoBase( pid_t pid , Bool3 enable ) : record{ New , enable , pid } {}
	void operator>>(::string& os) const { os << proc_mem ; }                  // NO_COV
	// data
	Record record   ;
	AcFd   proc_mem ;
} ;

// /!\ this function must be malloc free
static void _print_err(const char* reason) {
	const char* msg = ::strerror(errno) ;
	{ int rc = ::write( 2 , reason                   , ::strlen(reason                   ) ) ; (void)rc ; }
	{ int rc = ::write( 2 , " ("                     , ::strlen(" ("                     ) ) ; (void)rc ; }
	{ int rc = ::write( 2 , msg                      , ::strlen(msg                      ) ) ; (void)rc ; }
	{ int rc = ::write( 2 , ") when launching job\n" , ::strlen(") when launching job\n" ) ) ; (void)rc ; }
}

#if HAS_PIDFD
	extern "C" {
		#include <sys/pidfd.h>
	}
#endif

enum class AutodepSeccompKind : uint8_t { Job , Notif } ;

using Kind       = AutodepSeccompKind           ;
using Event      = Epoll<Kind>::Event           ;
using NotifSizes = struct ::seccomp_notif_sizes ;
using Notif      = struct ::seccomp_notif       ;
using NotifResp  = struct ::seccomp_notif_resp  ;
using NotifAddfd = struct ::seccomp_notif_addfd ;

namespace {
	// 1st bad news : seccomp does not tell use when a child dies, moreover it works by tid so we cannot use pidfd to trigger a wakeup upon death.
	// 2nd bad news : there may an arbitrary number of child processes (and 1 open fd per entry), so we cannot let the pid table grow without control.
	// good news    : we can freely reset Record instances, so we can erase any item from pid table without notice (only perf is impacted).
	// synthesis    : we keep a fixed number of entries in pid table and we use a LRU to recycle old entries, exceptionnaly needing to reopen a closed entry.
	struct TidInfoTab {
		static constexpr size_t NCtxs = 300 ;                                                  // we may have this number of open file descriptors
		struct TidInfo : PidInfoBase {
			// cxtors & co
			TidInfo() = default ;
			TidInfo( pid_t tid , Bool3 enable ) : PidInfoBase{tid,enable} , tid{tid} {}
			// data
			pid_t tid = 0/*garbage*/ ;
		} ;
		struct Lru {
			void operator>>(::string& os) const { os << newer<<','<<older ; }                  // NO_COV
			size_t newer = 0/*garbage*/ ;                                                      // circular list : newer of newest is oldest
			size_t older = 0/*.      */ ;                                                      // circular list : older of oldest is newest
		} ;
		// cxtors & co
		TidInfoTab() {
			for( size_t i : iota(NCtxs) ) {                                                    // make circular list
				_lrus[i].newer = i==NCtxs-1   ? size_t(0) : i+1 ;
				_lrus[i].older = i==size_t(0) ? NCtxs-1   : i-1 ;
			}
		}
		void operator>>(::string& os) const {                                                  // START_OF_NO_COV
			::array<pid_t,NCtxs> tids  = {} ; for( auto [tid,idx] : _idxs ) tids[idx] = tid ;
			First                first ;
			bool                 start = true ;
			os << "TidInfoTab(" ;
			for( size_t i=_newest ; start || i!=_newest ; i=_lrus[i].older ) {
				if (tids[i]) os << first("",",")<<(tids[i])<<':'<<i ;
				start = false ;
			}
			os << ')' ;
		}                                                                                      // END_OF_NO_COV
		// services
		TidInfo& emplace(pid_t tid) {
			auto    it_inserted = _idxs.try_emplace(tid)   ;
			size_t& idx         = it_inserted.first->second ;
			if (it_inserted.second) {
				auto it      = _get_ppid( tid , _idxs )                                      ; // get_ppid works on thread-id as well
				Bool3 enable = it==_idxs.end() ? Maybe : No|_infos[it->second].record.enable ;
				idx         = _oldest()        ;
				_idxs.erase(_infos[idx].tid) ;
				_infos[idx] = { tid , enable } ;
			}
			_mk_newest(idx) ;
			return _infos[idx] ;
		}
	private :
		size_t _oldest() {
			return _lrus[_newest].newer ;
		}
		void _mk_newest(size_t i) {
			if (i==_newest) return ;                                                           // simple case
			//
			_lrus[_lrus[i].newer].older = _lrus[i].older ;                                     // remove i from circular list
			_lrus[_lrus[i].older].newer = _lrus[i].newer ;                                     // .
			//
			_lrus[i             ].older = _newest              ;                               // insert i as newest
			_lrus[i             ].newer = _lrus[_newest].newer ;                               // .
			_lrus[_lrus[i].older].newer = i                    ;                               // .
			_lrus[_lrus[i].newer].older = i                    ;                               // .
			//
			_newest = i ;
		}
		// data
		::array<TidInfo,NCtxs> _infos  ;
		::array<Lru    ,NCtxs> _lrus   ;
		::umap<pid_t,size_t>   _idxs   ;
		size_t                 _newest = 0 ;                                                   // it is a circular list, any index is ok as initial value
	} ;
}

// runs in the child, just before execve /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
int/*rc*/ AutodepSeccomp::s_prepare_child(void*) {
	if (::prctl(PR_SET_NO_NEW_PRIVS,1,0/*arg3*/,0/*arg4*/,0/*arg5*/)!=0) { _print_err("cannot prevent privileges") ; return 1 ; }
	//
	AcFd fd = ::syscall( SYS_seccomp , SECCOMP_SET_MODE_FILTER , SECCOMP_FILTER_FLAG_NEW_LISTENER , &SyscallDescr::s_bpf_prog_seccomp ) ;
	if      (!fd            ) { _print_err("cannot set seccomp filter") ; return 1 ; }
	else if (fd.fd==3       )   fd.detach() ;
	else if (::dup2(fd,3)!=3) { _print_err("cannot prepare process"   ) ; return 1 ; } // so fd does not have to be transfered to supervisor (job is not started yet, so 3 is available)
	::raise(SIGSTOP) ;                                                                 // wait until released by supervisor
	::close(3)       ;
	return 0 ;
}

int/*wstatus*/ AutodepSeccomp::process(pid_t child_pid) {
	NotifSizes szs ;
	int        rc  = ::syscall( SYS_seccomp , SECCOMP_GET_NOTIF_SIZES , 0/*flags*/ , &szs ) ; SWEAR( rc==0 ) ;
	SWEAR_PROD( szs.seccomp_notif     >=sizeof(Notif    ) , szs.seccomp_notif     ,sizeof(Notif    ) ) ;
	SWEAR_PROD( szs.seccomp_notif_resp>=sizeof(NotifResp) , szs.seccomp_notif_resp,sizeof(NotifResp) ) ;
	//
	::string recv_bits ( szs.seccomp_notif      , 0 ) ; Notif     & recv_notif  = *reinterpret_cast<Notif     *>(recv_bits .data()) ;
	::string resp_bits ( szs.seccomp_notif_resp , 0 ) ; NotifResp & resp_notif  = *reinterpret_cast<NotifResp *>(resp_bits .data()) ;
	::string addfd_bits( sizeof(NotifAddfd)+64  , 0 ) ; NotifAddfd& addfd_notif = *reinterpret_cast<NotifAddfd*>(addfd_bits.data()) ; // no official size, take some margin
	//
	TidInfoTab tab  ;
	Lock       lock { Record::s_mutex } ; // we have a single thread here, no need to lock record reporting, but Record code checks that lock is taken
	//
	int   wstatus ;
	pid_t pid     = ::waitpid( child_pid , &wstatus , WUNTRACED ) ; SWEAR( pid==child_pid , pid,child_pid ) ; // wait for child to stop
	if (!WIFSTOPPED(wstatus)) {                                                                               // if error/killed before starting
		if (WIFEXITED(wstatus)) SWEAR( WEXITSTATUS(wstatus)!=0 ) ;                                            // if exiting before entering job, must be an error case
		return wstatus ;
	}
	//
	#if HAS_PIDFD                                                                                             // use libc provided wrappers if available
		AcFd pid_fd    { pidfd_open (                     child_pid ,     0/*flags*/ )  } ;
		AcFd notify_fd { pidfd_getfd(                     pid_fd.fd , 3 , 0/*flags*/ )  } ; throw_unless( +notify_fd , "cannot get (",StrErr(),") notify fd from pid ",child_pid) ;
	#else
		AcFd pid_fd    { int(::syscall( SYS_pidfd_open  , child_pid ,     0/*flags*/ )) } ;
		AcFd notify_fd { int(::syscall( SYS_pidfd_getfd , pid_fd.fd , 3 , 0/*flags*/ )) } ; throw_unless( +notify_fd , "cannot get (",StrErr(),") notify fd from pid ",child_pid) ;
	#endif
	//
	Epoll<Kind> epoll { New } ;
	epoll.add_read( pid_fd    , Kind::Job   ) ;
	epoll.add_read( notify_fd , Kind::Notif ) ;
	//
	kill_process( child_pid , SIGCONT ) ;
	//
	for(;;) {
		for( Event const& event : epoll.wait() ) {
			switch (event.data()) {
				case Kind::Job : {
					pid_t pid = ::waitpid( child_pid , &wstatus , WNOHANG ) ;                                 // epoll told us child_pid is dead
					SWEAR( pid==child_pid , pid,child_pid ) ;
					return wstatus ;
				}
				case Kind::Notif : {
					if (!(event.events&EPOLLIN)) {
						SWEAR( event.events==EPOLLHUP , event.events ) ;                                      // there are only 2 cases : notify_fd is ready or we are done
						epoll.del(false/*write*/,notify_fd) ;
						continue ;
					}
					recv_notif = {} ;                                                                         // recv_notif must be full 0 upon calling ioctl
					if ( ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_RECV , &recv_notif )!=0 )
						switch (errno) {
							case ENOENT :                                                                     // this can spuriously happen if process is killed at the time it sends notification
							case EINTR  : continue ;                                                          // .
						DF}
					//
					bool                 is_32   = NonPortable::is_32_from_audit_arch(recv_notif.data.arch) ;
					pid_t                tid     = recv_notif.pid                                           ;
					TidInfoTab::TidInfo& info    = tab.emplace(tid)                                         ;
					int                  syscall = recv_notif.data.nr                                       ;
					//
					#if HAS_32
						SWEAR( size_t(syscall)<(is_32?SyscallDescr::s_tab32.size():SyscallDescr::s_tab.size()) , is_32,syscall,SyscallDescr::s_tab.size(),SyscallDescr::s_tab32.size() ) ;
						SyscallDescr const& descr = is_32 ? SyscallDescr::s_tab32[syscall] : SyscallDescr::s_tab[syscall] ;
					#else
						if (is_32) {
							info.record.report_panic(
								cat("32-bit process ",read_lnk(cat("/proc/",tid,"/exe"))," (",tid,") on 64-bit host with no 32-bit support")
							,	false/*die*/
							) ;
							throw cat("bad arch 32-bit versus 64-bit") ;
						}
						SWEAR( size_t(syscall)<SyscallDescr::s_tab.size() , syscall,SyscallDescr::s_tab.size() ) ;
						SyscallDescr const& descr = SyscallDescr::s_tab[syscall] ;
					#endif
					//
					try {
						// ensure recev_notif is actually an array of uint64_t
						static_assert( sizeof(recv_notif.data.args[0])==sizeof(uint64_t) && ::is_unsigned_v<::remove_reference_t<decltype(recv_notif.data.args[0])>> ) ;
						uint64_t* args = reinterpret_cast<uint64_t*>(recv_notif.data.args) ;
						if (descr.entry) {
							// XXX : call SECCOMP_IOCTL_NOTIF_ID_VALID to validate if tid is still the right one, cf man 2 seccomp_unotify
							::pair<void* /*ctx*/,bool/*refresh*/> ctx_refresh ;
							if (!info.proc_mem) info.proc_mem = AcFd( cat("/proc/",tid,"/mem") , {O_RDWR} ) ;
							try { //!         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								ctx_refresh = descr.entry( info.record , info.proc_mem , args , true/*emulate*/ , descr.comment ) ;
								//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							} catch(::string const& msg) {
								int errno_ = +msg ? EINVAL : ENOSYS ;
									resp_notif = { .id=recv_notif.id , .val=-1 , .error=-errno_ , .flags=0 } ;
								int rc = ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_SEND , &resp_notif ) ;
								throw_unless( rc==0 , "cannot reply to syscall (",StrErr(),") to tid ",tid ) ;
								continue ;
							}
							if (ctx_refresh.first) {
								// because descr.entry is careful at not generating emulation context when operating outside repo,
								// there is no need to take care of /proc/self and /dev/std{in,out,err} not being interpreted identically in tracee and tracer
								//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								int64_t val = descr.exit( ctx_refresh.first , info.record , info.proc_mem , {}/*rc*/ ) ;
								//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								if ( descr.convention==SyscallConvention::Fd && val>=0 ) {
									addfd_notif = {
										.id          = recv_notif.id
									,	.flags       = SECCOMP_ADDFD_FLAG_SEND
									,	.srcfd       = uint32_t(val)
									,	.newfd       = 0
									,	.newfd_flags = uint32_t( ::fcntl(val,F_GETFD)&FD_CLOEXEC ? O_CLOEXEC : 0 )
									} ;
									int rc = ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_ADDFD , &addfd_notif ) ;
									::close(val) ;
									throw_unless( rc>=0 , "cannot inject syscall result (",StrErr(),") to tid ",tid ) ;
								} else {
									resp_notif = { .id=recv_notif.id , .val=val , .error=-errno , .flags=0 } ;
									int rc = ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_SEND , &resp_notif ) ;
									throw_unless( rc==0 , "cannot reply to syscall (",StrErr(),") to tid ",tid ) ;
								}
							} else {
								resp_notif = {
									.id    = recv_notif.id
								,	.val   = 0                                                                // compulsery when continue
								,	.error = 0                                                                // .
								,	.flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE
								} ;
								int rc = ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_SEND , &resp_notif ) ;
								throw_unless( rc==0 , "cannot reply to syscall (",StrErr(),") to tid ",tid ) ;
							}
							if (ctx_refresh.second) info.proc_mem.close() ;
						}
					} catch (::string const& e) {
						info.record.report_trace( cat("unexpected syscall tracing error : ",e) ) ;
					}
				} break ;
			}
		}
	}
}
