// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <err.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <linux/ptrace.h> // for struct ptrace_syscall_info, must be after sys/ptrace.h to avoid stupid request macro definitions

#include "disk.hh"
#include "non_portable.hh"
#include "process.hh"
#include "trace.hh"

#include "syscall_tab.hh"
#include "record.hh"

#include "ptrace.hh"

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
	PidInfoBase( pid_t pid , Bool3 enable ) : record{New,enable,pid} {}
	void operator>>(::string& os) const { os << proc_mem ; }            // NO_COV
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

// runs in the child, just before execve /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
int/*rc*/ AutodepPtrace::s_prepare_child(void*) {
	SyscallDescr::BpfProg const& bp = SyscallDescr::s_bpf_prog_ptrace ;
	// /!\ despite man page saying nothing, missing args must be 0 for prctl
	if (::ptrace(PTRACE_TRACEME,0/*pid*/,0/*addr*/,0/*data*/                          )!=0) { _print_err("cannot set up ptrace"     ) ; return 1 ; }
	if (::prctl (PR_SET_NO_NEW_PRIVS,1                  ,0/*arg3*/,0/*arg4*/,0/*arg5*/)!=0) { _print_err("cannot prevent privileges") ; return 1 ; }
	if (::prctl (PR_SET_SECCOMP     ,SECCOMP_MODE_FILTER,&bp      ,0/*.   */,0/*.   */)!=0) { _print_err("cannot set up seccomp"    ) ; return 1 ; }
	::raise(SIGSTOP) ;                                                                                                                               // wait until released by supervisor
	return 0 ;
}

namespace {

	struct PtracePidInfo : PidInfoBase {
		static constexpr Bool3 Is32Dflt = HAS_32 ? Maybe : sizeof(void*)==8 ? No : Yes ;
		// cxtors & co
		using PidInfoBase::PidInfoBase ;
		// services
		void event( pid_t , int wstatus ) ;
		// data
		void* ctx     = nullptr  ;
		long  syscall = 0        ;
		Bool3 is_32   = Is32Dflt ;
	} ;

	void PtracePidInfo::event( pid_t pid , int wstatus ) {
		static bool ctx_force_inval = false ;                                                           // unused, just a marker for ctx
		static bool ctx_force_nosys = false ;                                                           // unused, just a marker for ctx
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
				#ifdef __X32_SYSCALL_BIT
					syscall &= ~__X32_SYSCALL_BIT ;                                                     // we only look at char* args, so mode x32 has no impact
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
				try { //!              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					tie(ctx,refresh) = descr.entry( record , proc_mem , args , false/*emulate*/ , descr.comment ) ;
					//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (!descr.exit) SWEAR_PROD( !ctx , syscall ) ;                                     // no need for a context if we are not called at exit
				} catch(::string const& msg) {
					if (+msg) ctx = &ctx_force_inval ;
					else      ctx = &ctx_force_nosys ;
					NonPortable::ptrace_set_nr( pid , -1/*illegal syscall*/ ) ;                         // prevent execution of asked syscall
				}
				if (refresh) {
					proc_mem.close() ;
					is_32 = Is32Dflt ;
				}
			} else {
				// syscall exit
				int64_t       orig_rc = 0 ;
				int64_t/*rc*/ rc      ;
				if      (ctx==&ctx_force_inval) rc = -EINVAL ;
				else if (ctx==&ctx_force_nosys) rc = -ENOSYS ;
				else {
					#if HAS_PTRACE_GET_SYSCALL_INFO
						SWEAR_PROD( syscall_info.op==PTRACE_SYSCALL_INFO_EXIT ) ;
						orig_rc = syscall_info.exit.rval ;
					#else
						orig_rc = NonPortable::ptrace_get_res(pid) ;                                    // use non-portable calls if portable accesses are not implemented
					#endif
					#if HAS_32
						SyscallDescr const& descr = is_32==Yes ? SyscallDescr::s_tab32[syscall] : SyscallDescr::s_tab[syscall] ;
					#else
						SyscallDescr const& descr =                                               SyscallDescr::s_tab[syscall] ;
					#endif
					if (orig_rc<0) { rc = -1      ; errno = -orig_rc ; }
					else             rc = orig_rc ;
					SWEAR_PROD( descr.exit , is_32,syscall ) ;
					//   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					rc = descr.exit( ctx , record , proc_mem , rc ) ;
					//   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (rc==-1) rc = -errno ;
				}
				if (rc!=orig_rc) NonPortable::ptrace_set_res( pid , rc ) ;
				ctx = nullptr ;
			}
		} catch (::string const& e) {
			Trace("event","process_is_dead",e) ;
		}
	NextSyscall :
		long rc = ::ptrace( ctx?PTRACE_SYSCALL:PTRACE_CONT , pid , 0/*addr*/ , sig ) ;
		throw_unless( rc==0||errno==ESRCH , "cannot continue (",StrErr(),") process ",pid ) ;
	}

}

int/*wstatus*/ AutodepPtrace::process(pid_t child_pid) {
	static constexpr int Options =
		PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK
	|	PTRACE_O_TRACESYSGOOD                                              // necessary to have a correct syscall_info.op field
	|	PTRACE_O_TRACESECCOMP
	|	PTRACE_O_EXITKILL                                                  // ensure no process is left stopped, even if alive at end of job
	;
	Trace trace("AutodepPtrace::process",child_pid) ;
	//
	Lock                        lock    { Record::s_mutex } ;              // we have a single thread here, no need to lock record reporting, but Record code checks that lock is taken
	::umap<pid_t,PtracePidInfo> tab     ;
	int                         wstatus ;
	pid_t                       pid     = ::wait(&wstatus)  ;              // wait for child to stop
	SWEAR( pid==child_pid      , pid,child_pid ) ;                         // job has not started yet, only a single child exists
	#if HAS_SECCOMP
		throw_unless( WIFSTOPPED(wstatus) , "cannot ptrace, consider using autodep='seccomp' instead" ) ;
	#else
		throw_unless( WIFSTOPPED(wstatus) , "cannot ptrace"                                         ) ;
	#endif
	{ long rc = ::ptrace(PTRACE_SETOPTIONS,pid,0/*addr*/,Options  ) ; swear_prod( rc==0 , cat("cannot set ptrace options (",StrErr(),") for process ",pid                        )) ; }
	{ long rc = ::ptrace(PTRACE_CONT      ,pid,0/*.   */,0/*data*/) ; swear_prod( rc==0 , cat("cannot continue ("          ,StrErr(),") process "    ,pid," after setting ptrace")) ; }
	//
	while( (pid=::wait(&wstatus))>1 ) {
		auto           it     = _get_ppid( pid , tab )                              ;
		Bool3          enable = it==tab.end() ? Maybe : No|it->second.record.enable ;
		PtracePidInfo& info   = tab.try_emplace( pid , pid,enable ).first->second   ;
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
