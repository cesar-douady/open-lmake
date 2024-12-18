// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <err.h>
#include <sys/wait.h>

#include "disk.hh"
#include "trace.hh"

#include "record.hh"
#include "syscall_tab.hh"

#include "ptrace.hh"

#if !HAS_PTRACE
	#error cannot compile ptrace.cc without HAS_PTRACE
#endif


#if HAS_PTRACE_GET_SYSCALL_INFO
	#include <linux/audit.h>    // AUDIT_ARCH_*
#endif

#if HAS_SECCOMP
	::scmp_filter_ctx AutodepPtrace::s_scmp = ::seccomp_init(SCMP_ACT_ALLOW) ;
#endif

using namespace Disk ;

// When tracing a child, initially, the child will run till first signal and only then will follow the specified seccomp filter.
// If a traced system call (as per the seccomp filter) is done before, it will fail.
// This typically happen with the initial exec call.
// Thus, child must send a first signal that parent must ignore.
static constexpr int FirstSignal = SIGTRAP ;

static constexpr auto StopAtNextSyscallEntry = HAS_SECCOMP ? PTRACE_CONT : PTRACE_SYSCALL ; // if using seccomp, we can skip all non-watched syscalls (this is the whole purpose of it)
static constexpr auto StopAtSyscallExit      =                             PTRACE_SYSCALL ;

struct PidInfo {
	// static data
	static ::umap<pid_t,PidInfo> s_tab ;
	// cxtors & casts
	PidInfo(pid_t pid) : record{New,pid} {}
	// data
	Record record        ;
	size_t idx           = 0       ;
	void*  ctx           = nullptr ;
	bool   has_exit_proc = false   ;
	bool   on_going      = false   ;
} ;
::umap<pid_t,PidInfo > PidInfo::s_tab ;

void AutodepPtrace::s_init(AutodepEnv const& ade) {
	Record::s_autodep_env(ade) ;
	#if HAS_SECCOMP
		// XXX : find a way to load seccomp rules that support 32 bits and 64 bits exe's
		static SyscallDescr::Tab const& s_tab = SyscallDescr::s_tab ;
		// prepare seccomp filter outside s_prepare_child as this might very well call malloc
		bool ignore_stat = ade.ignore_stat && ade.lnk_support!=LnkSupport::Full ;                         // if full link support, we need to analyze uphill dirs
		swear_prod( ::seccomp_attr_set( s_scmp , SCMP_FLTATR_CTL_OPTIMIZE , 2 )==0 ) ;
		for( long syscall : iota(SyscallDescr::NSyscalls) ) {
			SyscallDescr const& descr = s_tab[syscall] ;
			if ( !descr || !descr.entry       ) continue ;                                                // descr is not allocated
			if ( descr.is_stat && ignore_stat ) continue ;                                                // non stat-like access are always needed
			swear_prod( ::seccomp_rule_add( s_scmp , SCMP_ACT_TRACE(0/*ret_data*/) , syscall , 0 )==0 ) ;
		}
	#endif
}

void AutodepPtrace::init(pid_t cp) {
	child_pid = cp ;
	//
	int   wstatus ;
	pid_t pid     = ::wait(&wstatus) ;                                         // first signal is only there to start tracing as we are initially traced to next signal
	if (pid!=child_pid) return ;                                               // child_pid will be waited for in process
	long rc = ::ptrace( PTRACE_SETOPTIONS , pid , 0/*addr*/ ,
		(HAS_SECCOMP?PTRACE_O_TRACESECCOMP:0)
	|	PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK
	|	PTRACE_O_TRACESYSGOOD                                                  // necessary to have a correct syscall_info.op field
	) ;
	if (rc!=0) return ;                                                        // child_pid will be waited for in process
	SWEAR( WIFSTOPPED(wstatus) && WSTOPSIG(wstatus)==FirstSignal , wstatus ) ;
	rc = ::ptrace( StopAtNextSyscallEntry , pid , 0/*addr*/ , 0/*data*/ ) ; SWEAR(rc==0,rc,int(errno)) ;
	// if pid disappeared, child_pid will be waited for in process, we have nothing to do
}

// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
int/*rc*/ AutodepPtrace::s_prepare_child(void*) {
	#if HAS_SECCOMP
		int rc = ::seccomp_load(s_scmp) ;
		if (rc!=0) ::_exit(+Rc::System) ;
	#endif
	::ptrace( PTRACE_TRACEME , 0/*pid*/ , 0/*addr*/ , 0/*data*/ ) ;
	kill_self(FirstSignal) ;                                        // cannot call a traced syscall until a signal is received as we are initially traced till the next signal
	return 0 ;
}

int/*wstatus*/ AutodepPtrace::process() {
	Trace trace("AutodepPtrace::process") ;
	int   wstatus ;
	pid_t pid     ;
	while( (pid=::wait(&wstatus))>1 )
		if (_changed(pid,wstatus)) {  // may update wstatus
			trace("done",wstatus) ;
			return wstatus ;
		}
	fail_prod("process",child_pid,"did not exit nor was signaled") ;
}

bool/*done*/ AutodepPtrace::_changed( pid_t pid , int& wstatus ) {
	PidInfo& info  = PidInfo::s_tab.try_emplace(pid,pid).first->second ;
	if (WIFSTOPPED(wstatus)) {
		try {
			int sig   = WSTOPSIG(wstatus) ;
			int event = wstatus>>16       ;
			if (sig==(SIGTRAP|0x80)) goto DoSyscall ;                                             // if HAS_SECCOMP => syscall exit, else syscall enter or exit
			switch (event) {
				#if HAS_SECCOMP
					case PTRACE_EVENT_SECCOMP : SWEAR(!info.on_going) ; goto DoSyscall ;          // syscall enter
				#endif
				case 0  : if (sig==SIGTRAP)         sig = 0 ; goto NextSyscall ;                  // other stop reasons, wash spurious SIGTRAP, ignore
				default : SWEAR(sig==SIGTRAP,sig) ; sig = 0 ; goto NextSyscall ;                  // ignore other events
			}
		DoSyscall :
			{	static SyscallDescr::Tab const& tab = SyscallDescr::s_tab ;
				sig = 0 ;
				uint32_t word_sz = 0/*garbage*/ ;
				#if HAS_PTRACE_GET_SYSCALL_INFO                                                   // use portable calls if implemented
					struct ptrace_syscall_info syscall_info ;
					if ( ::ptrace( PTRACE_GET_SYSCALL_INFO , pid , sizeof(struct ptrace_syscall_info) , &syscall_info )<=0 ) throw "cannot get syscall info from "s+pid ;
					switch (syscall_info.arch) {
						case AUDIT_ARCH_I386    :
						case AUDIT_ARCH_ARM     : word_sz = 32 ; break ;
						case AUDIT_ARCH_X86_64  :
						case AUDIT_ARCH_AARCH64 : word_sz = 64 ; break ;
						default                 : FAIL("unexpected arch",syscall_info.arch) ;
					}
				#else                                                                             // XXX : try to find a way to determine tracee word size
					word_sz = NpWordSz ;                                                          // waiting for a way to determine tracee word size, assume it is the same as us
				#endif
				if (!info.on_going) {
					#if HAS_PTRACE_GET_SYSCALL_INFO
						// XXX : support 32 bits exe's (beware of 32 bits syscall numbers)
						if (word_sz!=NpWordSz) {
							Trace trace("AutodepPtrace::_changed","panic","word width") ;
							info.record.report_direct({ JobExecProc::Panic , word_sz+" bits processes on "s+NpWordSz+" host not supported yet with ptrace" }) ;
							info.has_exit_proc = false ;
							info.on_going      = false ;
							goto NextSyscall ;
						}
						SWEAR( syscall_info.op==(HAS_SECCOMP?PTRACE_SYSCALL_INFO_SECCOMP:PTRACE_SYSCALL_INFO_ENTRY) , syscall_info.op ) ;
						#if HAS_SECCOMP
							auto& entry_info = syscall_info.seccomp ;                             // access available info upon syscall entry, i.e. seccomp field when seccomp is     used
						#else
							auto& entry_info = syscall_info.entry   ;                             // access available info upon syscall entry, i.e. entry   field when seccomp is not used
						#endif
						int syscall = entry_info.nr ;
					#else
						int syscall = np_ptrace_get_nr( pid , word_sz ) ;                         // use non-portable calls if portable accesses are not implemented
					#endif
					SWEAR( syscall>=0 && syscall<SyscallDescr::NSyscalls ) ;
					SyscallDescr const& descr = tab[syscall] ;
					if (HAS_SECCOMP) SWEAR(+descr,"should not be awaken for nothing") ;
					if ( +descr && descr.entry ) {
						info.idx = syscall ;
						#if HAS_PTRACE_GET_SYSCALL_INFO                                           // use portable calls if implemented
							// ensure entry_info is actually an array of uint64_t although one is declared as unsigned long and the other is unsigned long long
							static_assert( sizeof(entry_info.args[0])==sizeof(uint64_t) && ::is_unsigned_v<remove_reference_t<decltype(entry_info.args[0])>> ) ;
							uint64_t* args = reinterpret_cast<uint64_t*>(entry_info.args) ;
						#else
							::array<uint64_t,6> arg_array = np_ptrace_get_args( pid , word_sz ) ; // use non-portable calls if portable accesses are not implemented
							uint64_t *          args      = arg_array.data()                    ; // we need a variable to hold the data while we pass the pointer
						#endif
						SWEAR(!info.ctx,syscall) ;                                                // ensure following SWEAR on info.ctx is pertinent
						descr.entry( info.ctx , info.record , pid , args , descr.comment ) ;
						if (!descr.exit) SWEAR(!info.ctx,syscall) ;                               // no need for a context if we are not called at exit
					}
					info.has_exit_proc = descr.exit                         ;
					info.on_going      = !HAS_SECCOMP || info.has_exit_proc ;                     // if using seccomp and we have no exit proc, we skip the syscall-exit
					if (info.has_exit_proc) goto SyscallExit ;
					else                    goto NextSyscall ;
				} else {
					#if HAS_PTRACE_GET_SYSCALL_INFO
						SWEAR( syscall_info.op==PTRACE_SYSCALL_INFO_EXIT ) ;
					#endif
					if (HAS_SECCOMP) SWEAR(info.has_exit_proc,"should not have been stopped on exit") ;
					if (info.has_exit_proc) {
						#if HAS_PTRACE_GET_SYSCALL_INFO                                           // use portable calls if implemented
							int64_t res = syscall_info.exit.rval ;
						#else
							int64_t res = np_ptrace_get_res( pid , word_sz ) ;                    // use non-portable calls if portable accesses are not implemented
						#endif
						int64_t new_res = tab[info.idx].exit( info.ctx , info.record , pid , res ) ;
						if (new_res!=res) np_ptrace_set_res( pid , new_res , word_sz ) ;
						info.ctx = nullptr ;                                                      // ctx is used to retain some info between syscall entry and exit
					}
					info.on_going = false ;
					goto NextSyscall ;
				}
			}
		NextSyscall :
			if ( ::ptrace( StopAtNextSyscallEntry , pid , 0/*addr*/ , sig )<0 ) throw "cannot set trace for next syscall for "s+pid ;
			return false/*done*/ ;
		SyscallExit :
			if ( ::ptrace( StopAtSyscallExit      , pid , 0/*addr*/ , sig )<0 ) throw "cannot set trace for syscall exit for "s+pid ;
			return false/*done*/ ;
		} catch (::string const& e) {
			SWEAR(errno==ESRCH,errno) ;               // if we cant find pid, it means we were not informed it terminated
			int   ws   = 0/*garbage*/               ;
			pid_t pid_ = ::waitpid(pid,&ws,WNOHANG) ;
			if (pid_==0) {                            // XXX : it seems that there is a race here : process cant receive a ptrace, but waitpid is not aware of the new status
				sleep(1) ;
				pid_ = ::waitpid(pid,&ws,WNOHANG) ;   // retry once the race is solved
			}
			if (pid_>0) wstatus = ws ;
		}
	} else if (WIFEXITED  (wstatus)) {
	} else if (WIFSIGNALED(wstatus)) {
		#if HAS_SECCOMP
			// with seccomp, this is how we get a bad architecture as the filter is now
			if ( (wstatus&0xff) == (0x80|SIGSYS) ) {
				Trace trace("AutodepPtrace::_changed","panic","arch","seccomp") ;
				info.record.report_direct({ JobExecProc::Panic , "32 bits processes on "s+NpWordSz+" host not supported yet with ptrace" }) ;
			}
		#endif
	} else {
		fail("unrecognized wstatus ",wstatus," for pid ",pid) ;
	}
	PidInfo::s_tab.erase(pid) ;                       // process pid is terminated
	return pid==child_pid/*done*/ ;
}
