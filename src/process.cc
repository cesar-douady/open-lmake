// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h>
#include <sys/mount.h>

#include "process.hh"

[[noreturn]] void Child::_do_child() {
	if ( !first_pid && as_session ) ::setsid() ;                          // if first_pid, we have an init process above us and this is the one which calls setsid()
	sigset_t full_mask ; ::sigfillset(&full_mask) ;
	::sigprocmask(SIG_UNBLOCK,&full_mask,nullptr) ;                       // restore default behavior
	//
	if (stdin_fd ==Pipe) { _p2c .write.close() ; _p2c .read .no_std() ; } // could be optimized, but too complex to manage
	if (stdout_fd==Pipe) { _c2po.read .close() ; _c2po.write.no_std() ; } // .
	if (stderr_fd==Pipe) { _c2pe.read .close() ; _c2pe.write.no_std() ; } // .
	// set up std fd
	if (stdin_fd ==None) ::close(Fd::Stdin ) ; else if (_p2c .read !=Fd::Stdin ) ::dup2(_p2c .read ,Fd::Stdin ) ;
	if (stdout_fd==None) ::close(Fd::Stdout) ; else if (_c2po.write!=Fd::Stdout) ::dup2(_c2po.write,Fd::Stdout) ; // save stdout in case it is modified and we want to redirect stderr to it
	if (stderr_fd==None) ::close(Fd::Stderr) ; else if (_c2pe.write!=Fd::Stderr) ::dup2(_c2pe.write,Fd::Stderr) ;
	//
	if (_p2c .read >Fd::Std) _p2c .read .close() ;                 // clean up : we only want to set up standard fd, other ones are necessarily temporary constructions
	if (_c2po.write>Fd::Std) _c2po.write.close() ;                 // .
	if (_c2pe.write>Fd::Std) _c2pe.write.close() ;                 // .
	//
	const char** child_env   = const_cast<const char**>(environ) ;
	::vector_s   env_vector  ;                                     // ensure actual env strings lifetime last until execve call
	int          pre_exec_rc = 0/*garbage*/                      ;
	if (env) {
		SWEAR(+cmd_line) ;                                         // cannot fork with env
		size_t n_env = env->size() + (add_env?add_env->size():0) ;
		env_vector.reserve(n_env) ;
		for( auto const* e : {env,add_env} )
			if (e)
				for( auto const& [k,v] : *e )
					env_vector.push_back(k+'='+v) ;
		child_env = new const char*[n_env+1] ;
		// /!\ : c_str() seems to be invalidated by vector reallocation although this does not appear in doc : https://en.cppreference.com/w/cpp/string/basic_string/c_str
		for( size_t i=0 ; i<n_env ; i++ ) child_env[i] = env_vector[i].c_str() ;
		child_env[n_env] = nullptr ;
	} else if (add_env) {
		for( auto const& [k,v] : *add_env ) set_env(k,v) ;
	}
	if (+cwd_   ) { if (::chdir(cwd_.c_str())!=0) exit(Rc::System,"cannot chdir to : "+cwd_) ; }
	if (pre_exec)   pre_exec_rc = pre_exec(pre_exec_arg) ;
	//
	if (!cmd_line  ) exit(pre_exec_rc?Rc::Fail:Rc::Ok) ;           // no cmd_line  , pre_exec is the entire function executed in child
	if (pre_exec_rc) exit(            Rc::Fail       ) ;           // with cmd_line, check no prelude error
	#if HAS_CLOSE_RANGE
		//::close_range(3,~0u,CLOSE_RANGE_UNSHARE) ;               // activate this code (uncomment) as an alternative to set CLOEXEC in IFStream/OFStream
	#endif
	const char** child_args = new const char*[cmd_line.size()+1] ;
	for( size_t i=0 ; i<cmd_line.size() ; i++ ) child_args[i] = cmd_line[i].c_str() ;
	child_args[cmd_line.size()] = nullptr ;
	if (env) ::execve( child_args[0] , const_cast<char**>(child_args) , const_cast<char**>(child_env) ) ;
	else     ::execv ( child_args[0] , const_cast<char**>(child_args)                                 ) ;
	exit(Rc::System,"cannot exec (",strerror(errno),") : ",cmd_line) ;                                    // in case exec fails
}

