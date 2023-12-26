// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <err.h>
#include <linux/sched.h>

#include "disk.hh"
#include "record.hh"

#include "ptrace.hh"

#include <sys/ptrace.h>
#if HAS_PTRACE_GET_SYSCALL_INFO        // must be after utils.hh include, use portable calls if implemented
	#include <linux/ptrace.h>          // for struct ptrace_syscall_info, must be after sys/ptrace.h to avoid stupid request macro definitions
	#if MUST_UNDEF_PTRACE_MACROS       // must be after utils.hh include
		#undef PTRACE_CONT             // /!\ stupid linux/ptrace.h defines ptrace requests as macros while ptrace expects an enum on some systems
		#undef PTRACE_GET_SYSCALL_INFO // .
		#undef PTRACE_PEEKDATA         // .
		#undef PTRACE_POKEDATA         // .
		#undef PTRACE_SETOPTIONS       // .
		#undef PTRACE_SYSCALL          // .
		#undef PTRACE_TRACEME          // .
	#endif
#endif

#if HAS_SECCOMP                        // must be after utils.hh include
	#include <seccomp.h>
#endif

AutodepEnv* AutodepPtrace::s_autodep_env = nullptr  ;

using namespace Disk ;

static ::string get_str( pid_t pid , uint64_t val ) {
	if (!pid) return {reinterpret_cast<const char*>(val)} ;
	::string res ;
	errno = 0 ;
	for(;;) {
		uint64_t offset = val%sizeof(long)                                               ;
		long     word   = ptrace( PTRACE_PEEKDATA , pid , val-offset , nullptr/*data*/ ) ;
		if (errno) throw 0 ;
		char buf[sizeof(long)] ; ::memcpy( buf , &word , sizeof(long) ) ;
		for( uint64_t len=0 ; len<sizeof(long)-offset ; len++ ) if (!buf[offset+len]) { res.append( buf+offset , len                 ) ; return res ; }
		/**/                                                                            res.append( buf+offset , sizeof(long)-offset ) ;
		val += sizeof(long)-offset ;
	}
}

static ::string get_str( pid_t pid , uint64_t val , size_t len ) {
	if (!pid) return {reinterpret_cast<const char*>(val),len} ;
	::string res ; res.reserve(len) ;
	errno = 0 ;
	for(;;) {
		uint64_t offset = val%sizeof(long)                                               ;
		long     word   = ptrace( PTRACE_PEEKDATA , pid , val-offset , nullptr/*data*/ ) ;
		if (errno) throw 0 ;
		char buf[sizeof(long)] ; ::memcpy( buf , &word , sizeof(long) ) ;
		if (offset+len<=sizeof(long)) { res.append( buf+offset , len                 ) ; return res ; }
		/**/                            res.append( buf+offset , sizeof(long)-offset ) ;
		val += sizeof(long)-offset ;
		len -= sizeof(long)-offset ;
	}
}

template<class T> static T get( pid_t pid , uint64_t val ) {
	T res ;
	if (pid) ::memcpy( &res , get_str(pid,val,sizeof(T)).data()  , sizeof(T) ) ;
	else     ::memcpy( &res , reinterpret_cast<void const*>(val) , sizeof(T) ) ;
	return res ;
}

static void put_str( pid_t pid , uint64_t val , ::string const& str ) {
	if (!pid) {
		::memcpy( reinterpret_cast<void*>(val) , str.data() , str.size() ) ;
		return ;
	}
	errno = 0 ;
	for( size_t i=0 ; i<str.size() ;) {
		uint64_t offset = val%sizeof(long)                            ;
		size_t   len    = ::min( str.size()-i , sizeof(long)-offset ) ;
		char     buf[sizeof(long)] ;
		if ( offset || len<sizeof(long) ) {
			long word = ptrace( PTRACE_PEEKDATA , pid , val-offset , nullptr/*data*/ ) ;
			if (errno) throw 0 ;
			::memcpy( buf , &word , sizeof(long) ) ;
		}
		::memcpy( buf+offset , str.data()+i , len ) ;
		long word ;  ::memcpy( &word , buf , sizeof(long) ) ;
		ptrace( PTRACE_POKEDATA , pid , val-offset , reinterpret_cast<void*>(word) ) ;
		if (errno) throw 0 ;
		i   += len ;
		val += len ;
	}
}

