// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <err.h>
#include <fcntl.h>
#include <linux/sched.h>
#include <syscall.h>                   // for SYS_* macros

#include <sys/ptrace.h>
#include <linux/ptrace.h>              // must be after sys/ptrace.h to avoid stupid request macro definitions
#undef PTRACE_CONT                     // stupid linux/ptrace.h define ptrace requests as macros !
#undef PTRACE_GET_SYSCALL_INFO         // .
#undef PTRACE_PEEKDATA                 // .
#undef PTRACE_POKEDATA                 // .
#undef PTRACE_SETOPTIONS               // .
#undef PTRACE_SYSCALL                  // .
#undef PTRACE_TRACEME                  // .

#include "seccomp.h"

#include "disk.hh"
#include "record.hh"

#include "ptrace.hh"

AutodepEnv* AutodepPtrace::s_autodep_env = nullptr  ;

#if HAS_PTRACE

using namespace Disk ;

using SyscallEntry = decltype(ptrace_syscall_info().seccomp) ;
using SyscallExit  = decltype(ptrace_syscall_info().exit   ) ;

struct Action {
	RecordSock::Chdir   chdir    = {} ;
	RecordSock::Lnk     lnk      = {} ;
	RecordSock::Open    open     = {} ;
	RecordSock::ReadLnk read_lnk = {} ;
	RecordSock::Rename  rename   = {} ;
	RecordSock::SymLnk  sym_lnk  = {} ;
	RecordSock::Unlink  unlink   = {} ;
} ;

// When tracing a child, initially, the child will run till first signal and only then will follow the specified seccomp filter.
// If a traced system call (as per the seccomp filter) is done before, it will fail.
// This typically happen with the initial exec call.
// Thus, child must send a first signal that parent must ignore.
static constexpr int FirstSignal = SIGTRAP ;

struct PidInfo {
	// static data
	static ::umap<pid_t,PidInfo > s_tab ;
	// cxtors & casts
	PidInfo( pid_t pid ) : record{pid} {}
	// data
	RecordSock record   ;
	size_t     idx      = 0     ;
	Action     action   ;
	bool       on_going = false ;
} ;

struct SyscallDescr {
	// static data
	static ::vector<SyscallDescr> const s_tab ;
	// data
	int     syscall                                            = 0       ;
	void    (*entry)( PidInfo& , pid_t , SyscallEntry const& ) = nullptr ;
	void    (*exit )( PidInfo& , pid_t , SyscallExit  const& ) = nullptr ;
	uint8_t prio                                               = 0       ;
	bool    data_access                                        = false   ;
} ;

void AutodepPtrace::_init(pid_t cp) {
	RecordSock::s_init(*s_autodep_env) ;
	child_pid = cp ;
	//
	pid_t pid     ;
	int   wstatus ;
	SWEAR( (pid=wait(&wstatus))==child_pid ) ;                                 // first signal is only there to start tracing as we are initially traced to next signal
	::ptrace( PTRACE_SETOPTIONS , pid , 0/*addr*/ ,
		PTRACE_O_TRACESECCOMP
	|	PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK
	|	PTRACE_O_TRACESYSGOOD                                                  // necessary to have a correct syscall_info.op field
	) ;
	SWEAR( WIFSTOPPED(wstatus) && WSTOPSIG(wstatus)==FirstSignal ) ;
	::ptrace( PTRACE_CONT , pid , 0/*addr*/ , 0/*data*/ ) ;
}

