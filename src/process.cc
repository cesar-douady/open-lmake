// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"
#include "trace.hh"

#include "process.hh"

using namespace Disk ;
using namespace Time ;

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
	swear_prod(pid>1,"killing process",pid) ;                      // /!\ ::kill(-1) sends signal to all possible processes, ensure no system wide catastrophe
	//
	if (!as_group          ) return ::kill(pid,sig)==0 ;
	if (::kill(-pid,sig)==0) return true               ;           // fast path : group exists, nothing else to do
	bool proc_killed  = ::kill( pid,sig)==0 ;                      // else, there may be another possibility : the process to kill might not have had enough time to call setpgid(0,0) ...
	bool group_killed = ::kill(-pid,sig)==0 ;                      // ... that makes it be a group, so kill it as a process, and kill the group again in case it was created inbetween
	return proc_killed || group_killed ;
}

pid_t get_ppid(pid_t pid) {
	::string status_file = cat("/proc/",pid,"/status") ;
	::string status      = AcFd(status_file).read()    ;
	//
	size_t start = status.find("\nPPid:") ; throw_unless( start!=Npos , "bad format in ",status_file ) ;
	start += strlen("\nPPid:") ;
	//
	size_t end = status.find('\n',start) ; throw_unless( end!=Npos , "bad format in ",status_file ) ;
	//
	try         { return from_string<pid_t>(substr_view( status , start , end-start )) ; }
	catch (...) { throw cat("bad format in ",status_file)                              ; }
}

[[noreturn]] void Child::_exit( Rc rc , const char* msg1 , const char* msg2 ) {                                                            // signal-safe
	bool        ok = true              ;
	const char* e  = ::strerror(errno) ;
	// /!\ cannot use high level I/O because we are only allowed signal-safe functions
	/**/                                                                       ok &= ::write(2,"cannot spawn (",14                )>=0 ;
	/**/                                 { size_t l=::strlen(e             ) ; ok &= ::write(2,e               ,l                 )>=0 ; }
	/**/                                                                       ok &= ::write(2,") "            ,2                 )>=0 ;
	if ( _child_args && _child_args[0] ) { size_t l=::strlen(_child_args[0]) ; ok &= ::write(2,_child_args[0]  ,l                 )>=0 ; }
	if ( _child_args && _child_args[0] )                                       ok &= ::write(2," : "           ,3                 )>=0 ;
	if ( msg1                          ) { size_t l=::strlen(msg1          ) ; ok &= ::write(2,msg1            ,l-(msg1[l-1]=='/'))>=0 ; } // suppress ending /
	if ( msg2                          ) { size_t l=::strlen(msg2          ) ; ok &= ::write(2,msg2            ,l-(msg2[l-1]=='/'))>=0 ; } // .
	/**/                                                                       ok &= ::write(2,"\n"            ,1                 )>=0 ;
	if (!ok) rc = Rc::System ;
	::_exit(+rc) ;             // /!\ cannot use exit as we are only allowed signal-safe functions
}

