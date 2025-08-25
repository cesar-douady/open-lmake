// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "process.hh"

using namespace Disk ;

::string wstatus_str(int wstatus) {
	if (WIFEXITED(wstatus)) {
		int rc = WEXITSTATUS(wstatus) ;
		if ( rc==0                               ) return     "ok"                                                        ;
		if ( int sig=rc-128 ; sig>=0 && sig<NSIG ) return cat("exit ",rc," (could be signal ",sig,'-',strsignal(sig),')') ;
		/**/                                       return cat("exit ",rc                                                ) ;
	}
	if (WIFSIGNALED(wstatus)) {
		int sig = WTERMSIG(wstatus) ;
		return cat("signal ",sig,'-',::strsignal(sig)) ;
	}
	return "??" ;
}

bool/*done*/ kill_process( pid_t pid , int sig , bool as_group ) {
	swear_prod(pid>1,"killing process",pid) ;                                   // /!\ ::kill(-1) sends signal to all possible processes, ensure no system wide catastrophe
	//
	if (!as_group          ) return ::kill(pid,sig)==0 ;
	if (::kill(-pid,sig)==0) return true               ;                        // fast path : group exists, nothing else to do
	bool proc_killed  = ::kill( pid,sig)==0 ;                                   // else, there may be another possibility : the process to kill might not have had enough time to call setpgid(0,0) ...
	bool group_killed = ::kill(-pid,sig)==0 ;                                   // ... that makes it be a group, so kill it as a process, and kill the group again in case it was created inbetween
	return proc_killed || group_killed ;
}

pid_t get_ppid(pid_t pid) {
	::string status_file = cat("/proc/",pid,"/status") ;
	::string status = AcFd(status_file).read() ;
	//
	size_t start = status.find("\nPPid:") ;
	throw_unless( start!=Npos , "bad format in ",status_file ) ;
	start += strlen("\nPPid:") ;
	while (is_space(status[start])) start++ ;
	//
	size_t end = status.find('\n',start) ;
	throw_unless( end!=Npos , "bad format in ",status_file ) ;
	//
	try         { return from_string<pid_t>(substr_view( status , start , end-start )) ; }
	catch (...) { throw cat("bad format in ",status_file)                              ; }
}

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
	SWEAR(_child_args[0]) ;
	::execve( _child_args[0] , const_cast<char**>(_child_args) , const_cast<char**>(_child_env) ) ;
	_exit(Rc::System,"cannot exec") ;                                                               // in case exec fails
}