void AutodepPtrace::s_prepare_child() {
	// prepare filter
	RecordSock::s_init(*s_autodep_env) ;
	SWEAR(RecordSock::s_autodep_env->lnk_support!=LnkSupport::Unknown) ;
	scmp_filter_ctx scmp = seccomp_init(SCMP_ACT_ALLOW) ; SWEAR(scmp) ;
	bool ignore_stat = RecordSock::s_autodep_env->ignore_stat && RecordSock::s_autodep_env->lnk_support!=LnkSupport::Full ; // if full link support, we need to analyze uphill dirs
	SWEAR(SyscallDescr::s_tab[0].syscall==0) ;                                                                              // ensure first entry is empty
	for( size_t i=1 ; i<SyscallDescr::s_tab.size() ; i++ ) {                                                                // first entry is ignore to ease s_tab definition with #ifdef
		SyscallDescr const& sc = SyscallDescr::s_tab[i] ;
		if ( !sc.data_access && ignore_stat ) continue ;                       // non stat-like access are always needed
		//
		seccomp_syscall_priority( scmp ,                     sc.syscall , sc.prio ) ;
		seccomp_rule_add        ( scmp , SCMP_ACT_TRACE(i) , sc.syscall , 0       ) ;
	}
	// Load in the kernel & trace
	int rc = seccomp_load(scmp) ; SWEAR_PROD(rc==0) ;
	ptrace( PTRACE_TRACEME , 0/*pid*/ , 0/*addr*/ , 0/*data*/ ) ;
	kill_self(FirstSignal) ;                                                   // cannot call a traced syscall until a signal is received as we are initially traced till the next signal
}

::pair<bool/*done*/,int/*wstatus*/> AutodepPtrace::_changed( int pid , int wstatus ) {
	PidInfo& info = PidInfo::s_tab.try_emplace(pid,pid).first->second ;
	if (WIFSTOPPED(wstatus)) {
		struct ptrace_syscall_info  syscall_info ;
		::ptrace( PTRACE_GET_SYSCALL_INFO , pid , sizeof(struct ptrace_syscall_info) , &syscall_info ) ;
		if ( (wstatus>>8) == (SIGTRAP|(PTRACE_EVENT_SECCOMP<<8)) ) {
			// enter syscall
			SWEAR(syscall_info.op==PTRACE_SYSCALL_INFO_SECCOMP) ;
			info.idx = syscall_info.seccomp.ret_data ;
			SyscallDescr const& descr = SyscallDescr::s_tab.at(info.idx) ;
			descr.entry( info , pid , syscall_info.seccomp ) ;
			info.on_going = descr.exit ;
			ptrace( info.on_going?PTRACE_SYSCALL:PTRACE_CONT , pid , 0/*addr*/ , 0/*data*/ ) ;
		} else if ( syscall_info.op==PTRACE_SYSCALL_INFO_EXIT ) {
			// exit syscall
			SyscallDescr::s_tab.at(info.idx).exit( info , pid , syscall_info.exit ) ;
			info.on_going = false ;
			ptrace( PTRACE_CONT , pid , 0/*addr*/ , 0/*data*/ ) ;
		} else {
			// receive a signal : nothing to do
			swear(syscall_info.op==PTRACE_SYSCALL_INFO_NONE,"bad syscall_info.op ",syscall_info.op,"!=",PTRACE_SYSCALL_INFO_NONE) ;
			int sig = WSTOPSIG(wstatus) ;
			if (sig==SIGTRAP) sig = 0 ;                                    // for some reason, we get spurious SIGTRAP
			ptrace( info.on_going?PTRACE_SYSCALL:PTRACE_CONT , pid , 0/*addr*/ , sig ) ;
		}
	} else if ( WIFEXITED(wstatus) || WIFSIGNALED(wstatus) ) {             // not sure we are informed that a process exits
		if (pid==child_pid) return {true/*done*/,wstatus} ;
		PidInfo::s_tab.erase(pid) ;                                        // free resources if possible
	} else {
		fail("unrecognized wstatus ",wstatus," for pid ",pid) ;
	}
	return {false/*done*/,0} ;
}

//
// PidInfo
//

::umap<pid_t,PidInfo > PidInfo::s_tab ;

