// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "thread.hh"

#include "ptrace.hh"

#include "gather.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

//
// Gather::AccessInfo
//

::ostream& operator<<( ::ostream& os , Gather::AccessInfo const& ai ) {
	bool  i  = false/*garbage*/ ;
	Pdate rd = Pdate::Future    ;
	for( Access a : All<Access> ) if ( rd>ai.read[+a] ) { rd = ai.read[+a] ; i = !ai.digest.accesses[a] ; }
	/**/                                            os << "AccessInfo("                                         ;
	if ( rd!=Pdate::Future                        ) os << "R:" <<rd<<(i?"~":"")                           <<',' ;
	if ( ai.digest.extra_tflags[ExtraTflag::Allow]) os << "T:" <<ai.target                                <<',' ;
	if ( ai.digest.write!=No                      ) os << "W:"<<ai.write<<(ai.digest.write==Maybe?"?":"") <<',' ;
	if (+ai.dep_info                              ) os << ai.dep_info                                     <<',' ;
	/**/                                            os << ai.digest                                             ;
	if ( ai.seen!=Pdate::Future                   ) os <<",seen"                                                ;
	return                                          os <<')'                                                    ;
}

void Gather::AccessInfo::update( PD pd , AccessDigest ad , DI const& di ) {
	digest.tflags       |= ad.tflags       ;
	digest.extra_tflags |= ad.extra_tflags ;
	digest.dflags       |= ad.dflags       ;
	digest.extra_dflags |= ad.extra_dflags ;
	//
	bool tfi =        ad.extra_tflags[ExtraTflag::Ignore] ;
	bool dfi = tfi || ad.extra_dflags[ExtraDflag::Ignore] ; // tfi also prevents reads from being visible
	//
	if (!dfi) {
		for( Access a : All<Access> ) if (read[+a]<=pd) goto NotFirst ;
		dep_info = di ;
	NotFirst : ;
	}
	//
	for( Access a : All<Access> ) { if (!dfi) {                            if ( PD& d=read[+a] ; ad.accesses[a]                     && pd<d ) { digest.accesses |= a ; d = pd ; } } }
	/**/                          { if (!dfi) {                            if ( PD& d=seen     ; di.seen(ad.accesses)               && pd<d )                          d = pd ;   } }
	/**/                          { if (!tfi) { digest.write |= ad.write ; if ( PD& d=write    ; ad.write==Yes                      && pd<d )                          d = pd ;   } }
	/**/                          { if (!tfi) {                            if ( PD& d=target   ; ad.extra_tflags[ExtraTflag::Allow] && pd<d )                          d = pd ;   } }
	//
}

//
// Gather
//

::ostream& operator<<( ::ostream& os , Gather const& gd ) {
	/**/             os << "Gather(" << gd.accesses ;
	if (gd.seen_tmp) os <<",seen_tmp"               ;
	return           os << ')'                      ;
}

void Gather::_new_access( Fd fd , PD pd , ::string&& file , AccessDigest ad , DI const& di , ::string const& comment ) {
	SWEAR( +file , comment        ) ;
	SWEAR( +pd   , comment , file ) ;
	AccessInfo* info        = nullptr/*garbage*/                       ;
	auto        [it,is_new] = access_map.emplace(file,accesses.size()) ;
	if (is_new) {
		accesses.emplace_back(::move(file),AccessInfo()) ;
		info = &accesses.back().second ;
	} else {
		info = &accesses[it->second].second ;
	}
	AccessInfo old_info = *info ;                                                                                                                            // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update( pd , ad , di ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || *info!=old_info ) Trace("_new_access", fd , STR(is_new) , pd , ad , di , _parallel_id , comment , old_info , "->" , *info , it->first ) ; // only trace if something changes
}

