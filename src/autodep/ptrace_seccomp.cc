// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <err.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <linux/ptrace.h> // for struct ptrace_syscall_info, must be after sys/ptrace.h to avoid stupid request macro definitions

#include "disk.hh"
#include "process.hh"
#include "trace.hh"

#include "non_portable.hh"
#include "record.hh"

#include "ptrace_seccomp.hh"

#if MUST_UNDEF_PTRACE_MACROS       // must be after utils.hh include
	#undef PTRACE_CONT             // /!\ stupid linux/ptrace.h defines ptrace requests as macros while ptrace expects an enum on some systems
	#undef PTRACE_GET_SYSCALL_INFO // .
	#undef PTRACE_PEEKDATA         // .
	#undef PTRACE_POKEDATA         // .
	#undef PTRACE_SETOPTIONS       // .
	#undef PTRACE_SYSCALL          // .
	#undef PTRACE_TRACEME          // .
#endif

using namespace Disk ;

template<class T> static typename ::umap<pid_t,T>::const_iterator _get_ppid( pid_t pid , ::umap<pid_t,T> const& tab ) {
	try {
		/**/                          if (!Record::s_enable_was_modified) return tab.end()      ;
		pid_t ppid = get_ppid(pid)  ; if (ppid<=1                       ) return tab.end()      ;
		/**/                                                              return tab.find(ppid) ;
	} catch (::string const&) {
		return tab.end() ;
	}
}

struct PidInfoBase {
	// cxtors & casts
	PidInfoBase() = default ;
	PidInfoBase( pid_t pid , Bool3 enable ) : record{ New , enable , pid } {}
	// accesses
	void operator>>(::string& os) const { os << proc_mem ; } // NO_COV
	// data
	Record record   ;
	AcFd   proc_mem ;
} ;

namespace AutodepPtrace {

	struct PidInfo : PidInfoBase {
		// cxtors & casts
		using PidInfoBase::PidInfoBase ;
		// services
		void event( pid_t , int wstatus ) ;
		// data
		void* ctx     = nullptr ;
		long  syscall = 0       ;
		Bool3 is_32   = Maybe   ;
	} ;

	// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
	int/*rc*/ prepare_child(void*) {
		SyscallDescr::BpfProg const& bp = SyscallDescr::s_bpf_prog_ptrace ;
		// /!\ despite man page saying nothing, missing args must be 0 for prctl
		if (::ptrace(PTRACE_TRACEME,0/*pid*/,0/*addr*/,0/*data*/                          )!=0) { Fd::Stderr.write(cat("cannot set up ptrace ("     ,StrErr(),") when launching job\n")) ; return 1 ; }
		if (::prctl (PR_SET_NO_NEW_PRIVS,1                  ,0/*arg3*/,0/*arg4*/,0/*arg5*/)!=0) { Fd::Stderr.write(cat("cannot prevent privileges (",StrErr(),") when launching job\n")) ; return 1 ; }
		if (::prctl (PR_SET_SECCOMP     ,SECCOMP_MODE_FILTER,&bp      ,0/*.   */,0/*.   */)!=0) { Fd::Stderr.write(cat("cannot set up seccomp ("    ,StrErr(),") when launching job\n")) ; return 1 ; }
		::raise(SIGSTOP) ; // wait until released by supervisor
		return 0 ;
	}

