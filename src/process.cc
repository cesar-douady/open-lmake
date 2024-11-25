// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h>
#include <sys/mount.h>

#include "disk.hh"

#include "process.hh"

using namespace Disk ;

[[noreturn]] void Child::_exit( Rc rc , const char* msg ) { // signal-safe
	if (msg) {
		bool ok = true ;
		ok &= ::write(2,msg,::strlen(msg))>=0 ;             // /!\ cannot use high level I/O because we are only allowed signal-safe functions. Msg contains terminating null
		if (_child_args) {
			ok &= ::write(2," :",2)>=0 ;                                                   // .
			for( const char* const* p=_child_args ; *p ; p++ ) {
				size_t l = ::strlen(*p) ;
				ok &= ::write(2," ",1)>=0 ;                                                // .
				if (l<=100)   ok &= ::write(2,*p ,l )>=0 ;                                 // .
				else        { ok &= ::write(2,*p ,97)>=0 ; ok &= ::write(2,"...",3)>=0 ; } // .
			}
		}
		ok &= ::write(2,"\n",1)>=0 ;                                                       // .
		if (!ok) rc = Rc::System ;
	}
	::_exit(+rc) ;                                                                         // /!\ cannot use exit as we are only allowed signal-safe functions
}

// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
[[noreturn]] void Child::_do_child() {
	::execve( _child_args[0] , const_cast<char**>(_child_args) , const_cast<char**>(_child_env) ) ;
	_exit(Rc::System,"cannot exec") ;                                                               // in case exec fails
}

// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
[[noreturn]] void Child::_do_child_trampoline() {
	if (as_session) ::setsid() ;                                            // if we are here, we are the init process and we must be in the new session to receive the kill signal
	//
	sigset_t full_mask ; ::sigfillset(&full_mask) ;
	::sigprocmask(SIG_UNBLOCK,&full_mask,nullptr) ;                         // restore default behavior
	//
	if (stdin_fd ==PipeFd) { _p2c .write.close() ; _p2c .read .no_std() ; } // could be optimized, but too complex to manage
	if (stdout_fd==PipeFd) { _c2po.read .close() ; _c2po.write.no_std() ; } // .
	if (stderr_fd==PipeFd) { _c2pe.read .close() ; _c2pe.write.no_std() ; } // .
	// set up std fd
	if (stdin_fd ==NoneFd) ::close(Fd::Stdin ) ; else if (_p2c .read !=Fd::Stdin ) ::dup2(_p2c .read ,Fd::Stdin ) ;
	if (stdout_fd==NoneFd) ::close(Fd::Stdout) ; else if (_c2po.write!=Fd::Stdout) ::dup2(_c2po.write,Fd::Stdout) ; // save stdout in case it is modified and we want to redirect stderr to it
	if (stderr_fd==NoneFd) ::close(Fd::Stderr) ; else if (_c2pe.write!=Fd::Stderr) ::dup2(_c2pe.write,Fd::Stderr) ;
	//
	if (_p2c .read >Fd::Std) _p2c .read .close() ;   // clean up : we only want to set up standard fd, other ones are necessarily temporary constructions
	if (_c2po.write>Fd::Std) _c2po.write.close() ;   // .
	if (_c2pe.write>Fd::Std) _c2pe.write.close() ;   // .
	//
	if (+cwd_s  ) { if (::chdir(no_slash(cwd_s).c_str())!=0) _exit(Rc::System,"cannot chdir"    ) ; }
	//
	if (pre_exec) { if (pre_exec(pre_exec_arg)          !=0) _exit(Rc::Fail,"cannot setup child") ; }
	//
	#if HAS_CLOSE_RANGE
		//::close_range(3,~0u,CLOSE_RANGE_UNSHARE) ; // activate this code (uncomment) as an alternative to set CLOEXEC in Fd(::string)
	#endif
	//
	if (first_pid) {
		SWEAR(first_pid>1,first_pid) ;
		// mount is not signal-safe and we are only allowed signal-safe functions here, but this is a syscall, should be ok
		if (::mount(nullptr,"/proc","proc",0,nullptr)!=0) {
			::perror("cannot mount /proc ") ;
			_exit(Rc::System,"cannot mount /proc") ;
		}
		{	char                     first_pid_buf[30] ;                                                // /!\ cannot use ::string as we are only allowed signal-safe functions
			int                      first_pid_sz      = sprintf(first_pid_buf,"%d",first_pid-1  )    ; // /!\ .
			AcFd                     fd                { "/proc/sys/kernel/ns_last_pid" , Fd::Write } ;
			[[maybe_unused]] ssize_t _                 = ::write(fd,first_pid_buf,first_pid_sz)       ; // dont care about errors, this is best effort
		}
		pid_t pid = ::clone( _s_do_child , _child_stack_ptr , SIGCHLD , this ) ;
		//
		if (pid==-1) _exit(Rc::System,"cannot spawn sub-process") ;
		for(;;) {
			int   wstatus   ;
			pid_t child_pid = ::wait(&wstatus) ;
			if (child_pid==pid) {                                                                       // XXX : find a way to simulate a caught signal rather than exit 128+sig
				if (WIFEXITED  (wstatus)) ::_exit(    WEXITSTATUS(wstatus)) ;                           // exit as transparently as possible
				if (WIFSIGNALED(wstatus)) ::_exit(128+WTERMSIG   (wstatus)) ;                           // cannot kill self to be transparent as we are process 1, mimic bash
				SWEAR( WIFSTOPPED(wstatus) || WIFCONTINUED(wstatus) , wstatus ) ;                       // ensure we have not forgotten a case
			}
		}
	} else {
		_do_child() ;
	}
}