void Gather::new_deps( PD pd , ::vmap_s<DepDigest>&& deps , ::string const& stdin ) {
	for( auto& [f,dd] : deps ) {
		bool is_stdin = f==stdin ;
		if (is_stdin) {                             // stdin is read
			if (!dd.accesses) dd.sig(FileInfo(f)) ; // record now if not previously accessed
			dd.accesses |= Access::Reg ;
		}
		_new_access( pd , ::move(f) , {.accesses=dd.accesses,.dflags=dd.dflags} , dd , is_stdin?"stdin"s:"s_deps"s ) ;
	}
}

void Gather::new_exec( PD pd , ::string const& exe , ::string const& c ) {
	RealPath              rp       { autodep_env }                    ;
	RealPath::SolveReport sr       = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(sr) ) {
		_new_access( pd , ::move(f) , {.accesses=a} , FileInfo(f) , c ) ;
	}
}

void Gather::_send_to_server( Fd fd , Jerr&& jerr ) {
	Trace trace("_send_to_server",fd,jerr) ;
	//
	Proc   proc = jerr.proc         ;                                    // capture essential info before moving to server_cb
	size_t sz   = jerr.files.size() ;                                    // .
	switch (proc) {
		case Proc::ChkDeps    : reorder(false/*at_end*/)       ; break ; // ensure server sees a coherent view
		case Proc::DepVerbose : _new_accesses(fd,::copy(jerr)) ; break ;
		//
		case Proc::Decode : SWEAR( jerr.sync && jerr.files.size()==1 , jerr ) ; _codec_files[fd] = Codec::mk_decode_node( jerr.files[0].first , jerr.ctx , jerr.txt ) ; break ;
		case Proc::Encode : SWEAR( jerr.sync && jerr.files.size()==1 , jerr ) ; _codec_files[fd] = Codec::mk_encode_node( jerr.files[0].first , jerr.ctx , jerr.txt ) ; break ;
		default : ;
	}
	if (!jerr.sync) fd = {} ;                                            // dont reply if not sync
	JobMngtRpcReq jmrr ;
	switch (jerr.proc) {
		case JobExecProc::ChkDeps : jmrr = { JobMngtProc::ChkDeps , seq_id , job , fd , cur_deps_cb()                                                                    } ; break ;
		case JobExecProc::Decode  : jmrr = { JobMngtProc::Decode  , seq_id , job , fd , ::move(jerr.txt) , ::move(jerr.files[0].first) , ::move(jerr.ctx)                } ; break ;
		case JobExecProc::Encode  : jmrr = { JobMngtProc::Encode  , seq_id , job , fd , ::move(jerr.txt) , ::move(jerr.files[0].first) , ::move(jerr.ctx) , jerr.min_len } ; break ;
		case JobExecProc::DepVerbose : {
			JobMngtRpcReq::MDD deps ; deps.reserve(jerr.files.size()) ;
			for( auto&& [dep,date] : jerr.files ) deps.emplace_back( ::move(dep) , DepDigest(jerr.digest.accesses,date,{}/*dflags*/,true/*parallel*/) ) ; // no need for flags to ask info
			jmrr = { JobMngtProc::DepVerbose , seq_id , job , fd , ::move(deps) } ;
		} break ;
	DF}
	for( int i=3 ; i>=1 ; i-- ) {                                                     // retry if server exists and cannot be reached
		bool sent = false ;
		try {
			ClientSockFd csfd { service_mngt } ;                                      // ensure csfd is closed only after sent = true
			//vvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send( csfd , jmrr ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^
			sent = true ;
		} catch (::string const& e) {
			if (i>1) continue ;                                                       // retry
			if (sent) {
				trace("server_not_available",i,e) ;
				throw ;                                                               // server exists but could not be reached (error when closing socket)
			} else {
				trace("no_server") ;
				JobExecRpcReply sync_reply ;
				sync_reply.proc      = proc                                         ;
				sync_reply.ok        = Yes                                          ; // try to mimic server as much as possible when none is available
				sync_reply.dep_infos = ::vector<pair<Bool3/*ok*/,Crc>>(sz,{Yes,{}}) ; // .
				sync(fd,::move(sync_reply)) ;
			}
		}
		break ;
	}
}