void Child::spawn() {
	SWEAR(+cmd_line) ;
	switch (stdin .fd) { case NoneFd.fd : case PipeFd.fd :                  case Fd::Stdin .fd :                      break ; default : SWEAR( stdin >Fd::Std , stdin  ) ; }
	switch (stdout.fd) { case NoneFd.fd : case PipeFd.fd :                  case Fd::Stdout.fd : case Fd::Stderr.fd : break ; default : SWEAR( stdout>Fd::Std , stdout ) ; }
	switch (stderr.fd) { case NoneFd.fd : case PipeFd.fd : case JoinFd.fd : case Fd::Stdout.fd : case Fd::Stderr.fd : break ; default : SWEAR( stderr>Fd::Std , stderr ) ; }
	SWEAR( !( stderr==Fd::Stdout && stdout==Fd::Stderr ) ) ;
	if (stdin ==PipeFd) { _p2c .open() ; _p2c .no_std() ; }
	if (stdout==PipeFd) { _c2po.open() ; _c2po.no_std() ; }
	if (stderr==PipeFd) { _c2pe.open() ; _c2pe.no_std() ; }
	//
	// /!\ memory for environment must be allocated before calling clone
	::vector_s            env_str_vec ;                                                              // ensure actual env strings (of the form name=val) lifetime
	::vector<const char*> env_vec     ;
	if ( env || add_env ) {
		env_str_vec.reserve( (env?env->size():  0) + (add_env?add_env->size():0)                 ) ; // a little bit too large in case of key conflict, no harm
		env_vec    .reserve( (env?env->size():100) + (add_env?add_env->size():0) + 1/*sentinel*/ ) ; // rough approximation if !env, better than nothing
		//
		if (add_env) {
			if (env) for( auto const& [k,v] : *env     ) { if (                                  !(      add_env->contains(k                ) ) ) env_str_vec.push_back(k+'='+v) ; }
			else     for( char** e=environ ; *e ; e++  ) { if ( const char* p=::strchr(*e,'=') ; !( p && add_env->contains({*e,size_t(p-*e)}) ) ) env_vec    .push_back(*e     ) ; }
			/**/     for( auto const& [k,v] : *add_env )                                                                                          env_str_vec.push_back(k+'='+v) ;
		} else {
			if (env) for( auto const& [k,v] : *env     ) {                                                                                        env_str_vec.push_back(k+'='+v) ; }
			else     for( char** e=environ ; *e ; e++  ) {                                                                                        env_vec    .push_back(*e     ) ; }
		}
		//
		for( ::string const& e : env_str_vec ) env_vec.push_back(e.c_str()          ) ;
		/**/                                   env_vec.push_back(nullptr/*sentinel*/) ;
		_child_env = env_vec.data() ;
	} else {
		_child_env = const_cast<const char**>(environ) ;
	}
	// /!\ memory for args must be allocated before calling clone
	::vector<const char*> cmd_line_vec ; cmd_line_vec.reserve(cmd_line.size()+1) ;                   // account for sentinel
	for( ::string const& c : cmd_line ) cmd_line_vec.push_back(c.c_str()) ;
	/**/                                cmd_line_vec.push_back(nullptr  ) ;                          // sentinel
	_child_args = cmd_line_vec.data() ;
	// pre_exec may modify parent's memory
	pid_t       pid_       = pre_exec ? ::fork() : ::vfork() ; // NOLINT(clang-analyzer-security.insecureAPI.vfork,clang-analyzer-unix.Vfork) faster than anything else
	::string    nice_str   = cat(nice)                       ;
	const char* nice_c_str = nice_str.c_str()                ;
	if (pid_==0) {                                             // in child
		// /!\ this section must be malloc free as malloc takes a lock that may be held by another thread at the time process is cloned
		if (as_session) ::setsid() ;                         // if we are here, we are the init process and we must be in the new session to receive the kill signal
		if (nice) {
			/**/             int fd  = ::open ( "/proc/self/autogroup" , O_WRONLY|O_TRUNC ) ;           // ignore error if any, as we cant do much about it
			[[maybe_unused]] int rc1 = ::write( fd , nice_c_str , nice_str.size()         ) ;           // .
			/**/                       ::close( fd                                        ) ;           // .
			[[maybe_unused]] int rc2 = ::nice ( nice                                      ) ;           // .
		}
		//
		sigset_t full_mask ; ::sigfillset(&full_mask) ;                                                 // sigfillset may be a macro
		::sigprocmask(SIG_UNBLOCK,&full_mask,nullptr/*oldset*/) ;                                       // restore default behavior
		//
		switch (stdin.fd) {
			case NoneFd.fd    : ::close(Fd::Stdin ) ;                                                    break ;
			case PipeFd.fd    : ::close(_p2c.write) ; ::dup2(_p2c.read,Fd::Stdin) ; ::close(_p2c.read) ; break ;
			case Fd::Stdin.fd :                                                                          break ;
			default           :                       ::dup2(stdin    ,Fd::Stdin) ;
		}
		switch (stdout.fd) {
			case NoneFd.fd     : ::close(Fd::Stdout) ;                                                         break ;
			case PipeFd.fd     : ::close(_c2po.read) ; ::dup2(_c2po.write,Fd::Stdout) ; ::close(_c2po.write) ; break ;
			case Fd::Stdout.fd :                                                                               break ;
			default            :                       ::dup2(stdout     ,Fd::Stdout) ;
		}
		switch (stderr.fd) {
			case NoneFd.fd     : ::close(Fd::Stderr) ;                                                         break ;
			case PipeFd.fd     : ::close(_c2pe.read) ; ::dup2(_c2pe.write,Fd::Stderr) ; ::close(_c2pe.write) ; break ;
			case Fd::Stderr.fd :                                                                               break ;
			default            :                       ::dup2(stderr     ,Fd::Stderr) ;
		}
		if (+cwd_s  ) { if (::chdir(cwd_s.c_str())!=0) _exit( Rc::System , "cannot chdir to ",cwd_s.c_str() ) ; }
		if (pre_exec) { if (pre_exec(pre_exec_arg)!=0) _exit( Rc::Fail   , "cannot setup child"             ) ; }
		//
		#if HAS_CLOSE_RANGE
			//::close_range(3/*first*/,~0u/*last*/,CLOSE_RANGE_UNSHARE) ;                               // activate this code (uncomment) as an alternative to set CLOEXEC in Fd(::string)
		#endif
		//
		SWEAR(_child_args[0]) ;
		::execve( _child_args[0] , const_cast<char**>(_child_args) , const_cast<char**>(_child_env) ) ;
		_exit(Rc::System,"cannot exec") ;                                                               // in case exec fails
	}
	pid = pid_ ;                                                                                        // only parent can modify parent's memory
	//
	if (pid==-1) {
		waited() ;                                                                                      // NO_COV defensive programming, ensure we can be destructed
		throw cat("cannot spawn process ",cmd_line," : ",StrErr()) ;                                    // NO_COV .
	}
	//
	if      (stdin ==PipeFd) { stdin  = _p2c .write ; _p2c .read .close() ; }
	if      (stdout==PipeFd) { stdout = _c2po.read  ; _c2po.write.close() ; }
	if      (stderr==PipeFd) { stderr = _c2pe.read  ; _c2pe.write.close() ; }
	else if (stderr==JoinFd)   stderr = stdout      ;
}