void Child::spawn() {
	SWEAR( +cmd_line                                                            ) ;
	SWEAR( !stdin_fd  || stdin_fd ==Fd::Stdin  || stdin_fd >Fd::Std , stdin_fd  ) ;                                          // ensure reasonably simple situations
	SWEAR( !stdout_fd || stdout_fd>=Fd::Stdout                      , stdout_fd ) ;                                          // .
	SWEAR( !stderr_fd || stderr_fd>=Fd::Stdout                      , stderr_fd ) ;                                          // .
	SWEAR( !( stderr_fd==Fd::Stdout && stdout_fd==Fd::Stderr )                  ) ;                                          // .
	if (stdin_fd ==PipeFd) _p2c .open() ; else if (+stdin_fd ) _p2c .read  = stdin_fd  ;
	if (stdout_fd==PipeFd) _c2po.open() ; else if (+stdout_fd) _c2po.write = stdout_fd ;
	if (stderr_fd==PipeFd) _c2pe.open() ; else if (+stderr_fd) _c2pe.write = stderr_fd ;
	//
	// /!\ memory for environment must be allocated before calling clone
	::vector_s env_vector ;                                                                                                  // ensure actual env strings (of the form name=val) lifetime
	if (env) {
		size_t n_env = env->size() + (add_env?add_env->size():0) ;
		env_vector.reserve(n_env) ;
		_child_env = new const char*[n_env+1] ;
		size_t i = 0 ;
		/**/         for( auto const& [k,v] : *env     ) { env_vector.push_back(k+'='+v) ; _child_env[i++] = env_vector.back().c_str() ; }
		if (add_env) for( auto const& [k,v] : *add_env ) { env_vector.push_back(k+'='+v) ; _child_env[i++] = env_vector.back().c_str() ; }
		/**/                                                                               _child_env[i  ] = nullptr                   ;
	} else if (add_env) {
		size_t n_env = add_env->size() ; for( char** e=environ ; *e ; e++ ) n_env++ ;
		env_vector.reserve(add_env->size()) ;
		_child_env = new const char*[n_env+1] ;
		size_t i = 0 ;
		for( char** e=environ ; *e ; e++  )                                   _child_env[i++] = *e                        ;
		for( auto const& [k,v] : *add_env ) { env_vector.push_back(k+'='+v) ; _child_env[i++] = env_vector.back().c_str() ; }
		/**/                                                                  _child_env[i  ] = nullptr                   ;
	} else {
		_child_env = const_cast<const char**>(environ) ;
	}
	//
	// /!\ memory for args must be allocated before calling clone
	_child_args = new const char*[cmd_line.size()+1] ;
	{	size_t i = 0 ;
		for( ::string const& a : cmd_line ) _child_args[i++] = a.c_str() ;
		/**/                                _child_args[i  ] = nullptr   ;
	}
	//
	// /!\ memory for child stack must be allocated before calling clone
	::vector<uint64_t> child_stack ( StackSz/sizeof(uint64_t) ) ;
	_child_stack_ptr = child_stack.data()+(NpStackGrowsDownward?child_stack.size():0) ;
	//
	if (first_pid) {
		::vector<uint64_t> trampoline_stack     ( StackSz/sizeof(uint64_t) )                                               ; // we need a trampoline stack if we launch a grand-child
		void*              trampoline_stack_ptr = trampoline_stack.data()+(NpStackGrowsDownward?trampoline_stack.size():0) ; // .
		pid = ::clone( _s_do_child_trampoline , trampoline_stack_ptr , SIGCHLD|CLONE_NEWPID|CLONE_NEWNS , this ) ;           // CLONE_NEWNS is important to mount the new /proc without disturing caller
	} else {
		pid = ::clone( _s_do_child_trampoline , _child_stack_ptr     , SIGCHLD                          , this ) ;
	}
	//
	if (pid==-1) {
		waited() ;                                                                                                           // ensure we can be destructed
		throw cat("cannot spawn process ",cmd_line," : ",::strerror(errno)) ;
	}
	//
	if (stdin_fd ==PipeFd) { stdin  = _p2c .write ; _p2c .read .close() ; }
	if (stdout_fd==PipeFd) { stdout = _c2po.read  ; _c2po.write.close() ; }
	if (stderr_fd==PipeFd) { stderr = _c2pe.read  ; _c2pe.write.close() ; }
}