	void PidInfo::event( pid_t pid , int wstatus ) {
		int sig   = WSTOPSIG(wstatus) ;
		int event = wstatus>>16       ;
		if (sig!=(SIGTRAP|0x80))
			switch (event) {
				case PTRACE_EVENT_SECCOMP : SWEAR_PROD(!ctx) ;                       break            ; // syscall enter
				case 0                    : if        (sig==SIGTRAP)       sig = 0 ; goto NextSyscall ; // other stop reasons, wash SIGTRAP, ignore
				default                   : SWEAR_PROD(sig==SIGTRAP,sig) ; sig = 0 ; goto NextSyscall ; // ignore other events
			}
		try {
			sig = 0 ;
			#if HAS_PTRACE_GET_SYSCALL_INFO                                                             // use portable calls if implemented
				struct ::ptrace_syscall_info syscall_info ;
				if ( ::ptrace( PTRACE_GET_SYSCALL_INFO , pid , sizeof(struct ptrace_syscall_info) , &syscall_info )<=0 )
					throw cat("cannot get syscall info (",StrErr(),") from ",pid) ;
			#endif
			if (!ctx) {
				// syscall enter
				#if HAS_PTRACE_GET_SYSCALL_INFO
					SWEAR_PROD( syscall_info.op==PTRACE_SYSCALL_INFO_SECCOMP , syscall_info.op ) ;
					if (is_32==Maybe) is_32   = No|(NonPortable::is_32_from_audit_arch(syscall_info.arch)) ;
					/**/              syscall = syscall_info.seccomp.nr                                    ;
				#else
					if (is_32==Maybe) is_32   = No|NonPortable::ptrace_is_32(pid) ;
					/**/              syscall = NonPortable::ptrace_get_nr(pid)   ;                     // use non-portable calls if portable accesses are not implemented
				#endif
				#if !HAS_32
					if (is_32==Yes) {
						record.report_panic(
							cat("32-bit process ",read_lnk(cat("/proc/",pid,"/exe"))," (",pid,") on 64-bit host with no 32-bit support")
						,	false/*die*/
						) ;
						throw cat("bad arch 32-bit versus 64-bit") ;
					}
				#endif
				#ifdef _X32_SYSCALL_BIT
					syscall &= ~__X32_SYSCALL_BIT ;                                                     // we only look at char*, so mode x32 has no impact
				#endif
				SWEAR_PROD( syscall>=0 && syscall<SyscallDescr::NSyscalls , syscall ) ;                 // bad syscall should have been filtered out by BPF
				#if HAS_32
					SyscallDescr const& descr = is_32==Yes ? SyscallDescr::s_tab32[syscall] : SyscallDescr::s_tab[syscall] ;
				#else
					SyscallDescr const& descr =                                               SyscallDescr::s_tab[syscall] ;
				#endif
				SWEAR_PROD( descr.entry , is_32,syscall ) ;                                             // syscall should have been filtered out by BPF
				#if HAS_PTRACE_GET_SYSCALL_INFO                                                         // use portable calls if implemented
					// ensure args is actually an array of uint64_t although one is declared as unsigned long and the other is unsigned long long
					auto& sc_args = syscall_info.seccomp.args ;
					static_assert( sizeof(sc_args[0])==sizeof(uint64_t) && ::is_unsigned_v<::remove_reference_t<decltype(sc_args[0])>> ) ;
					uint64_t* args = reinterpret_cast<uint64_t*>(sc_args) ;
				#else
					::array<uint64_t,6> arg_array = NonPortable::ptrace_get_args(pid) ;                 // use non-portable calls if portable accesses are not implemented
					uint64_t*           args      = arg_array.data()                  ;                 // we need a variable to hold the data while we pass the pointer
				#endif
				if (!proc_mem) proc_mem = AcFd( cat("/proc/",pid,"/mem") , {O_RDWR} ) ;
				bool refresh = false ;
				//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				tie(ctx,refresh) = descr.entry( record , proc_mem , args , false/*emulate*/ , descr.comment ) ;
				//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (refresh) {
					proc_mem.close() ;
					is_32 = Maybe ;
				}
				if (!descr.exit) SWEAR_PROD( !ctx , syscall ) ;                                         // no need for a context if we are not called at exit
			} else {
				// syscall exit
				#if HAS_PTRACE_GET_SYSCALL_INFO
					SWEAR_PROD( syscall_info.op==PTRACE_SYSCALL_INFO_EXIT ) ;
					int64_t res = syscall_info.exit.rval ;
				#else
					int64_t res = NonPortable::ptrace_get_res(pid) ;                                    // use non-portable calls if portable accesses are not implemented
				#endif
				int64_t old_rc = res ; if (res<0) { old_rc = -1 ; errno = -res ; }                      // we do not emulate, handling errno is only for magic readlink
				::pair<int64_t,int> rc_errno ;
				#if HAS_32
					SyscallDescr const& descr = is_32==Yes ? SyscallDescr::s_tab32[syscall] : SyscallDescr::s_tab[syscall] ;
				#else
					SyscallDescr const& descr =                                               SyscallDescr::s_tab[syscall] ;
				#endif
				SWEAR_PROD( descr.exit , is_32,syscall ) ;
				//          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				rc_errno = descr.exit( ctx , record , proc_mem , old_rc ) ;
				//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				ctx = nullptr ;
				if (rc_errno.first<0   ) { SWEAR( rc_errno.first==-1 , syscall,rc_errno ) ; rc_errno.first = -rc_errno.second ; }
				if (rc_errno.first!=res)
				NonPortable::ptrace_set_res( pid , rc_errno.first ) ;
			}
		} catch (::string const& e) {
			Trace("event","process_is_dead",e) ;
		}
	NextSyscall :
		long rc = ::ptrace( ctx?PTRACE_SYSCALL:PTRACE_CONT , pid , 0/*addr*/ , sig ) ;
		throw_unless( rc==0 , "cannot continue (",StrErr(),") process ",pid ) ;
	}

	int/*wstatus*/ process(pid_t child_pid) {
		static constexpr int Options =
			PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK
		|	PTRACE_O_TRACESYSGOOD                                              // necessary to have a correct syscall_info.op field
		|	PTRACE_O_TRACESECCOMP
		|	PTRACE_O_EXITKILL                                                  // ensure no process is left stopped, even if alive at end of job
		;
		Trace trace("AutodepPtrace::process",child_pid) ;
		//
		Lock                  lock    { Record::s_mutex } ;                    // we have a single thread here, no need to lock record reporting, but Record code checks that lock is taken
		::umap<pid_t,PidInfo> tab     ;
		int                   wstatus ;
		pid_t                 pid     = ::wait(&wstatus)  ;                    // wait for child to stop
		SWEAR( pid==child_pid      , pid,child_pid ) ;                         // job has not started yet, only a single child exists
		SWEAR( WIFSTOPPED(wstatus) , pid,wstatus   ) ;
		{ long rc = ::ptrace(PTRACE_SETOPTIONS,pid,0/*addr*/,Options  ) ; swear_prod( rc==0 , cat("cannot set ptrace options (",StrErr(),") for process ",pid                        )) ; }
		{ long rc = ::ptrace(PTRACE_CONT      ,pid,0/*.   */,0/*data*/) ; swear_prod( rc==0 , cat("cannot continue ("          ,StrErr(),") process "    ,pid," after setting ptrace")) ; }
		//
		while( (pid=::wait(&wstatus))>1 ) {
			auto     it     = _get_ppid( pid , tab )                              ;
			Bool3    enable = it==tab.end() ? Maybe : No|it->second.record.enable ;
			PidInfo& info   = tab.try_emplace( pid , pid,enable ).first->second   ;
			if (WIFSTOPPED(wstatus)) {
				info.event(pid,wstatus) ;
			} else {
				SWEAR_PROD( WIFEXITED(wstatus) || WIFSIGNALED(wstatus) , "unrecognized wstatus ",wstatus," for pid ",pid ) ;
				tab.erase(pid) ;
				if (pid==child_pid) {
					trace("done",wstatus) ;
					return wstatus ;
				}
			}
		}
		fail_prod("process",child_pid,"cannot be waited for (",StrErr(),')') ; // NO_COV
	}

}