#define PTRACE
#include "syscall.cc"

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
	PidInfo(pid_t pid) : record{pid} {}
	// data
	Record record        ;
	size_t idx           = 0       ;
	void*  ctx           = nullptr ;
	bool   has_exit_proc = false   ;
	bool   on_going      = false   ;
} ;
::umap<pid_t,PidInfo > PidInfo::s_tab ;

void AutodepPtrace::_init(pid_t cp) {
	SWEAR( !s_autodep_env->tmp_view , s_autodep_env->tmp_view ) ;              // mapping tmp is incompatible with ptrace as memory allocation in child process is impossible
	Record::s_autodep_env(*s_autodep_env) ;
	child_pid = cp ;
	//
	int   wstatus ;
	pid_t pid     = wait(&wstatus) ;                                           // first signal is only there to start tracing as we are initially traced to next signal
	SWEAR( pid==child_pid , pid , child_pid ) ;
	::ptrace( PTRACE_SETOPTIONS , pid , 0/*addr*/ ,
		(HAS_SECCOMP?PTRACE_O_TRACESECCOMP:0)
	|	PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK
	|	PTRACE_O_TRACESYSGOOD                                                  // necessary to have a correct syscall_info.op field
	) ;
	SWEAR( WIFSTOPPED(wstatus) && WSTOPSIG(wstatus)==FirstSignal , wstatus ) ;
	::ptrace( StopAtNextSyscallEntry , pid , 0/*addr*/ , 0/*data*/ ) ;
}

void AutodepPtrace::s_prepare_child() {
	AutodepEnv const& ade = Record::s_autodep_env(*s_autodep_env) ;
	SWEAR( !ade.tmp_view                        , ade.tmp_view ) ;             // cannot support directory mapping as there is no way to allocate memory in the traced process
	SWEAR( ade.lnk_support!=LnkSupport::Unknown )                ;
	#if HAS_SECCOMP
		// prepare seccomp filter
		bool ignore_stat = ade.ignore_stat && ade.lnk_support!=LnkSupport::Full ; // if full link support, we need to analyze uphill dirs
		scmp_filter_ctx scmp = seccomp_init(SCMP_ACT_ALLOW) ;
		SWEAR(scmp) ;
		static SyscallDescr::Tab const& s_tab = SyscallDescr::s_tab() ;
		for( long syscall=0 ; syscall<SyscallDescr::NSyscalls ; syscall++ ) {
			SyscallDescr const& entry = s_tab[syscall] ;
			if ( !entry                            ) continue ;                // entry is not allocated
			if ( !entry.data_access && ignore_stat ) continue ;                // non stat-like access are always needed
			//
			seccomp_syscall_priority( scmp ,                                 syscall , entry.prio ) ;
			seccomp_rule_add        ( scmp , SCMP_ACT_TRACE(0/*ret_data*/) , syscall , 0          ) ;
		}
		// Load in the kernel & trace
		int rc = seccomp_load(scmp) ;
		SWEAR_PROD( rc==0 , rc ) ;
	#endif
	::ptrace( PTRACE_TRACEME , 0/*pid*/ , 0/*addr*/ , 0/*data*/ ) ;
	kill_self(FirstSignal) ;                                                   // cannot call a traced syscall until a signal is received as we are initially traced till the next signal
}