[[noreturn]] void Gather::_do_child() {
	AutodepPtrace::s_autodep_env = new AutodepEnv{autodep_env} ;
	Child grand_child { .cmd_line=cmd_line , .env=env , .add_env=&_add_env , .cwd_=cwd , .pre_exec=AutodepPtrace::s_prepare_child } ; // first level child has created the group
	//    vvvvvvvvvvvvvvvvvvv
	try { grand_child.spawn() ; }
	//    ^^^^^^^^^^^^^^^^^^^
	catch(::string const& e) { exit(Rc::System,e) ; }
	Trace trace("grand_child_pid",grand_child.pid) ;
	AutodepPtrace autodep_ptrace { grand_child.pid }        ;
	int           wstatus        = autodep_ptrace.process() ;
	grand_child.waited() ;                                                                                                            // grand_child has already been waited
	if      (WIFEXITED  (wstatus)) ::_exit(WEXITSTATUS(wstatus)) ;
	else if (WIFSIGNALED(wstatus)) ::_exit(+Rc::System         ) ;
	fail_prod("ptraced child did not exit and was not signaled : wstatus : ",wstatus) ;
}

void Gather::_spawn_child() {
	Trace trace("_spawn_child",cmd_line,child_stdin,child_stdout,child_stderr) ;
	//
	_add_env          = { {"LMAKE_AUTODEP_ENV",autodep_env} } ; // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	_child.as_session = as_session                            ;
	_child.stdin_fd   = child_stdin                           ;
	_child.stdout_fd  = child_stdout                          ;
	_child.stderr_fd  = child_stderr                          ;
	_child.first_pid  = first_pid                             ;
	if (method==AutodepMethod::Ptrace) {                        // PER_AUTODEP_METHOD : handle case
		// we split the responsability into 2 processes :
		// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
		// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
		_child.pre_exec     = _s_do_child ;
		_child.pre_exec_arg = this        ;
		_child.spawn() ;
		start_date = New ;                                      // record job start time as late as possible
	} else {
		if (method>=AutodepMethod::Ld) {                                                                                                                    // PER_AUTODEP_METHOD : handle case
			::string env_var ;
			switch (method) {                                                                                                                               // PER_AUTODEP_METHOD : handle case
				case AutodepMethod::LdAudit           : env_var = "LD_AUDIT"   ; _add_env[env_var] = *g_lmake_dir_s+"_lib/ld_audit.so"            ; break ;
				case AutodepMethod::LdPreload         : env_var = "LD_PRELOAD" ; _add_env[env_var] = *g_lmake_dir_s+"_lib/ld_preload.so"          ; break ;
				case AutodepMethod::LdPreloadJemalloc : env_var = "LD_PRELOAD" ; _add_env[env_var] = *g_lmake_dir_s+"_lib/ld_preload_jemalloc.so" ; break ;
			DF}
			if (env) { if (env->contains(env_var)) _add_env[env_var] += ':' + env->at(env_var) ; }
			else     { if (has_env      (env_var)) _add_env[env_var] += ':' + get_env(env_var) ; }
		}
		new_exec( New , cmd_line[0] ) ;
		start_date      = New       ;                                                                                                                       // record job start time as late as possible
		_child.cmd_line = cmd_line  ;
		_child.env      = env       ;
		_child.add_env  = &_add_env ;
		_child.cwd_     = cwd       ;
		//vvvvvvvvvvvv
		_child.spawn() ;
		//^^^^^^^^^^^^
	}
	if (+timeout) { _end_timeout = start_date + timeout ; trace("set_timeout",timeout,_end_timeout) ; }
	trace("child_pid",_child.pid) ;
}
Status Gather::exec_child() {
	Trace trace("exec_child",STR(as_session),method,autodep_env,cmd_line) ;
	if (env) trace("env",*env) ;
	ServerSockFd job_master_fd { New } ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service = job_master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	AutoCloseFd                           child_fd           ;
	::jthread                             wait_jt            ;                             // thread dedicated to wating child
	Epoll                                 epoll              { New }       ;
	Status                                status             = Status::New ;
	::umap<Fd,Jerr>                       delayed_check_deps ;                             // check_deps events are delayed to ensure all previous deps are received
	size_t                                live_out_pos       = 0           ;
	::umap<Fd,pair<IMsgBuf,vector<Jerr>>> slaves             ;                             // Jerr's are waiting for confirmation
	//
	auto set_status = [&]( Status status_ , ::string const& msg_={} )->void {
		if ( status==Status::New || status==Status::Ok ) status = status_ ;                // else there is already another reason
		if ( +msg_                                     ) append_line_to_string(msg,msg_) ;
	} ;
	auto kill = [&](bool next_step=false)->void {
		trace("kill",STR(next_step),_kill_step,STR(as_session),_child.pid,_wait) ;
		if      (next_step             ) SWEAR(_kill_step<=kill_sigs.size()) ;
		else if (_kill_step            ) return ;
		if      (!_wait[Kind::ChildEnd]) return ;
		int sig = _kill_step==kill_sigs.size() ? SIGKILL : kill_sigs[_kill_step] ;
		trace("kill_sig",sig) ;
		//                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if ( sig && _child.pid>1 ) kill_process(_child.pid,sig,as_session/*as_group*/) ;
		//                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		set_status(Status::Killed) ;
		if      (_kill_step==kill_sigs.size()) _end_kill = Pdate::Future       ;
		else if (!_end_kill                  ) _end_kill = Pdate(New)+Delay(1) ;
		else                                   _end_kill = _end_kill +Delay(1) ;
		_kill_step++ ;
		trace("kill_done",_end_kill) ;
	} ;
	//
	if (+server_master_fd) {
		epoll.add_read(server_master_fd,Kind::ServerMaster) ;
		trace("read_server_master",server_master_fd,"wait",_wait,epoll.cnt) ;
	}
	_wait = Kind::ChildStart ;
	trace("start","wait",_wait,epoll.cnt) ;
	while ( epoll.cnt || +_wait ) {
		Pdate now = New ;
		if (now>_end_child) {
			if      ( !_wait[Kind::ChildEnd]                     ) set_status(Status::Err,to_string("still active after having been dead for ",network_delay.short_str()                   )) ;
			else if ( _kill_step && _kill_step< kill_sigs.size() ) set_status(Status::Err,to_string("still alive after having been killed ",_kill_step      ," times"                      )) ;
			else if (               _kill_step==kill_sigs.size() ) set_status(Status::Err,to_string("still alive after having been killed ",kill_sigs.size()," times followed by a SIGKILL")) ;
			else if ( _timeout_fired                             ) set_status(Status::Err,to_string("still alive after having timed out and been killed with SIGKILL"                      )) ;
			else                                                   FAIL("dont know why still active") ;
			break ;
		}
		if (now>_end_kill) {
			kill() ;
		}
		if ( now>_end_timeout && !_timeout_fired ) {
			set_status(Status::Err,to_string("timeout after ",timeout.short_str())) ;
			kill() ;
			_timeout_fired = true          ;
			_end_timeout   = Pdate::Future ;
		}
		Delay wait_for ;
		if ( !delayed_check_deps && !_wait[Kind::ChildStart] ) {
			Pdate event_date = ::min( _end_child , ::min( _end_kill , _end_timeout ) ) ;
			wait_for = event_date<Pdate::Future ? event_date-now : Delay::Forever ;
		}
		::vector<Epoll::Event> events = epoll.wait(wait_for) ;
		if (!events) {
			if (+delayed_check_deps) {                          // process delayed check deps after all other events
				trace("delayed_chk_deps") ;
				for( auto& [fd,jerr] : delayed_check_deps ) _send_to_server(fd,::move(jerr)) ;
				delayed_check_deps.clear() ;
				continue ;
			}
			if (_wait[Kind::ChildStart]) {                      // handle case where we are killed before starting : create child when we have processed waiting connections from server
				try {
					_spawn_child() ;
					_wait &= ~Kind::ChildStart ;
					trace("started","wait",_wait,epoll.cnt) ;
				} catch(::string const& e) {
					if (child_stderr==Child::Pipe) stderr = ensure_nl(e) ;
					else                           child_stderr.write(ensure_nl(e)) ;
					status = Status::EarlyErr ;
					goto Return ;
				}
				SWEAR(is_blocked_sig(SIGCHLD)) ;
				child_fd = open_sigs_fd({SIGCHLD}) ;
				if (child_stdout==Child::Pipe) { epoll.add_read(_child.stdout ,Kind::Stdout   ) ; _wait |= Kind::Stdout   ; trace("read_stdout"    ,_child.stdout,"wait",_wait,epoll.cnt) ; }
				if (child_stderr==Child::Pipe) { epoll.add_read(_child.stderr ,Kind::Stderr   ) ; _wait |= Kind::Stderr   ; trace("read_stderr"    ,_child.stderr,"wait",_wait,epoll.cnt) ; }
				/**/                             epoll.add_read(child_fd      ,Kind::ChildEnd ) ; _wait |= Kind::ChildEnd ; trace("read_child "    ,child_fd     ,"wait",_wait,epoll.cnt) ;
				/**/                             epoll.add_read(job_master_fd ,Kind::JobMaster) ;                           trace("read_job_master",job_master_fd,"wait",_wait,epoll.cnt) ;
			}
		}
		for( Epoll::Event const& event : events ) {
			Kind kind = event.data<Kind>() ;
			Fd   fd   = event.fd()         ;
			if (kind!=Kind::JobSlave) trace(kind,fd) ;
			switch (kind) {
				case Kind::Stdout :
				case Kind::Stderr : {
					char          buf[4096] ;
					int           cnt       = ::read( fd , buf , sizeof(buf) ) ; SWEAR( cnt>=0 , cnt ) ;
					::string_view buf_view  { buf , size_t(cnt) }                                      ;
					if (cnt) {
						if (kind==Kind::Stderr) {
							stderr.append(buf_view) ;
						} else {
							size_t old_sz = stdout.size() ;
							stdout.append(buf_view) ;
							if (live_out) {
								if ( size_t pos = buf_view.rfind('\n') ;  pos!=Npos ) {
									pos++ ;
									size_t len = old_sz + pos - live_out_pos ;
									trace("live_out",live_out_pos,len) ;
									//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
									OMsgBuf().send( ClientSockFd(service_mngt) , JobMngtRpcReq( JobMngtProc::LiveOut , seq_id , job , stdout.substr(live_out_pos,len) ) ) ;
									//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
									live_out_pos = old_sz+pos ;
								}
							}
						}
					} else {
						epoll.del(fd) ;
						_wait &= ~kind ;
						trace("close",kind,fd,"wait",_wait,epoll.cnt) ;
					}
				} break ;
				case Kind::ChildEnd : {
					struct signalfd_siginfo si  ;
					int                     cnt = ::read( fd , &si , sizeof(si) ) ; SWEAR( cnt==sizeof(si) , cnt ) ;
					::waitpid(si.ssi_pid,&wstatus,0) ;
					if (!WIFSTOPPED(wstatus)) {
						end_date = New ;
						_end_child = end_date + network_delay ; // wait at most network_delay for reporting & stdout & stderr to settle down
						if      (WIFEXITED  (wstatus)) set_status(             WEXITSTATUS(wstatus)!=0 ? Status::Err : Status::Ok       ) ;
						else if (WIFSIGNALED(wstatus)) set_status( is_sig_sync(WTERMSIG   (wstatus))   ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
						else                           fail("unexpected wstatus : ",wstatus) ;
						epoll.del(fd) ;
						_wait &= ~Kind::ChildEnd ;
						/**/                   epoll.cnt-- ;                                    // dont wait for new connections from job (but process those that come)
						if (+server_master_fd) epoll.cnt-- ;                                    // idem for connections from server
						trace("close",kind,status,::hex,wstatus,::dec,"wait",_wait,epoll.cnt) ;
					}
				} break ;
				case Kind::JobMaster    :
				case Kind::ServerMaster : {
					bool is_job = kind==Kind::JobMaster ;
					Fd   slave  ;
					if (is_job) { SWEAR( fd==job_master_fd    , fd , job_master_fd    ) ; slave = job_master_fd   .accept().detach() ; epoll.add_read(slave,Kind::JobSlave   ) ; }
					else        { SWEAR( fd==server_master_fd , fd , server_master_fd ) ; slave = server_master_fd.accept().detach() ; epoll.add_read(slave,Kind::ServerSlave) ; }
					trace("read_slave",STR(is_job),slave,"wait",_wait,epoll.cnt) ;
					slaves[slave] ;                                                             // allocate entry
				} break ;
				case Kind::ServerSlave : {
					JobMngtRpcReply jmrr ;
					auto it           = slaves.find(fd) ;
					auto& slave_entry = it->second      ;
					try         { if (!slave_entry.first.receive_step(fd,jmrr)) continue ; }
					catch (...) { trace("no_jmrr",jmrr) ; jmrr.proc = {} ;                 }                                                 // fd was closed, ensure no partially received jmrr
					trace(kind,jmrr) ;
					Fd rfd = jmrr.fd ;                                                                                                       // capture before move
					if (jmrr.seq_id==seq_id) {
						switch (jmrr.proc) {
							case JobMngtProc::DepVerbose :
							case JobMngtProc::Heartbeat  :                                                                           break ;
							case JobMngtProc::Kill       :
							case JobMngtProc::None       :                       set_status(Status::Killed ) ; kill() ;              break ; // server died
							case JobMngtProc::ChkDeps    : if (jmrr.ok==Maybe) { set_status(Status::ChkDeps) ; kill() ; rfd = {} ; } break ;
							case JobMngtProc::Decode :
							case JobMngtProc::Encode : {
								SWEAR(+jmrr.fd) ;
								auto it = _codec_files.find(jmrr.fd) ;
								_new_access( rfd , PD(New) , ::move(it->second) , {.accesses=Access::Reg} , jmrr.crc , ::string(snake(jmrr.proc)) ) ;
								_codec_files.erase(it) ;
							} break ;
						DF}
						if (+rfd) {
							JobExecRpcReply jerr ;
							switch (jmrr.proc) {
								case JobMngtProc::None       :                                                                                          break ;
								case JobMngtProc::ChkDeps    : SWEAR(jmrr.ok!=Maybe) ; jerr = { Proc::ChkDeps    , jmrr.ok                          } ; break ;
								case JobMngtProc::DepVerbose :                         jerr = { Proc::DepVerbose ,           ::move(jmrr.dep_infos) } ; break ;
								case JobMngtProc::Decode     :                         jerr = { Proc::Decode     , jmrr.ok , ::move(jmrr.txt      ) } ; break ;
								case JobMngtProc::Encode     :                         jerr = { Proc::Encode     , jmrr.ok , ::move(jmrr.txt      ) } ; break ;
							DF}
							//vvvvvvvvvvvvvvvvvvvvvvvv
							sync( rfd , ::move(jerr) ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^
						}
					}
					epoll.close(fd) ;
					trace("close",kind,fd,"wait",_wait,epoll.cnt) ;
				} break ;
				case Kind::JobSlave : {
					Jerr jerr         ;
					auto it           = slaves.find(fd) ;
					auto& slave_entry = it->second      ;
					try         { if (!slave_entry.first.receive_step(fd,jerr)) continue ; }
					catch (...) { trace("no_jerr",kind,fd,jerr) ; jerr.proc = Proc::None ; }                  // fd was closed, ensure no partially received jerr
					Proc proc  = jerr.proc ;                                                                  // capture essential info so as to be able to move jerr
					bool sync_ = jerr.sync ;                                                                  // .
					if (proc!=Proc::Access) trace(kind,fd,proc) ;                                             // there may be too many Access'es, only trace within _new_accesses
					switch (proc) {
						case Proc::Confirm :
							for( Jerr& j : slave_entry.second ) { j.digest.write = jerr.digest.write ; _new_accesses(fd,::move(j)) ; }
							slave_entry.second.clear() ;
						break ;
						case Proc::None :
							epoll.close(fd) ;
							trace("close",kind,fd,"wait",_wait,epoll.cnt) ;
							for( Jerr& j : slave_entry.second ) _new_accesses(fd,::move(j)) ;                 // process deferred entries although with uncertain outcome
							slaves.erase(it) ;
						break ;
						case Proc::Access   :
							// for read accesses, trying is enough to trigger a dep, so confirm is useless
							if ( jerr.digest.write==Maybe ) slave_entry.second.push_back(::move(jerr)) ;      // defer until confirm resolution
							else                            _new_accesses(fd,::move(jerr))             ;
						break ;
						case Proc::Tmp        : seen_tmp = true ;                           break           ;
						case Proc::Guard      : _new_guards(fd,::move(jerr)) ;              break           ;
						case Proc::DepVerbose :
						case Proc::Decode     :
						case Proc::Encode     : _send_to_server(fd,::move(jerr)) ;          goto NoReply    ;
						case Proc::ChkDeps    : delayed_check_deps[fd] = ::move(jerr) ;     goto NoReply    ; // if sync, reply is delayed as well
						case Proc::Panic      : set_status(Status::Err,jerr.txt) ; kill() ; [[fallthrough]] ;
						case Proc::Trace      : trace(jerr.txt) ;                           break           ;
					DF}
					if (sync_) sync( fd , JobExecRpcReply(proc) ) ;
				NoReply : ;
				} break ;
			DF}
		}
	}
Return :
	_child.waited() ;
	trace("done",status) ;
	SWEAR(status!=Status::New) ;
	reorder(true/*at_end*/) ;                                                                                 // ensure server sees a coherent view
	return status ;
}

// reorder accesses in chronological order and suppress implied dependencies :
// - when a file is depended upon, its uphill directories are implicitely depended upon, no need to keep them and this significantly decreases the number of deps
// - suppress dir when one of its sub-files appear before
// - suppress dir when one of its sub-files appear immediately after
void Gather::reorder(bool at_end) {
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	Trace trace("reorder") ;
	::stable_sort(                                                   // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b ) -> bool {
			return a.second.first_read().first < b.second.first_read().first ;
		}
	) ;
	// first pass (backward) : note dirs of immediately following files
	::string const* last = nullptr ;
	for( auto it=accesses.rbegin() ; it!=accesses.rend() ; it++ ) {  // XXX : manage parallel deps
		::string const& file   = it->first         ;
		::AccessDigest& digest = it->second.digest ;
		if (
			last
		&&	( digest.write==No        && !digest.dflags            )
		&&	( last->starts_with(file) && (*last)[file.size()]=='/' )
		)    digest.accesses = {}    ;                               // keep original last which is better
		else last            = &file ;
	}
	// second pass : suppress dirs of seen files and previously noted dirs
	::uset_s dirs  ;
	size_t   i_dst = 0     ;
	bool     cpy   = false ;
	for( auto& access : accesses ) {
		::string const& file   = access.first         ;
		::AccessDigest& digest = access.second.digest ;
		if ( digest.write==No && !digest.dflags && !digest.tflags ) {
			if (!digest.accesses   ) { trace("skip_from_next",file) ; { if (!at_end) access_map.erase(file) ; } cpy = true ; continue ; }
			if (dirs.contains(file)) { trace("skip_from_prev",file) ; { if (!at_end) access_map.erase(file) ; } cpy = true ; continue ; }
		}
		for( ::string dir_s=dir_name_s(file) ; +dir_s ; dir_s=dir_name_s(dir_s) ) if (!dirs.insert(no_slash(dir_s)).second) break ; // all uphill dirs are already inserted if a dir has been inserted
		if (cpy) accesses[i_dst] = ::move(access) ;
		i_dst++ ;
	}
	accesses.resize(i_dst) ;
	for( NodeIdx i=0 ; i<accesses.size() ; i++ ) access_map.at(accesses[i].first) = i ;                                             // always recompute access_map as accesses has been sorted
}