::string get_str( pid_t pid , uint64_t val ) {
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

::string get_str( pid_t pid , uint64_t val , size_t len ) {
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

template<class T> T get( pid_t pid , uint64_t val ) {
	::string buf = get_str(pid,val,sizeof(T)) ;
	T        res ; ::memcpy(&res,buf.data(),sizeof(T)) ;
	return res ;
}

void put_str( pid_t pid , uint64_t val , ::string const& str ) {
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

//
// SyscallDescr
//

template<bool At> int _at(uint64_t val) { if (At) return val ; else return AT_FDCWD ; }

// chdir
template<bool At> void entry_chdir( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	try { info.action.chdir = RecordSock::Chdir( true/*active*/ , info.record , _at<At>(entry.args[0]) , At?nullptr:get_str(pid,entry.args[0]).c_str() ) ; }
	catch (int) {}
}
void exit_chdir( PidInfo& info , pid_t pid , SyscallExit const& res ) {
	info.action.chdir( info.record , res.rval , pid ) ;
}

// execve
// must be called before actual syscall execution as after execution, info is no more available
template<bool At,bool Flags> void entry_execve( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	try { info.record.exec( _at<At>(entry.args[0]) , get_str(pid,entry.args[0+At]).c_str() , Flags&&(entry.args[3+At]&AT_SYMLINK_NOFOLLOW) , At?"execveat":"execve" ) ; }
	catch (int) {}
}

// link
template<bool At,bool Flags> void entry_lnk( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	int flags = Flags ? entry.args[2+At*2] : 0 ;
	try {
		info.action.lnk = RecordSock::Lnk(
			true/*active*/ , info.record
		,	_at<At>(entry.args[0   ]) , get_str(pid,entry.args[0+At  ]).c_str()
		,	_at<At>(entry.args[1+At]) , get_str(pid,entry.args[1+At*2]).c_str()
		,	flags
		,	At?to_string(::hex,"linkat.",flags):"link"
		) ;
	} catch (int) {}
}
void exit_lnk( PidInfo& info , pid_t pid , SyscallExit const& res ) {
	info.action.lnk( info.record , res.rval , np_ptrace_get_errno(pid) ) ;
}

// open
template<bool At> void entry_open( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	int flags = entry.args[1+At] ;
	try {
		info.action.open = RecordSock::Open(
			true/*active*/ , info.record
		,	_at<At>(entry.args[0])
		,	get_str(pid,entry.args[0+At]).c_str()
		,	flags
		,	to_string(::hex,At?"openat.":"open.",flags)
		) ;
	}
	catch (int) {}
}
void exit_open( PidInfo& info , pid_t pid , SyscallExit const& res ) {
	info.action.open( info.record , false/*has_fd*/ , res.rval , np_ptrace_get_errno(pid) ) ;
}

// read_lnk
template<bool At> void entry_read_lnk( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	using Len = MsgBuf::Len ;
	try {
		int at = _at<At>(entry.args[0]) ;
		if (at==AT_BACKDOOR) {
			::string data = get_str( pid , entry.args[0+At] , sizeof(Len)+get<size_t>(pid,entry.args[0+At]) ) ;
			::string buf  ( entry.args[2+At] , char(0) ) ;
			RecordSock::ReadLnk( true/*active*/ , info.record , data.data() , buf.data() , buf.size() ) ;
			Len len = MsgBuf::s_sz(buf.data()) ;
			if      (sizeof(Len)+len<=buf.size()) buf.resize(sizeof(Len)+len) ;
			else if (sizeof(Len)    <=buf.size()) buf.resize(sizeof(Len)    ) ;
			else                                  buf.resize(0              ) ;
			put_str( pid , entry.args[1+At] , buf ) ;
			np_ptrace_clear_syscall(pid) ;
		} else {
			info.action.read_lnk = RecordSock::ReadLnk( true/*active*/ , info.record , at , get_str(pid,entry.args[0+At]).c_str() , At?"readlinkat":"readlink" ) ;
		}
	} catch (int) {}
}
void exit_read_lnk( PidInfo& info , pid_t /*pid*/ , SyscallExit const& res ) {
	info.action.read_lnk( info.record , res.rval ) ;
}

// rename
template<bool At,bool Flags> void entry_rename( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	int flags = Flags ? entry.args[2+At*2] : 0 ;
	try {
		info.action.rename = RecordSock::Rename(
			true/*active*/ , info.record
		,	_at<At>(entry.args[0   ]) , get_str(pid,entry.args[0+At  ]).c_str()
		,	_at<At>(entry.args[1+At]) , get_str(pid,entry.args[1+At*2]).c_str()
		,	flags
		,	At?to_string(::hex,"renameat.",flags):"rename"
		) ;
	} catch (int) {}
}
void exit_rename( PidInfo& info , pid_t pid , SyscallExit const& res ) {
	info.action.rename( info.record , res.rval , np_ptrace_get_errno(pid) ) ;
}

// symlink
template<bool At> void entry_sym_lnk( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	try { info.action.sym_lnk = RecordSock::SymLnk( true/*active*/ , info.record , _at<At>(entry.args[1]) , get_str(pid,entry.args[1+At]).c_str() , At?"symlinkat":"symlink" ) ; }
	catch (int) {}
}
void exit_sym_lnk( PidInfo& info , pid_t , SyscallExit const& res ) {
	info.action.sym_lnk( info.record , res.rval ) ;
}

// unlink
template<bool At> void entry_unlink( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	try {
		int      at    = _at<At>(entry.args[0])        ;
		int      flags = At ? entry.args[1+At] : 0     ;
		::string file  = get_str(pid,entry.args[0+At]) ;
		info.action.unlink = RecordSock::Unlink( true/*active*/ , info.record , at , file.c_str() , flags&AT_REMOVEDIR , At?"unlinkat":"unlink" ) ;
	} catch (int) {}
}
void exit_unlink( PidInfo& info , pid_t , SyscallExit const& res ) {
	info.action.unlink( info.record , res.rval ) ;
}

// access
static constexpr int FlagAlways = -1 ;
static constexpr int FlagNever  = -2 ;
template<bool At,int FlagArg> void entry_stat( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	bool no_follow ;
	switch (FlagArg) {
		case FlagAlways : no_follow = true                                         ; break ;
		case FlagNever  : no_follow = false                                        ; break ;
		default         : no_follow = entry.args[FlagArg+At] & AT_SYMLINK_NOFOLLOW ;
	}
	try { info.record.stat( _at<At>(entry.args[0]) , get_str(pid,entry.args[0+At]).c_str() , no_follow ) ; }
	catch (int) {}
}
template<bool At,int FlagArg> void entry_solve( PidInfo& info , pid_t pid , SyscallEntry const& entry ) {
	bool no_follow ;
	switch (FlagArg) {
		case FlagAlways : no_follow = true                                         ; break ;
		case FlagNever  : no_follow = false                                        ; break ;
		default         : no_follow = entry.args[FlagArg+At] & AT_SYMLINK_NOFOLLOW ;
	}
	try { info.record.solve( _at<At>(entry.args[0]) , get_str(pid,entry.args[0+At]).c_str() , no_follow ) ; }
	catch (int) {}
}

// ordered by priority of generated seccomp filter (more frequent first)
::vector<SyscallDescr> const SyscallDescr::s_tab = {
	{}                                                                                                 // first entry is ignored so each active line contains a ','
#ifdef SYS_faccessat
,	{ SYS_faccessat         , entry_stat    <true /*At*/,2             > , nullptr       , 2 , false }
#endif
#ifdef SYS_access
,	{ SYS_access            , entry_stat    <false/*At*/,FlagNever     > , nullptr       , 1 , false }
#endif
#ifdef SYS_faccessat2
,	{ SYS_faccessat2        , entry_stat    <true /*At*/,2             > , nullptr       , 2 , false }
#endif
#ifdef SYS_chdir
,	{ SYS_chdir             , entry_chdir   <false/*At*/>                , exit_chdir    , 1 , true  }
#endif
#ifdef SYS_fchdir
,	{ SYS_fchdir            , entry_chdir   <true /*At*/>                , exit_chdir    , 1 , true  }
#endif
#ifdef SYS_execve
,	{ SYS_execve            , entry_execve  <false/*At*/,false/*Flags*/> , nullptr       , 1 , true  }
#endif
#ifdef SYS_execveat
,	{ SYS_execveat          , entry_execve  <true /*At*/,true /*Flags*/> , nullptr       , 1 , true  }
#endif
#ifdef SYS_link
,	{ SYS_link              , entry_lnk     <false/*At*/,false/*Flags*/> , exit_lnk      , 1 , true  }
#endif
#ifdef SYS_linkat
,	{ SYS_linkat            , entry_lnk     <true /*At*/,true /*Flags*/> , exit_lnk      , 1 , true  }
#endif
#ifdef SYS_mkdir
,	{ SYS_mkdir             , entry_solve   <false/*At*/,FlagNever     > , nullptr       , 1 , false }
#endif
#ifdef SYS_mkdirat
,	{ SYS_mkdirat           , entry_solve   <true /*At*/,FlagNever     > , nullptr       , 1 , false }
#endif
#ifdef SYS_name_to_handle_at
,	{ SYS_name_to_handle_at , entry_open    <true /*At*/>                , exit_open     , 1 , true  }
#endif
#ifdef SYS_open
,	{ SYS_open              , entry_open    <false/*At*/>                , exit_open     , 2 , true  }
#endif
#ifdef SYS_openat
,	{ SYS_openat            , entry_open    <true /*At*/>                , exit_open     , 2 , true  }
#endif
#ifdef SYS_openat2
, 	{ SYS_openat2           , entry_open    <true /*At*/>                , exit_open     , 2 , true  }
#endif
#ifdef SYS_open_tree
,	{ SYS_open_tree         , entry_stat    <true /*At*/,1             > , nullptr       , 1 , false }
#endif
#ifdef SYS_readlink
,	{ SYS_readlink          , entry_read_lnk<false/*At*/>                , exit_read_lnk , 2 , true  }
#endif
#ifdef SYS_readlinkat
,	{ SYS_readlinkat        , entry_read_lnk<true /*At*/>                , exit_read_lnk , 2 , true  }
#endif
#if SYS_rename
,	{ SYS_rename            , entry_rename  <false/*At*/,false/*Flags*/> , exit_rename   , 1 , true  }
#endif
#ifdef SYS_renameat
,	{ SYS_renameat          , entry_rename  <true /*At*/,false/*Flags*/> , exit_rename   , 1 , true  }
#endif
#ifdef SYS_renameat2
,	{ SYS_renameat2         , entry_rename  <true /*At*/,true /*Flags*/> , exit_rename   , 1 , true  }
#endif
#ifdef SYS_rmdir
,	{ SYS_rmdir             , entry_stat    <false/*At*/,FlagAlways    > , nullptr       , 1 , false }
#endif
#ifdef SYS_stat
,	{ SYS_stat              , entry_stat    <false/*At*/,FlagNever     > , nullptr       , 2 , false }
#endif
#ifdef SYS_stat64
,	{ SYS_stat64            , entry_stat    <false/*At*/,FlagNever     > , nullptr       , 1 , false }
#endif
#ifdef SYS_fstatat64
,	{ SYS_fstatat64         , entry_stat    <true /*At*/,2             > , nullptr       , 1 , false }
#endif
#ifdef SYS_lstat
,	{ SYS_lstat             , entry_stat    <false/*At*/,FlagAlways    > , nullptr       , 2 , false }
#endif
#ifdef SYS_lstat64
,	{ SYS_lstat64           , entry_stat    <false/*At*/,FlagAlways    > , nullptr       , 1 , false }
#endif
#ifdef SYS_statx
,	{ SYS_statx             , entry_stat    <true /*At*/,1             > , nullptr       , 1 , false }
#endif
#if SYS_newfstatat
,	{ SYS_newfstatat        , entry_stat    <true /*At*/,2             > , nullptr       , 2 , false }
#endif
#ifdef SYS_oldstat
,	{ SYS_oldstat           , entry_stat    <false/*At*/,FlagNever     > , nullptr       , 1 , false }
#endif
#ifdef SYS_oldlstat
,	{ SYS_oldlstat          , entry_stat    <false/*At*/,FlagAlways>     , nullptr       , 1 , false }
#endif
#ifdef SYS_symlink
,	{ SYS_symlink           , entry_sym_lnk <false/*At*/>                , exit_sym_lnk  , 1 , true  }
#endif
#ifdef SYS_symlinkat
,	{ SYS_symlinkat         , entry_sym_lnk <true /*At*/>                , exit_sym_lnk  , 1 , true  }
#endif
#ifdef SYS_unlink
,	{ SYS_unlink            , entry_unlink  <false/*At*/>                , exit_unlink   , 1 , true  }
#endif
#ifdef SYS_unlinkat
,	{ SYS_unlinkat          , entry_unlink  <true /*At*/>                , exit_unlink   , 1 , true  }
#endif
} ;

#endif // HAS_PTRACE