// /!\ this function must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
// /!\ this function must not modify anything outside its local frame as it may be called as pre_exec under ::vfork()
[[noreturn]] void Child::_do_child_trampoline() {
	if (as_session) ::setsid() ;                  // if we are here, we are the init process and we must be in the new session to receive the kill signal
	if (nice) {
		[[maybe_unused]] int nice_val ;                                                                  // ignore error if any, as we cant do much about it
		if (!as_session)
			/**/                       nice_val = ::nice(nice) ;
		else
			// as_session creates a new autogroup, apply nice_val to it, not between processes within it
			try                      { AcFd("/proc/self/autogroup",FdAction::Write).write(cat(nice)) ; }
			catch (::string const&e) { nice_val = ::nice(nice) ;                                       } // best effort
	}
	//
	sigset_t full_mask ; ::sigfillset(&full_mask) ;                          // sig fillset may be a macro
	::sigprocmask(SIG_UNBLOCK,&full_mask,nullptr/*oldset*/) ;                // restore default behavior
	//
	if (stdin_fd ==PipeFd) { ::close(_p2c .write) ; _p2c .read .no_std() ; } // could be optimized, but too complex to manage
	if (stdout_fd==PipeFd) { ::close(_c2po.read ) ; _c2po.write.no_std() ; } // .
	if (stderr_fd==PipeFd) { ::close(_c2pe.read ) ; _c2pe.write.no_std() ; } // .
	// set up std fd
	if (stdin_fd ==NoneFd) ::close(Fd::Stdin ) ; else if (                      _p2c .read !=Fd::Stdin  ) ::dup2(_p2c .read ,Fd::Stdin ) ;
	if (stdout_fd==NoneFd) ::close(Fd::Stdout) ; else if ( stdout_fd!=JoinFd && _c2po.write!=Fd::Stdout ) ::dup2(_c2po.write,Fd::Stdout) ;
	if (stderr_fd==NoneFd) ::close(Fd::Stderr) ; else if ( stderr_fd!=JoinFd && _c2pe.write!=Fd::Stderr ) ::dup2(_c2pe.write,Fd::Stderr) ;
	//
	if      (stdout_fd==JoinFd) { SWEAR(stderr_fd!=JoinFd) ; ::dup2(Fd::Stderr,Fd::Stdout) ; }
	else if (stderr_fd==JoinFd)                              ::dup2(Fd::Stdout,Fd::Stderr) ;
	//
	if (_p2c .read >Fd::Std) ::close(_p2c .read ) ;                          // clean up : we only want to set up standard fd, other ones are necessarily temporary constructions
	if (_c2po.write>Fd::Std) ::close(_c2po.write) ;                          // .
	if (_c2pe.write>Fd::Std) ::close(_c2pe.write) ;                          // .
	//
	if (+cwd_s  ) { if (::chdir(no_slash(cwd_s).c_str())!=0) _exit(Rc::System,"cannot chdir"      ) ; }
	if (pre_exec) { if (pre_exec(pre_exec_arg)          !=0) _exit(Rc::Fail  ,"cannot setup child") ; }
	//
	#if HAS_CLOSE_RANGE
		//::close_range(3/*first*/,~0u/*last*/,CLOSE_RANGE_UNSHARE) ;        // activate this code (uncomment) as an alternative to set CLOEXEC in Fd(::string)
	#endif
	//
	if (first_pid) {
		SWEAR(first_pid>1,first_pid) ;                                                                        // START_OF_NO_COV coverage recording does not work in isolated namespace
		// mount is not signal-safe and we are only allowed signal-safe functions here, but this is a syscall, should be ok
		if (::mount(nullptr/*source*/,"/proc","proc",0/*flags*/,nullptr/*data*/)!=0) {
			::perror("cannot mount /proc ") ;
			_exit(Rc::System,"cannot mount /proc") ;
		}
		{	char                     first_pid_buf[30] ;                                                      // /!\ cannot use ::string as we are only allowed signal-safe functions
			int                      first_pid_sz      = sprintf(first_pid_buf,"%d",first_pid-1  )          ; // /!\ .
			AcFd                     fd                { "/proc/sys/kernel/ns_last_pid" , FdAction::Write } ;
			[[maybe_unused]] ssize_t _                 = ::write(fd,first_pid_buf,first_pid_sz)             ; // dont care about errors, this is best effort
		}
		pid_t pid = ::vfork() ;                                                                               // NOLINT(clang-analyzer-security.insecureAPI.vfork) faster than anything else
		if (pid==0 ) _do_child() ;                                                                            // in child
		if (pid==-1) _exit(Rc::System,"cannot spawn sub-process") ;
		//
		for(;;) {
			int   wstatus   ;
			pid_t child_pid = ::wait(&wstatus) ;
			if (child_pid==pid) {                                                                             // XXX! : find a way to simulate a caught signal rather than exit 128+sig (mimic bash)
				if (WIFEXITED  (wstatus)) ::_exit(    WEXITSTATUS(wstatus)) ;                                 // exit as transparently as possible
				if (WIFSIGNALED(wstatus)) ::_exit(128+WTERMSIG   (wstatus)) ;                                 // cannot kill self to be transparent as we are process 1, mimic bash
				SWEAR( WIFSTOPPED(wstatus) || WIFCONTINUED(wstatus) , wstatus ) ;                             // ensure we have not forgotten a case
			}
		}                                                                                                     // END_OF_NO_COV
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
		_child_env     = new const char*[n_env+1] ;
		_own_child_env = true                     ;
		size_t i = 0 ;
		/**/         for( auto const& [k,v] : *env     ) { env_vector.push_back(k+'='+v) ; _child_env[i++] = env_vector.back().c_str() ; }
		if (add_env) for( auto const& [k,v] : *add_env ) { env_vector.push_back(k+'='+v) ; _child_env[i++] = env_vector.back().c_str() ; }
		/**/                                                                               _child_env[i  ] = nullptr                   ;
	} else if (add_env) {
		size_t n_env = add_env->size() ; for( char** e=environ ; *e ; e++ ) n_env++ ;
		env_vector.reserve(add_env->size()) ;
		_child_env     = new const char*[n_env+1] ;
		_own_child_env = true                     ;
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
	if (first_pid) {
		::vector<uint64_t> trampoline_stack     ( StackSz/sizeof(uint64_t) )                                               ; // we need a trampoline stack if we launch a grand-child
		void*              trampoline_stack_ptr = trampoline_stack.data()+(STACK_GROWS_DOWNWARD?trampoline_stack.size():0) ; // .
		pid = ::clone( _s_do_child_trampoline , trampoline_stack_ptr , SIGCHLD|CLONE_NEWPID|CLONE_NEWNS , this ) ;           // CLONE_NEWNS is passed to mount a new /proc without disturing caller
	} else {
		// pre_exec may modify parent's memory
		pid_t pid_ = pre_exec ? ::fork() : ::vfork() ;                        // NOLINT(clang-analyzer-security.insecureAPI.vfork,clang-analyzer-unix.Vfork) faster than anything else
		if (pid_==0) _do_child_trampoline() ;                                 // in child
		pid = pid_ ;                                                          // only parent can modify parent's memory
	}
	//
	if (pid==-1) {
		waited() ;                                                            // NO_COV defensive programming, ensure we can be destructed
		throw cat("cannot spawn process ",cmd_line," : ",::strerror(errno)) ; // NO_COV .
	}
	//
	if (stdin_fd ==PipeFd) { stdin  = _p2c .write ; _p2c .read .close() ; }
	if (stdout_fd==PipeFd) { stdout = _c2po.read  ; _c2po.write.close() ; }
	if (stderr_fd==PipeFd) { stderr = _c2pe.read  ; _c2pe.write.close() ; }
}
