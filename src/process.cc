// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "process.hh"

bool/*parent*/ Child::spawn(
	bool            as_session_ , ::vector_s const& args
,	Fd              stdin_fd    , Fd                stdout_fd , Fd stderr_fd
,	::map_ss const* env         , ::map_ss   const* add_env
,	::string const& cwd_
,	void (*pre_exec)()
) {
	SWEAR( !stdin_fd  || stdin_fd ==Fd::Stdin  || stdin_fd >Fd::Std , stdin_fd  ) ;                                 // ensure reasonably simple situations
	SWEAR( !stdout_fd || stdout_fd>=Fd::Stdout                      , stdout_fd ) ;                                 // .
	SWEAR( !stderr_fd || stderr_fd>=Fd::Stdout                      , stderr_fd ) ;                                 // .
	SWEAR(!( stderr_fd==Fd::Stdout && stdout_fd==Fd::Stderr )                   ) ;                                 // .
	::Pipe p2c  ;
	::Pipe c2po ;
	::Pipe c2pe ;
	if (stdin_fd ==Pipe) p2c .open() ; else if (+stdin_fd ) p2c .read  = stdin_fd  ;
	if (stdout_fd==Pipe) c2po.open() ; else if (+stdout_fd) c2po.write = stdout_fd ;
	if (stderr_fd==Pipe) c2pe.open() ; else if (+stderr_fd) c2pe.write = stderr_fd ;
	as_session = as_session_ ;
	pid        = fork()      ;
	if (!pid) {                                                                                                     // if in child
		if (as_session) ::setsid() ;
		sigset_t full_mask ; ::sigfillset(&full_mask) ;
		::sigprocmask(SIG_UNBLOCK,&full_mask,nullptr) ;                                                             // restore default behavior
		//
		if (stdin_fd ==Pipe) { p2c .write.close() ; p2c .read .no_std() ; }                                         // could be optimized, but too complex to manage
		if (stdout_fd==Pipe) { c2po.read .close() ; c2po.write.no_std() ; }                                         // .
		if (stderr_fd==Pipe) { c2pe.read .close() ; c2pe.write.no_std() ; }                                         // .
		// set up std fd
		if (stdin_fd ==None) ::close(Fd::Stdin ) ; else if (p2c .read !=Fd::Stdin ) ::dup2(p2c .read ,Fd::Stdin ) ;
		if (stdout_fd==None) ::close(Fd::Stdout) ; else if (c2po.write!=Fd::Stdout) ::dup2(c2po.write,Fd::Stdout) ; // save stdout in case it is modified and we want to redirect stderr to it
		if (stderr_fd==None) ::close(Fd::Stderr) ; else if (c2pe.write!=Fd::Stderr) ::dup2(c2pe.write,Fd::Stderr) ;
		//
		if (p2c .read >Fd::Std) p2c .read .close() ;                   // clean up : we only want to set up standard fd, other ones are necessarily temporary constructions
		if (c2po.write>Fd::Std) c2po.write.close() ;                   // .
		if (c2pe.write>Fd::Std) c2pe.write.close() ;                   // .
		//
		const char** child_env  = const_cast<const char**>(environ) ;
		::vector_s   env_vector ;                                      // ensure actual env strings lifetime last until execve call
		if (env) {
			SWEAR(+args) ;                                             // cannot fork with env
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
		} else {
			if (add_env) for( auto const& [k,v] : *add_env ) set_env(k,v) ;
		}
		if (+cwd_   ) { if (::chdir(cwd_.c_str())!=0) throw to_string("cannot chdir to : " ,cwd_) ; }
		if (pre_exec)   pre_exec() ;
		//
		if (!args) return false ;
		#if HAS_CLOSE_RANGE
			//::close_range(3,~0u,CLOSE_RANGE_UNSHARE) ;               // activate this code (uncomment) as an alternative to set CLOEXEC in IFStream/OFStream
		#endif
		const char** child_args = new const char*[args.size()+1] ;
		for( size_t i=0 ; i<args.size() ; i++ ) child_args[i] = args[i].c_str() ;
		child_args[args.size()] = nullptr ;
		if (env) ::execve( child_args[0] , const_cast<char**>(child_args) , const_cast<char**>(child_env) ) ;
		else     ::execv ( child_args[0] , const_cast<char**>(child_args)                                 ) ;
		pid = 0 ;
		exit(Rc::System,"cannot exec (",strerror(errno),") : ",args) ; // in case exec fails
	}
	if (stdin_fd ==Pipe) { stdin  = p2c .write ; p2c .read .close() ; }
	if (stdout_fd==Pipe) { stdout = c2po.read  ; c2po.write.close() ; }
	if (stderr_fd==Pipe) { stderr = c2pe.read  ; c2pe.write.close() ; }
	return true ;
}