::pair<bool/*done*/,int/*wstatus*/> AutodepPtrace::_changed( pid_t pid , int wstatus ) {
	PidInfo& info = PidInfo::s_tab.try_emplace(pid,pid).first->second ;
	if (WIFSTOPPED(wstatus)) {
		int sig   = WSTOPSIG(wstatus) ;
		int event = wstatus>>16       ;
		if (sig==(SIGTRAP|0x80)) goto DoSyscall ;                              // if HAS_SECCOMP => syscall exit, else syscall enter or exit
		switch (event) {
			#if HAS_SECCOMP
				case PTRACE_EVENT_SECCOMP : SWEAR(!info.on_going) ; goto DoSyscall ; // syscall enter
			#endif
			case 0  : if (sig==SIGTRAP)         sig = 0 ; goto NextSyscall ;   // other stop reasons, wash spurious SIGTRAP, ignore
			default : SWEAR(sig==SIGTRAP,sig) ; sig = 0 ; goto NextSyscall ;   // ignore other events
		}
	DoSyscall :
		{	static SyscallDescr::Tab const& s_tab = SyscallDescr::s_tab() ;
			sig = 0 ;
			#if HAS_PTRACE_GET_SYSCALL_INFO                                    // use portable calls if implemented
				struct ptrace_syscall_info syscall_info ;
				::ptrace( PTRACE_GET_SYSCALL_INFO , pid , sizeof(struct ptrace_syscall_info) , &syscall_info ) ;
			#endif
			if (!info.on_going) {
				#if HAS_PTRACE_GET_SYSCALL_INFO
					SWEAR( syscall_info.op==(HAS_SECCOMP?PTRACE_SYSCALL_INFO_SECCOMP:PTRACE_SYSCALL_INFO_ENTRY) ) ;
					#if HAS_SECCOMP
						auto& entry_info = syscall_info.seccomp ;              // access available info upon syscall entry, i.e. seccomp field when seccomp is     used
					#else
						auto& entry_info = syscall_info.entry   ;              // access available info upon syscall entry, i.e. entry   field when seccomp is not used
					#endif
					int syscall = entry_info.nr ;
				#else
					int syscall = np_ptrace_get_syscall(pid) ;                 // use non-portable calls if portable accesses are not implemented
				#endif
				SWEAR( syscall>=0 && syscall<SyscallDescr::NSyscalls ) ;
				SyscallDescr const& descr = s_tab[syscall] ;
				if (HAS_SECCOMP) SWEAR(+descr,"should not be awaken for nothing") ;
				if (+descr) {
					info.idx = syscall ;
					#if HAS_PTRACE_GET_SYSCALL_INFO                            // use portable calls if implemented
						// ensure entry_info is actually an array of uint64_t although one is declared as unsigned long and the other is unesigned long long
						static_assert( sizeof(entry_info.args[0])==sizeof(uint64_t) && ::is_unsigned_v<remove_reference_t<decltype(entry_info.args[0])>> ) ;
						uint64_t* args = reinterpret_cast<uint64_t*>(entry_info.args) ;
					#else
						::array<uint64_t,6> arg_array = np_ptrace_get_args(pid) ; // use non-portable calls if portable accesses are not implemented
						uint64_t *          args      = arg_array.data()        ; // we need a variable to hold the data while we pass the pointer
					#endif
					bool skip = descr.entry( info.ctx , info.record , pid , args , descr.comment ) ;
					if (skip) np_ptrace_clear_syscall(pid) ;
					info.has_exit_proc = descr.exit && !skip ;
					if (!info.has_exit_proc) SWEAR(!info.ctx) ;                // no need for a context if we are not called at exit
				} else {
					info.has_exit_proc = false ;
				}
				info.on_going = !HAS_SECCOMP || info.has_exit_proc ;           // if using seccomp and we have no exit proc, we skip the syscall-exit
				if (info.has_exit_proc) goto SyscallExit ;
				else                    goto NextSyscall ;
			} else {
				#if HAS_PTRACE_GET_SYSCALL_INFO
					SWEAR( syscall_info.op==PTRACE_SYSCALL_INFO_EXIT ) ;
				#endif
				if (HAS_SECCOMP) SWEAR(info.has_exit_proc,"should not have been stopped on exit") ;
				if (info.has_exit_proc) {
					#if HAS_PTRACE_GET_SYSCALL_INFO                            // use portable calls if implemented
						int64_t res    = syscall_info.exit.rval ;
					#else
						int64_t res    = np_ptrace_get_res(pid) ;              // use non-portable calls if portable accesses are not implemented
					#endif
					int64_t new_res = s_tab[info.idx].exit( info.ctx , info.record , pid , res ) ;
					if (new_res!=res) FAIL("modified syscall result ",new_res,"!=",res," not yet implemented for ptrace") ;  // there is no such cases for now, if it arises, new_res must be reported
					info.ctx = nullptr ;                                                                                     // ctx is used to retain some info between syscall entry and exit
				}
				info.on_going = false ;
				goto NextSyscall ;
			}
		}
	NextSyscall :
		::ptrace( StopAtNextSyscallEntry , pid , 0/*addr*/ , sig ) ;
		goto Done ;
	SyscallExit :
		::ptrace( StopAtSyscallExit , pid , 0/*addr*/ , sig ) ;
		goto Done ;
	} else if ( WIFEXITED(wstatus) || WIFSIGNALED(wstatus) ) {                 // not sure we are informed that a process exits
		if (pid==child_pid) return {true/*done*/,wstatus} ;
		PidInfo::s_tab.erase(pid) ;                                            // free resources if possible
	} else {
		fail("unrecognized wstatus ",wstatus," for pid ",pid) ;
	}
Done :
	return {false/*done*/,0} ;
}
