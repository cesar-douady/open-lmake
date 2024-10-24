// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/signalfd.h>
#include <sys/wait.h>
#include <wait.h>

#include <csignal>

#include "fd.hh"

struct Pipe {
	// cxtors & casts
	Pipe(                          ) = default ;
	Pipe(NewType,bool no_std_=false) { open(no_std_) ; }
	// services
	void open(bool no_std_=false) {
		int fds[2] ;
		swear_prod( ::pipe(fds)==0 , "cannot create pipes" ) ;
		read  = {fds[0],no_std_} ;
		write = {fds[1],no_std_} ;
	}
	void close() {
		read .close() ;
		write.close() ;
	}
	void no_std() {
		read .no_std() ;
		write.no_std() ;
	}
	// data
	Fd read  ; // read  side of the pipe
	Fd write ; // write side of the pipe
} ;

inline sigset_t _mk_sigset(::vector<int> const& sigs) {
	sigset_t res ;
	::sigemptyset(&res) ;
	for( int s : sigs) ::sigaddset(&res,s) ;
	return res ;
}
inline bool is_blocked_sig(int sig) {
	sigset_t old_mask ;
	swear( ::pthread_sigmask(0,nullptr,&old_mask)==0 , "cannot get sig ",sig ) ;
	return ::sigismember(&old_mask,sig) ;
}
inline void block_sigs  (::vector<int> const& sigs) { swear( ::pthread_sigmask( SIG_BLOCK   , &::ref(_mk_sigset(sigs)) , nullptr )==0 , "cannot block sigs "  ,sigs) ; }
inline void unblock_sigs(::vector<int> const& sigs) { swear( ::pthread_sigmask( SIG_UNBLOCK , &::ref(_mk_sigset(sigs)) , nullptr )==0 , "cannot unblock sigs ",sigs) ; }
inline Fd open_sigs_fd(::vector<int> const& sigs) {
	return ::signalfd( -1 , &::ref(_mk_sigset(sigs)) , SFD_CLOEXEC ) ;
}
struct BlockedSig {
	BlockedSig (                         ) = default ;
	BlockedSig (BlockedSig&&             ) = default ;                                         // copy is meaningless
	BlockedSig (::vector<int> const& sigs) : blocked{       sigs } { block_sigs  (blocked) ; }
	BlockedSig (::vector<int>     && sigs) : blocked{::move(sigs)} { block_sigs  (blocked) ; }
	~BlockedSig(                         )                         { unblock_sigs(blocked) ; }
	// data
	::vector<int> blocked ;
} ;

inline bool is_sig_sync(int sig) {
	switch (sig) {
		case SIGILL  :
		case SIGTRAP :
		case SIGABRT :
		case SIGBUS  :
		case SIGFPE  :
		case SIGSEGV :
		case SIGSYS  : return true  ;
		default      : return false ;
	}
}

inline bool wstatus_ok(int wstatus) {
	return WIFEXITED(wstatus) && WEXITSTATUS(wstatus)==0 ;
}

inline ::string wstatus_str(int wstatus) {
	if (WIFEXITED  (wstatus)) return WEXITSTATUS(wstatus) ? "exit "s+WEXITSTATUS(wstatus)  : "ok"s   ;
	if (WIFSIGNALED(wstatus)) return "signal "s+WTERMSIG(wstatus)+'-'+::strsignal(WTERMSIG(wstatus)) ;
	else                      return "??"                                                            ;
}

struct Child {
	static constexpr size_t StackSz = 16<<10 ;                       // stack size for sub-process : we just need s small stack before exec, experiment shows 8k is enough, take 16k
	static constexpr Fd     NoneFd  { -1 }   ;
	static constexpr Fd     PipeFd  { -2 }   ;
	// statics
	[[noreturn]] static int _s_do_child           (void* self) { reinterpret_cast<Child*>(self)->_do_child           () ; }
	[[noreturn]] static int _s_do_child_trampoline(void* self) { reinterpret_cast<Child*>(self)->_do_child_trampoline() ; }
	// cxtors & casts
	~Child() {
		swear_prod(pid==0,"bad pid",pid) ;
	}
	// accesses
	bool operator+() const { return pid     ; }
	bool operator!() const { return !+*this ; }
	// services
	void spawn() ;
	void mk_daemon() {
		pid = 0 ;
		stdin .detach() ;
		stdout.detach() ;
		stderr.detach() ;
	}
	void waited() {
		pid = 0 ;
	}
	int/*wstatus*/ wait() {
		SWEAR(pid!=0) ;
		int wstatus ;
		int rc = ::waitpid(pid,&wstatus,0) ;
		swear_prod(rc==pid,"cannot wait for pid",pid) ;
		waited() ;
		return wstatus ;
	}
	bool         wait_ok (       )       { return wstatus_ok(wait())                           ; }
	bool/*done*/ kill    (int sig)       { return kill_process(pid,sig,as_session/*as_group*/) ; }
	bool         is_alive(       ) const { return kill_process(pid,0                         ) ; }
private :
	[[noreturn]] void _do_child           (                      ) ;
	[[noreturn]] void _do_child_trampoline(                      ) ; // used when creating a new pid namespace : we need an intermediate process as the init process
	[[noreturn]] void _exit               ( Rc , const char* msg ) ;
	//data
public :
	// spawn parameters
	bool            as_session         = false      ;
	pid_t           first_pid          = 0          ;
	::vector_s      cmd_line           = {}         ;
	Fd              stdin_fd           = Fd::Stdin  ;
	Fd              stdout_fd          = Fd::Stdout ;
	Fd              stderr_fd          = Fd::Stderr ;
	::map_ss const* env                = nullptr    ;
	::map_ss const* add_env            = nullptr    ;
	::string        cwd_s              = {}         ;
	int/*rc*/       (*pre_exec)(void*) = nullptr    ;                // if no cmd_line, this is the entire function exected as child returning the exit status
	void*           pre_exec_arg       = nullptr    ;
	// child info
	pid_t       pid    = 0  ;
	AutoCloseFd stdin  = {} ;
	AutoCloseFd stdout = {} ;
	AutoCloseFd stderr = {} ;
	// private (cannot really declare private or it would not be an aggregate any more)
	Pipe         _p2c             = {}      ;
	Pipe         _c2po            = {}      ;
	Pipe         _c2pe            = {}      ;
	void*        _child_stack_ptr = nullptr ;                        // all memory must be allocated before clone is called
	const char** _child_env       = nullptr ;                        // .
	const char** _child_args      = nullptr ;                        // .
} ;