#if CAN_AUTODEP_SECCOMP

	#if HAS_PIDFD
		extern "C" {
			#include <sys/pidfd.h>
		}
	#endif

	static constexpr size_t NCtxs = 300 ; // we may have this number of open file descriptors

	enum class AutodepSeccompKind : uint8_t { Job , Notif } ;

	namespace AutodepSeccomp {

		using Kind       = AutodepSeccompKind           ;
		using Event      = Epoll<Kind>::Event           ;
		using NotifSizes = struct ::seccomp_notif_sizes ;
		using Notif      = struct ::seccomp_notif       ;
		using NotifResp  = struct ::seccomp_notif_resp  ;
		using NotifAddfd = struct ::seccomp_notif_addfd ;

		struct TidInfo : PidInfoBase {
			// cxtors & casts
			TidInfo() = default ;
			TidInfo( pid_t tid , Bool3 enable ) : PidInfoBase{tid,enable} , tid{tid} {}
			// data
			pid_t tid = 0/*garbage*/ ;
		} ;

		// 1st bad news : seccomp does not tell use when a child dies, moreover it works by tid so we cannot use pidfd to trigger a wakeup upon death.
		// 2nd bad news : there may an arbitrary number of child processes (and 1 open fd per entry), so we cannot let the pid table grow without control.
		// good news    : we can freely reset Record instances, so we can erase any item from pid table without notice (only perf is impacted).
		// synthesis    : we keep a fixed number of entries in pid table and we use a LRU to recycle old entries, exceptionnaly needing to reopen a closed entry.
		struct TidInfoTab {
			struct Lru {
				void operator>>(::string& os) const { os << newer<<','<<older ; }                  // NO_COV
				size_t newer = 0/*garbage*/ ;                                                      // circular list : newer of newest is oldest
				size_t older = 0/*.      */ ;                                                      // circular list : older of oldest is newest
			} ;
			// cxtors & casts
			TidInfoTab() {
				for( size_t i : iota(NCtxs) ) {                                                    // make circular list
					_lrus[i].newer = i==NCtxs-1   ? size_t(0) : i+1 ;
					_lrus[i].older = i==size_t(0) ? NCtxs-1   : i-1 ;
				}
			}
			// accesses
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

		static NotifSizes _seccomp_get_sizes() {
			NotifSizes res ;
			int        rc  = ::syscall( SYS_seccomp , SECCOMP_GET_NOTIF_SIZES , 0/*flags*/ , &res ) ; SWEAR( rc==0 ) ;
			return res ;
		}

		static AcFd _seccomp_set_filter(SyscallDescr::BpfProg const& bp) {
			AcFd res = ::syscall( SYS_seccomp , SECCOMP_SET_MODE_FILTER , SECCOMP_FILTER_FLAG_NEW_LISTENER , &bp ) ; SWEAR( +res ) ;
			return res ;
		}

		// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
		int/*rc*/ prepare_child(void*) {
			if (::prctl(PR_SET_NO_NEW_PRIVS,1,0/*arg3*/,0/*arg4*/,0/*arg5*/)!=0) {
				Fd::Stderr.write(cat("cannot prevent privileges (",StrErr(),") when launching job\n")) ;
				return 1 ;
			}
			//
			AcFd fd = _seccomp_set_filter(SyscallDescr::s_bpf_prog_seccomp) ;
			if (fd.fd==3)
				fd.detach() ;
			else
				if (::dup2(fd,3)!=3) { // so fd does not have to be transfered to supervisor (job is not started yet, so 3 is available)
					Fd::Stderr.write(cat("cannot prepare process (",StrErr(),") when launching job\n")) ;
					return 1 ;
				}
			::raise(SIGSTOP) ;         // wait until released by supervisor
			::close(3) ;
			return 0 ;
		}

		int/*wstatus*/ process(pid_t child_pid) {
			NotifSizes szs = _seccomp_get_sizes() ;
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
			pid_t pid     = ::waitpid( child_pid , &wstatus , WUNTRACED ) ; SWEAR( pid==child_pid      , pid,child_pid ) ; // wait for child to stop
			/**/                                                            SWEAR( WIFSTOPPED(wstatus) , pid,wstatus   ) ;
			//
			#if HAS_PIDFD                                                                                                  // use libc provided wrappers if available
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
			for (;;) {
				for( Event const& event : epoll.wait() ) {
					switch (event.data()) {
						case Kind::Job : {
							pid_t pid = ::waitpid( child_pid , &wstatus , WNOHANG ) ;                                      // epoll told us child_pid is dead
							SWEAR( pid==child_pid , pid,child_pid ) ;
							return wstatus ;
						}
						case Kind::Notif : {
							if (!(event.events&EPOLLIN)) {
								SWEAR( event.events==EPOLLHUP , event.events ) ;                                           // there are only 2 cases : notify_fd is ready or we are done
								epoll.del(false/*write*/,notify_fd) ;
								continue ;
							}
							recv_notif = {} ;                                                                              // recv_notif must be full 0 upon calling ioctl
							if ( ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_RECV , &recv_notif )!=0 ) FAIL() ;
							//
							bool     is_32   = NonPortable::is_32_from_audit_arch(recv_notif.data.arch) ;
							pid_t    tid     = recv_notif.pid                                           ;
							TidInfo& info    = tab.emplace(tid)                                         ;
							int      syscall = recv_notif.data.nr                                       ;
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
								// ensure entry_info is actually an array of uint64_t although one is declared as unsigned long and the other is unsigned long long
								static_assert( sizeof(recv_notif.data.args[0])==sizeof(uint64_t) && ::is_unsigned_v<::remove_reference_t<decltype(recv_notif.data.args[0])>> ) ;
								uint64_t* args = reinterpret_cast<uint64_t*>(recv_notif.data.args) ;
								if (descr.entry) {
									// XXX : call SECCOMP_IOCTL_NOTIF_ID_VALID to validate if tid is still the right one, cf man 2 seccomp_unotify
									if (!info.proc_mem) info.proc_mem = AcFd( cat("/proc/",tid,"/mem") , {O_RDWR} ) ;
									//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
									auto [ctx,refresh] = descr.entry( info.record , info.proc_mem , args , true/*emulate*/ , descr.comment ) ;
									//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
									if (ctx) {
										// because descr.entry is careful at not generating emulation context when operating outside repo,
										// there is no need to take care of /proc/self and /dev/std{in,out,err} not being interpreted identically in tracee and tracer
										//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
										auto [val,e/*errno*/] = descr.exit( ctx , info.record , info.proc_mem , {}/*rc*/ ) ;
										//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
										if ( descr.return_fd && val>=0 ) {
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
											resp_notif = { .id=recv_notif.id , .val=val , .error=-e , .flags=0 } ;
											int rc = ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_SEND , &resp_notif ) ;
											throw_unless( rc==0 , "cannot reply to syscall (",StrErr(),") to tid ",tid ) ;
										}
									} else {
										resp_notif = {
											.id    = recv_notif.id
										,	.val   = 0                                                                     // compulsery when continue
										,	.error = 0                                                                     // .
										,	.flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE
										} ;
										int rc = ::ioctl( notify_fd , SECCOMP_IOCTL_NOTIF_SEND , &resp_notif ) ;
										throw_unless( rc==0 , "cannot reply to syscall (",StrErr(),") to tid ",tid ) ;
									}
									if (refresh) info.proc_mem.close() ;
								}
							} catch (::string const& e) {
								info.record.report_trace( cat("unexpected syscall tracing error : ",e) ) ;
							}
						} break ;
					}
				}
			}
		}

	}
#endif
