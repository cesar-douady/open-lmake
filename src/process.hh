// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include <csignal>

#include "fd.hh"

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
	if (WIFEXITED(wstatus)) {
		int rc = WEXITSTATUS(wstatus) ;
		if ( rc==0                               ) return "ok"                                                        ;
		if ( int sig=rc-128 ; sig>=0 && sig<NSIG ) return "exit "s+rc+" (could be signal "+sig+'-'+strsignal(sig)+')' ;
		/**/                                       return "exit "s+rc                                                 ;
	}
	if (WIFSIGNALED(wstatus)) {
		int sig = WTERMSIG(wstatus) ;
		return "signal "s+sig+'-'+::strsignal(sig) ;
	}
	return "??"                                                            ;
}

struct Child {
	static constexpr size_t StackSz = 16<<10 ;                       // stack size for sub-process : we just need s small stack before exec, experiment shows 8k is enough, take 16k
	static constexpr Fd     NoneFd  { -1 }   ;
	static constexpr Fd     PipeFd  { -2 }   ;
	// statics
	[[noreturn]] static int _s_do_child           (void* self_) { reinterpret_cast<Child*>(self_)->_do_child           () ; }
	[[noreturn]] static int _s_do_child_trampoline(void* self_) { reinterpret_cast<Child*>(self_)->_do_child_trampoline() ; }
	// cxtors & casts
	~Child() {
		swear_prod(pid==0,"bad pid",pid) ;
		if (_child_args) delete[] _child_args ;
	}
	// accesses
	bool operator+() const { return pid ; }
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
	pid_t pid    = 0  ;
	AcFd  stdin  = {} ;
	AcFd  stdout = {} ;
	AcFd  stderr = {} ;
	// private (cannot really declare private or it would not be an aggregate any more)
	Pipe         _p2c             = {}      ;
	Pipe         _c2po            = {}      ;
	Pipe         _c2pe            = {}      ;
	void*        _child_stack_ptr = nullptr ;                        // all memory must be allocated before clone is called
	const char** _child_env       = nullptr ;                        // .
	const char** _child_args      = nullptr ;                        // .
} ;