static constexpr size_t StackSz = 16<<10 ; // we just need s small stack before exec, experiment shows 8k is enough, take 16k

[[noreturn]] void Child::_do_child_new_pid_namespace() {
	if (as_session) ::setsid() ;                         // if we are here, we are the init process and we must be in the new session to receive the kill signal
	SWEAR(first_pid>1,first_pid) ;
	if (::mount(nullptr,"/proc","proc",0,nullptr)!=0) exit(Rc::System,"cannot mount /proc") ;
	{	AutoCloseFd              fd  = ::open("/proc/sys/kernel/ns_last_pid",O_WRONLY|O_TRUNC) ;
		::string                 val = ::to_string(first_pid-1)                                ;
		[[maybe_unused]] ssize_t wrc = ::write(fd,val.c_str(),val.size())                      ;      // dont care about errors, this is best effort
	}
	::vector<uint64_t> stack     ( StackSz/sizeof(uint64_t) )                       ;
	void*              stack_ptr = stack.data()+(StackGrowsDownward?stack.size():0) ;
	pid_t pid = ::clone( _s_do_child , stack_ptr , SIGCHLD , this ) ;
	//
	if (pid==-1) exit(Rc::System,"cannot spawn sub-process ",cmd_line) ;
	for(;;) {
		int   wstatus   ;
		pid_t child_pid = ::wait(&wstatus) ;
		if (child_pid==pid) {
			if (WIFEXITED  (wstatus))   ::exit (WEXITSTATUS(wstatus)) ;                               // exit as transparently as possible
			if (WIFSIGNALED(wstatus)) { ::raise(WTERMSIG   (wstatus)) ; raise(SIGABRT) ; }            // .
			SWEAR( WIFSTOPPED(wstatus) || WIFCONTINUED(wstatus) ) ;                                   // ensure we have not forgotten a case
		}
	}
}

void Child::spawn() {
	SWEAR( !stdin_fd  || stdin_fd ==Fd::Stdin  || stdin_fd >Fd::Std , stdin_fd  ) ;    // ensure reasonably simple situations
	SWEAR( !stdout_fd || stdout_fd>=Fd::Stdout                      , stdout_fd ) ;    // .
	SWEAR( !stderr_fd || stderr_fd>=Fd::Stdout                      , stderr_fd ) ;    // .
	SWEAR(!( stderr_fd==Fd::Stdout && stdout_fd==Fd::Stderr )                   ) ;    // .
	if (stdin_fd ==Pipe) _p2c .open() ; else if (+stdin_fd ) _p2c .read  = stdin_fd  ;
	if (stdout_fd==Pipe) _c2po.open() ; else if (+stdout_fd) _c2po.write = stdout_fd ;
	if (stderr_fd==Pipe) _c2pe.open() ; else if (+stderr_fd) _c2pe.write = stderr_fd ;
	//
	::vector<uint64_t> stack     ( StackSz/sizeof(uint64_t) )                       ;
	void*              stack_ptr = stack.data()+(StackGrowsDownward?stack.size():0) ;
	if (first_pid) pid = ::clone( _s_do_child_new_pid_namespace , stack_ptr , SIGCHLD|CLONE_NEWPID , this ) ;
	else           pid = ::clone( _s_do_child                   , stack_ptr , SIGCHLD              , this ) ;
	//
	if (pid==-1) {
		pid = 0 ;                                                                      // ensure we can be destructed
		throw "cannot spawn process "+fmt_string(cmd_line)+" : "+strerror(errno) ;
	}
	//
	if (stdin_fd ==Pipe) { stdin  = _p2c .write ; _p2c .read .close() ; }
	if (stdout_fd==Pipe) { stdout = _c2po.read  ; _c2po.write.close() ; }
	if (stderr_fd==Pipe) { stderr = _c2pe.read  ; _c2pe.write.close() ; }
}