static ::pair_s/*fqdn*/<pid_t> _get_mrkr(::string const& server_mrkr) {
	try {
		::vector_s lines = AcFd(server_mrkr).read_lines() ; throw_unless(lines.size()==2) ;
		return { SockFd::s_host(lines[0]/*service*/) , from_string<pid_t>(lines[1]) } ; }
	catch (::string const&) {
		return { {}/*fqdn*/ , 0/*pid*/ } ;
	}
}
static ::string _g_server_mrkr ;
static void _server_cleanup() {
	Trace trace("_server_cleanup") ;
	unlnk(File(_g_server_mrkr)) ;
}
void AutoServerBase::start() {
	::pair_s<pid_t> mrkr      { fqdn() , ::getpid() }  ;
	::pair_s<pid_t> file_mrkr = _get_mrkr(server_mrkr) ;
	Trace trace("start_server",mrkr,file_mrkr) ;
	if ( +file_mrkr.first && file_mrkr.first!=mrkr.first ) {
		trace("already_existing_elsewhere",file_mrkr) ;
		throw ::pair_s<Rc>( {}/*msg*/ , Rc::Ok ) ;
	}
	if (file_mrkr.second) {
		if (sense_process(file_mrkr.second)) {                                  // another server exists on same host
			trace("already_existing",file_mrkr) ;
			throw ::pair_s<Rc>( {}/*msg*/ , Rc::Ok ) ;
		}
		unlnk(File(server_mrkr)) ;                                              // before doing anything, we create the marker, which is unlinked at the end, so it is a marker of a crash
		rescue = true ;
		trace("vanished",file_mrkr) ;
	}
	server_fd = { 0/*backlog*/ , false/*reuse_addr*/ } ;
	if (!is_daemon) Fd::Stdout.write(serialize(server_fd.service(0/*addr*/))) ; // pass connection info to client, no need for addr as client is necessarily local
	::close(Fd::Stdout) ;
	if (writable) {
		SWEAR(+server_mrkr) ;
		mode_t   mod    = 0666 & ~((get_umask()&0222)<<1)                    ;  // if we have access to server, we grant write access, so suppress read access for those not having write access
		::string tmp    = cat(server_mrkr,'.',mrkr.first,'.',mrkr.second)    ;
		AcFd     tmp_fd { tmp , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=mod} } ;
		tmp_fd.write(cat(
			server_fd.service_str(mrkr.first) , '\n'
		,	mrkr.second                       , '\n'
		)) ; //!  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		bool ok = ::link(tmp.c_str(),server_mrkr.c_str())==0 ;
		//        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		unlnk(tmp) ;
		if (!ok) { trace("no_unlnk") ; throw ::pair_s<Rc>( cat(server_mrkr," : ",StrErr()) , Rc::BadServer ) ; }
		_g_server_mrkr = server_mrkr ;
		//vvvvvvvvvvvvvvvvvvvvvvv
		::atexit(_server_cleanup) ;
		//^^^^^^^^^^^^^^^^^^^^^^^
		// if server marker is touched by user, we do as we received a ^C
		// ideally, we should watch server_mrkr before it is created to be sure to miss nothing, but inotify requires an existing file
		if ( +(watch_fd=::inotify_init1(O_CLOEXEC)) )
			if ( ::inotify_add_watch( watch_fd , server_mrkr.c_str() , IN_DELETE_SELF|IN_MOVE_SELF|IN_MODIFY )<0 )
				watch_fd.close() ;                                                                                 // useless if we cannot watch
	}
	trace("done",STR(rescue)) ;
}

::pair<ClientSockFd,pid_t> connect_to_server( bool try_old , uint64_t magic , ::vector_s&& cmd_line , ::string const& server_mrkr , ::string const& dir_s , Channel chnl ) {
	Trace trace(chnl,"connect_to_server",magic,cmd_line) ;
	::string file_service_str ;
	Bool3    server_is_local  = Maybe                                                          ;
	pid_t    server_pid       = 0                                                              ;
	Pdate    now              = New                                                            ;
	Child    server           { .as_session=true , .cmd_line=::move(cmd_line) , .cwd_s=dir_s } ;
	//
	auto mk_client = [&](KeyedService service) {
		ClientSockFd res       { service , false/*reuse_addr*/ , Delay(3)/*timeout*/ } ; res.set_receive_timeout(Delay(10)) ;                                // if server is too long to answer, ...
		::string     magic_str = res.read(sizeof(magic))                               ; throw_unless( magic_str.size()==sizeof(magic) , "bad_answer_sz" ) ; // ... it is probably not working properly
		uint64_t     magic_    = decode_int<uint64_t>(&magic_str[0])                   ; throw_unless( magic_          ==magic         , "bad_answer"    ) ;
		res.set_receive_timeout() ;                                                                                                                          // restore
		return res ;
	} ;
	//
	for ( int i : iota(10) ) {
		trace("try_old",i) ;
		if (try_old) {                                                                             // try to connect to an existing server if we have a magic key to identify it
			AcFd       server_mrkr_fd { dir_s+server_mrkr , {.err_ok=true} } ; if (!server_mrkr_fd) { trace("no_marker"  ) ; goto LaunchServer ; }
			::vector_s lines          = server_mrkr_fd.read_lines()          ; if (lines.size()!=2) { trace("bad_markers") ; goto LaunchServer ; }
			//
			file_service_str = ::move            (lines[0]) ;
			server_pid       = from_string<pid_t>(lines[1]) ;
			server_is_local  = No                           ;
			try {
				KeyedService service { file_service_str , true/*name_ok*/ } ;
				server_is_local |= fqdn()==SockFd::s_host(file_service_str) ;
				if (server_is_local==Yes) service.addr = 0 ;                                       // dont use network if not necessary
				//
				try                     { return { mk_client(service) , server_pid } ; }
				catch (::string const&) { goto LaunchServer                          ; }
			} catch(::string const&) { trace("cannot_connect",file_service_str) ; }
			trace("server",file_service_str,server_pid) ;
		}
	LaunchServer :
		// try to launch a new server
		// server calls ::setpgid(0/*pid*/,0/*pgid*/) to create a new group by itself, after initialization, so during init, a ^C will propagate to server
		trace("try_new",i) ;
		//
		server.stdin  = Child::PipeFd ;
		server.stdout = Child::PipeFd ;
		server.spawn() ;
		//
		try {
			auto                       service = deserialize<KeyedService>(server.stdout.read()) ;
			::pair<ClientSockFd,pid_t> res     { mk_client(service) , server.pid }               ;
			server.stdin.close() ;                                                                 // now that we have connected to server, we can release its stdin
			server.mk_daemon() ;                                                                   // let process survive to server dxtor
			return res ;
		} catch (::string const&) {
			int wstatus = server.wait() ;                                                          // dont care about return code, we are going to relauch/reconnect anyway
			if (!wstatus_ok(wstatus)) break ;
			// retry if not successful, may be a race between several clients trying to connect to/launch servers
			now += Delay(0.1) ;
			now.sleep_until() ;
		}
	}
	::string msg = "cannot connect to nor launch "+base_name(server.cmd_line[0]) ;
	if (server_is_local!=Maybe) {
		msg << ", consider :\n" ;
		if ( server_pid && (server_is_local==No||sense_process(server_pid)) ) {
			/**/                     msg << '\t'                                         ;
			if (server_is_local==No) msg << "ssh "<<SockFd::s_host(file_service_str)+' ' ;
			/**/                     msg << "kill "<<server_pid                          ;
			/**/                     msg << '\n'                                         ;
		}
		msg << "\trm "<<dir_s<<server_mrkr ;
	}
	msg << '\n' ;
	trace("bad",file_service_str,msg) ;
	throw ::pair_s<Rc>( msg , Rc::BadServer) ;
}
