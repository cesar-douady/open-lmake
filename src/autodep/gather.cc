// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "thread.hh"

#include "ptrace.hh"
#include "record.hh"

#include "gather.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

//
// Gather::AccessInfo
//

::string& operator+=( ::string& os , Gather::AccessInfo const& ai ) {
	bool  i  = false/*garbage*/ ;
	Pdate rd = Pdate::Future    ;
	for( Access a : iota(All<Access>) ) if ( rd>ai.read[+a] ) { rd = ai.read[+a] ; i = !ai.digest.accesses[a] ; }
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
	// if ignore, record empty info (which will mask further info)
	/**/                                if (required<=pd) goto NotFirst ;
	for( Access a : iota(All<Access>) ) if (read[+a]<=pd) goto NotFirst ;
	dep_info = dfi ? DI() : di ;
NotFirst :
	/**/                                if ( PD& d=seen     ; pd<d && (dfi||di.seen(ad.accesses)       ) ) { d = pd ; digest_seen     = !dfi         ; }
	/**/                                if ( PD& d=required ; pd<d && (dfi||ad.dflags[Dflag::Required] ) ) { d = pd ; digest_required = !dfi         ; }
	for( Access a : iota(All<Access>) ) if ( PD& d=read[+a] ; pd<d && (dfi||ad.accesses[a]             ) ) { d = pd ; digest.accesses.set(a,!dfi)    ; }
	/**/                                if ( PD& d=write    ; pd<d && (tfi||ad.write!=No               ) ) { d = pd ; digest.write = (!tfi)&ad.write ; }
	/**/                                if ( PD& d=target   ; pd<d && ad.extra_tflags[ExtraTflag::Allow] )   d = pd ;
	//
}

//
// Gather
//

::string& operator+=( ::string& os , Gather const& gd ) {
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
	AccessInfo old_info = *info ;                                                                                               // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update( pd , ad , di ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || *info!=old_info ) {
		if (+comment) {
			if (is_new) _exec_trace(pd,::copy(comment),accesses.back().first) ;                                                 // file has been ::move()'ed
			else        _exec_trace(pd,::copy(comment),file                 ) ;
		}
		Trace("_new_access", fd , STR(is_new) , pd , ad , di , _parallel_id , comment , old_info , "->" , *info , it->first ) ; // only trace if something changes
	}
}

void Gather::new_deps( PD pd , ::vmap_s<DepDigest>&& deps , ::string const& stdin ) {
	for( auto& [f,dd] : deps ) {
		bool is_stdin = f==stdin ;
		if (is_stdin) {                             // stdin is read
			if (!dd.accesses) dd.sig(FileInfo(f)) ; // record now if not previously accessed
			dd.accesses |= Access::Reg ;
		}
		_new_access( pd , ::move(f) , {.accesses=dd.accesses,.dflags=dd.dflags} , dd , is_stdin?"stdin":"s_deps" ) ;
	}
}

void Gather::new_exec( PD pd , ::string const& exe , ::string const& c ) {
	RealPath              rp       { autodep_env }                    ;
	RealPath::SolveReport sr       = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(sr) )
		if (!Record::s_is_simple(f.c_str())) _new_access( pd , ::move(f) , {.accesses=a} , FileInfo(f) , c ) ;
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
		case Proc::Decode : SWEAR( jerr.sync , jerr ) ; _codec_files[fd] = Codec::mk_decode_node( jerr.codec_file() , jerr.ctx , jerr.code() ) ; break ;
		case Proc::Encode : SWEAR( jerr.sync , jerr ) ; _codec_files[fd] = Codec::mk_encode_node( jerr.codec_file() , jerr.ctx , jerr.val () ) ; break ;
	DN}
	if (!jerr.sync) fd = {} ;                                            // dont reply if not sync
	JobMngtRpcReq jmrr ;
	switch (proc) {
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

void Gather::_ptrace_child( Fd report_fd , ::latch* ready ) {
	t_thread_key = 'T' ;
	AutodepPtrace::s_init(autodep_env) ;
	_child.pre_exec = AutodepPtrace::s_prepare_child  ;
	//vvvvvvvvvvvv
	_child.spawn() ;                                                        // /!\ although not mentioned in man ptrace, child must be launched by the tracing thread
	//^^^^^^^^^^^^
	ready->count_down() ;                                                   // signal main thread that _child.pid is available
	AutodepPtrace autodep_ptrace{_child.pid} ;
	wstatus = autodep_ptrace.process() ;
	ssize_t cnt = ::write(report_fd,&::ref(char()),1) ; SWEAR(cnt==1,cnt) ; // report child end
	Record::s_close_report() ;
}

Fd Gather::_spawn_child() {
	SWEAR(+cmd_line) ;
	Trace trace("_spawn_child",child_stdin,child_stdout,child_stderr) ;
	//
	Fd child_fd ;
	Fd   report_fd ;
	bool is_ptrace = method==AutodepMethod::Ptrace ;
	//
	_add_env          = { {"LMAKE_AUTODEP_ENV",autodep_env} } ;                                  // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	_child.as_session = as_session                            ;
	_child.stdin_fd   = child_stdin                           ;
	_child.stdout_fd  = child_stdout                          ;
	_child.stderr_fd  = child_stderr                          ;
	_child.first_pid  = first_pid                             ;
	if (is_ptrace) {                                                                             // PER_AUTODEP_METHOD : handle case
		// we split the responsability into 2 threads :
		// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
		// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
		Pipe pipe{New,true/*no_std*/} ;
		child_fd  = pipe.read  ;
		report_fd = pipe.write ;
	} else {
		if (method>=AutodepMethod::Ld) {                                                         // PER_AUTODEP_METHOD : handle case
			::string env_var ;
			switch (method) {                                                                    // PER_AUTODEP_METHOD : handle case
				#if HAS_32
					#define DOLLAR_LIB "$LIB"                                                    // 32 bits is supported, use ld.so automatic detection feature
				#else
					#define DOLLAR_LIB "lib"                                                     // 32 bits is not supported, use standard name
				#endif
				#if HAS_LD_AUDIT
					case AutodepMethod::LdAudit           : env_var = "LD_AUDIT"   ; _add_env[env_var] = *g_lmake_root_s + "_d" DOLLAR_LIB "/ld_audit.so"            ; break ;
				#endif
					case AutodepMethod::LdPreload         : env_var = "LD_PRELOAD" ; _add_env[env_var] = *g_lmake_root_s + "_d" DOLLAR_LIB "/ld_preload.so"          ; break ;
					case AutodepMethod::LdPreloadJemalloc : env_var = "LD_PRELOAD" ; _add_env[env_var] = *g_lmake_root_s + "_d" DOLLAR_LIB "/ld_preload_jemalloc.so" ; break ;
				#undef DOLLAR_LIB
			DF}
			if (env) { if (env->contains(env_var)) _add_env[env_var] += ':' + env->at(env_var) ; }
			else     { if (has_env      (env_var)) _add_env[env_var] += ':' + get_env(env_var) ; }
		}
		new_exec( New , mk_glb(cmd_line[0],cwd_s) ) ;
	}
	start_date      = New       ;                                                                // record job start time as late as possible
	_child.cmd_line = cmd_line  ;
	_child.env      = env       ;
	_child.add_env  = &_add_env ;
	_child.cwd_s    = cwd_s     ;
	if (is_ptrace) {
		::latch ready{1} ;
		_ptrace_thread = ::jthread( _s_ptrace_child , this , report_fd , &ready ) ;              // /!\ _child must be spawned from tracing thread
		ready.wait() ;                                                                           // wait until _child.pid is available
	} else {
		//vvvvvvvvvvvv
		_child.spawn() ;
		//^^^^^^^^^^^^
	}
	if (+timeout) { _end_timeout = start_date + timeout ; trace("set_timeout",timeout,_end_timeout) ; }
	trace("child_pid",_child.pid) ;
	return child_fd ;                                                                            // child_fd is only used with ptrace
}
Status Gather::exec_child() {
	using Event = Epoll<Kind>::Event ;
	Trace trace("exec_child",STR(as_session),method,autodep_env,cmd_line) ;
	if (env) trace("env",*env) ;
	ServerSockFd job_master_fd { New } ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service = job_master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	AcFd                                                 child_fd           ;
	::jthread                                            wait_jt            ;                    // thread dedicated to wating child
	Epoll<Kind>                                          epoll              { New }       ;
	Status                                               status             = Status::New ;
	::umap<Fd,Jerr>                                      delayed_check_deps ;                    // check_deps events are delayed to ensure all previous deps are received
	size_t                                               live_out_pos       = 0           ;
	::umap<Fd,pair<IMsgBuf,::umap<uint64_t/*id*/,Jerr>>> slaves             ;                    // Jerr's are waiting for confirmation
	bool                                                 panic_seen         = false       ;
	//
	auto set_status = [&]( Status status_ , ::string const& msg_={} )->void {
		if ( status==Status::New || status==Status::Ok ) status = status_ ;                      // else there is already another reason
		if ( +msg_                                     ) msg << set_nl << msg_ ;
	} ;
	auto kill = [&](bool next_step=false)->void {
		trace("kill",STR(next_step),_kill_step,STR(as_session),_child.pid,_wait) ;
		if      (next_step             ) SWEAR(_kill_step<=kill_sigs.size()) ;
		else if (_kill_step            ) return ;
		if      (!_wait[Kind::ChildEnd]) return ;
		int   sig = _kill_step==kill_sigs.size() ? SIGKILL : kill_sigs[_kill_step] ;
		Pdate now { New }                                                          ;
		trace("kill_sig",sig) ;
		//                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if ( sig && _child.pid>1 ) kill_process(_child.pid,sig,as_session/*as_group*/) ;
		//                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		set_status(Status::Killed) ;
		if      (_kill_step==kill_sigs.size()) _end_kill = Pdate::Future       ;
		else if (!_end_kill                  ) _end_kill = now       +Delay(1) ;
		else                                   _end_kill = _end_kill +Delay(1) ;
		_exec_trace(now,"kill",cat(sig)) ;
		_kill_step++ ;
		trace("kill_done",_end_kill) ;
	} ;
	//
	if (+server_master_fd) {
		epoll.add_read(server_master_fd,Kind::ServerMaster) ;
		trace("read_server_master",server_master_fd,"wait",_wait,+epoll) ;
	}
	_wait = Kind::ChildStart ;
	trace("start","wait",_wait,+epoll) ;
	while ( +epoll || +_wait ) {
		Pdate now = New ;
		if (now>_end_child) {
			_exec_trace(now,"still_alive") ;
			if (!_wait[Kind::ChildEnd]) {
				SWEAR( _wait[Kind::Stdout] || _wait[Kind::Stderr] , _wait , now , _end_child ) ; // else we should already have exited
				::string msg ;
				if ( _wait[Kind::Stdout]                        ) msg += "stdout " ;
				if ( _wait[Kind::Stdout] && _wait[Kind::Stderr] ) msg += "and "    ;
				if (                        _wait[Kind::Stderr] ) msg += "stderr " ;
				msg << "still open after job having been dead for " << network_delay.short_str() ;
				set_status(Status::Err,msg) ;
			}
			else if ( _kill_step && _kill_step< kill_sigs.size() ) set_status(Status::Err,"still alive after having been killed "s+_kill_step      +" times"                      ) ;
			else if (               _kill_step==kill_sigs.size() ) set_status(Status::Err,"still alive after having been killed "s+kill_sigs.size()+" times followed by a SIGKILL") ;
			else if ( _timeout_fired                             ) set_status(Status::Err,"still alive after having timed out and been killed with SIGKILL"                       ) ;
			else                                                   FAIL("dont know why still active") ;
			break ;
		}
		if (now>_end_kill) {
			kill() ;
		}
		if ( now>_end_timeout && !_timeout_fired ) {
			_exec_trace(now,"timeout") ;
			set_status(Status::Err,"timeout after "+timeout.short_str()) ;
			kill() ;
			_timeout_fired = true          ;
			_end_timeout   = Pdate::Future ;
		}
		Delay wait_for ;
		if ( !delayed_check_deps && !_wait[Kind::ChildStart] ) {
			Pdate event_date = ::min( _end_child , ::min( _end_kill , _end_timeout ) ) ;
			wait_for = event_date<Pdate::Future ? event_date-now : Delay::Forever ;
		}
		::vector<Event> events = epoll.wait(wait_for) ;
		if (!events) {
			if (+delayed_check_deps) {      // process delayed check deps after all other events
				trace("delayed_chk_deps") ;
				for( auto& [fd,jerr] : delayed_check_deps ) _send_to_server(fd,::move(jerr)) ;
				delayed_check_deps.clear() ;
				continue ;
			}
			if (_wait[Kind::ChildStart]) {  // handle case where we are killed before starting : create child when we have processed waiting connections from server
				try {
					child_fd  = _spawn_child()    ;
					_wait    &= ~Kind::ChildStart ;
					_exec_trace(start_date,"start_job") ;
					trace("started","wait",_wait,+epoll) ;
				} catch(::string const& e) {
					if (child_stderr==Child::PipeFd) stderr = ensure_nl(e) ;
					else                             child_stderr.write(ensure_nl(e)) ;
					status = Status::EarlyErr ;
					goto Return ;
				}
				if (child_stdout==Child::PipeFd) { epoll.add_read( _child.stdout , Kind::Stdout     ) ; _wait |= Kind::Stdout   ; trace("read_stdout    ",_child.stdout,"wait",_wait,+epoll) ; }
				if (child_stderr==Child::PipeFd) { epoll.add_read( _child.stderr , Kind::Stderr     ) ; _wait |= Kind::Stderr   ; trace("read_stderr    ",_child.stderr,"wait",_wait,+epoll) ; }
				if (+child_fd                  ) { epoll.add_read( child_fd      , Kind::ChildEndFd ) ; _wait |= Kind::ChildEnd ; trace("read_child     ",child_fd     ,"wait",_wait,+epoll) ; }
				else                             { epoll.add_pid ( _child.pid    , Kind::ChildEnd   ) ; _wait |= Kind::ChildEnd ; trace("read_child_proc",              "wait",_wait,+epoll) ; }
				/**/                               epoll.add_read( job_master_fd , Kind::JobMaster  ) ;                           trace("read_job_master",job_master_fd,"wait",_wait,+epoll) ;
			}
		}
		for( Event const& event : events ) {
			Kind kind = event.data() ;
			Fd   fd   ;                if (kind!=Kind::ChildEnd) fd = event.fd() ;                                              // no fd available for ChildEnd
			switch (kind) {
				case Kind::Stdout :
				case Kind::Stderr : {
					char          buf[4096] ;
					int           cnt       = ::read( fd , buf , sizeof(buf) ) ; SWEAR( cnt>=0 , cnt ) ;
					::string_view buf_view  { buf , size_t(cnt) }                                      ;
					if (cnt) {
						trace(kind,fd,cnt) ;
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
						epoll.del(false/*write*/,fd) ;
						_wait &= ~kind ;
						trace("close",kind,fd,"wait",_wait,+epoll) ;
					}
				} break ;
				case Kind::ChildEnd   :
				case Kind::ChildEndFd : {
					int ws ;
					if (kind==Kind::ChildEnd) { ::waitpid(_child.pid,&ws,0) ;                             wstatus = ws      ; } // wstatus is atomic, cant take its addresss as a int*
					else                      { int cnt=::read(fd,&::ref(char()),1) ; SWEAR(cnt==1,cnt) ; ws      = wstatus ; } // wstatus is already set, just flush fd
					trace(kind,fd,_child.pid,to_hex(uint(ws))) ;
					SWEAR(!WIFSTOPPED(ws),_child.pid) ;          // child must have ended if we are here
					end_date   = New                      ;
					_end_child = end_date + network_delay ;      // wait at most network_delay for reporting & stdout & stderr to settle down
					_exec_trace(end_date,"end_job") ;
					if      (WIFEXITED  (ws)) set_status(             WEXITSTATUS(ws)!=0 ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(ws)) set_status( is_sig_sync(WTERMSIG   (ws))   ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                           fail("unexpected wstatus : ",ws) ;
					if (kind==Kind::ChildEnd) epoll.del_pid(_child.pid       ) ;
					else                      epoll.del    (false/*write*/,fd) ;
					_wait &= ~Kind::ChildEnd ;
					/**/                   epoll.dec() ;                                                                      // dont wait for new connections from job (but process those that come)
					if (+server_master_fd) epoll.dec() ;                                                                      // idem for connections from server
					trace("close",kind,status,"wait",_wait,+epoll) ;
					_child.waited() ;                                                                                         // _child has been waited without calling _child.wait()
				} break ;
				case Kind::JobMaster    :
				case Kind::ServerMaster : {
					bool is_job = kind==Kind::JobMaster ;
					Fd   slave  ;
					if (is_job) { SWEAR( fd==job_master_fd    , fd , job_master_fd    ) ; slave = job_master_fd   .accept().detach() ; epoll.add_read(slave,Kind::JobSlave   ) ; }
					else        { SWEAR( fd==server_master_fd , fd , server_master_fd ) ; slave = server_master_fd.accept().detach() ; epoll.add_read(slave,Kind::ServerSlave) ; }
					trace(kind,fd,"read_slave",STR(is_job),slave,"wait",_wait,+epoll) ;
					slaves[slave] ;                                                                                           // allocate entry
				} break ;
				case Kind::ServerSlave : {
					JobMngtRpcReply jmrr        ;
					auto&           slave_entry = slaves.at(fd) ;
					try         { if (!slave_entry.first.receive_step(fd,jmrr)) { trace(kind,fd,"...") ; continue ; } }
					catch (...) { trace("no_jmrr",jmrr) ; jmrr.proc = {} ;                                            }       // fd was closed, ensure no partially received jmrr
					trace(kind,fd,jmrr) ;
					Fd rfd = jmrr.fd ;                                                                                        // capture before move
					if (jmrr.seq_id==seq_id) {
						switch (jmrr.proc) {
							case JobMngtProc::DepVerbose : {
								Pdate now { New } ;
								for( auto const& [ok,crc] : jmrr.dep_infos ) switch (ok) {
									case Yes   : _exec_trace( now , "dep_verbose.reply" , ::string(crc) ) ; break ;
									case Maybe : _exec_trace( now , "dep_verbose.reply" , "???"         ) ; break ;
									case No    : _exec_trace( now , "dep_verbose.reply" , "error"       ) ; break ;
								}
							} break ;
							case JobMngtProc::Heartbeat  :                                                                   break ;
							case JobMngtProc::Kill : _exec_trace(New,"killed"     ) ; set_status(Status::Killed ) ; kill() ; break ;
							case JobMngtProc::None : _exec_trace(New,"lost_server") ; set_status(Status::Killed ) ; kill() ; break ;
							case JobMngtProc::ChkDeps :
								if (jmrr.ok==Maybe) {
									_exec_trace( New , "chk_deps.killed" ) ;
									set_status(Status::ChkDeps) ; kill() ;
									rfd = {} ;                                                                                // dont reply to ensure job waits if sync
								} else {
									_exec_trace( New , cat("chk_deps.",jmrr.ok==Yes?"ok":"err") ) ;
								}
							break ;
							case JobMngtProc::Decode :
							case JobMngtProc::Encode : {
								SWEAR(+jmrr.fd) ;
								_exec_trace( New , snake(jmrr.proc)+".reply" , jmrr.txt ) ;
								auto it = _codec_files.find(jmrr.fd) ;
								_new_access( rfd , PD(New) , ::move(it->second) , {.accesses=Access::Reg} , jmrr.crc , snake_str(jmrr.proc) ) ;
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
					epoll.close(false/*write*/,fd) ;
					trace("close",kind,fd,"wait",_wait,+epoll) ;
				} break ;
				case Kind::JobSlave : {
					Jerr jerr         ;
					auto sit          = slaves.find(fd) ;
					auto& slave_entry = sit->second     ;
					try                       { if (!slave_entry.first.receive_step(fd,jerr)) { trace(kind,fd,"...") ; continue ; } }
					catch (::string const& e) { trace("no_jerr",kind,fd,jerr,e) ; jerr.proc = Proc::None ; }                          // fd was closed, ensure no partially received jerr
					Proc proc  = jerr.proc ;                                                                                          // capture essential info so as to be able to move jerr
					bool sync_ = jerr.sync ;                                                                                          // .
					if ( proc!=Proc::Access || sync_ ) trace(kind,fd,proc,STR(sync_)) ;                                               // accesses are traced when processed
					switch (proc) {
						case Proc::Confirm : {
							trace("confirm",jerr.id,jerr.digest.write) ;
							auto jit = slave_entry.second.find(jerr.id) ;
							SWEAR(jit!=slave_entry.second.end()  ) ;                                                                  // ensure we can find access to confirm
							SWEAR(jit->second.digest.write==Maybe) ;                                                                  // ensure confirmation is required
							SWEAR(jerr       .digest.write!=Maybe) ;                                                                  // ensure we confirm/infirm
							jit->second.digest.write = jerr.digest.write ;
							_new_accesses(fd,::move(jit->second)) ;
							slave_entry.second.erase(jit) ;
						} break ;
						case Proc::None :
							epoll.close(false/*write*/,fd) ;
							trace("close",kind,fd,"wait",_wait,+epoll) ;
							for( auto& [id,j] : slave_entry.second ) _new_accesses(fd,::move(j)) ;                                    // process deferred entries although with uncertain outcome
							slaves.erase(sit) ;
						break ;
						case Proc::Access :
							// for read accesses, trying is enough to trigger a dep, so confirm is useless
							if      (jerr.digest.write!=Maybe                                    ) _new_accesses(fd,::move(jerr)) ;
							else if (!slave_entry.second.try_emplace(jerr.id,::move(jerr)).second) FAIL()                         ;   // check no id clash and defer until confirm resolution
						break ;
						case Proc::Tmp :
							if (!seen_tmp) {
								_exec_trace(jerr.date,"access_tmp") ;
								seen_tmp = true ;
							}
						break ;
						case Proc::Guard :
							_new_guards(fd,::move(jerr)) ;
						break ;
						case Proc::DepVerbose :
							for( auto const& [f,_] : jerr.files ) _exec_trace(jerr.date,"dep_verbose.req",f) ;
							jerr.txt.clear() ;                                                                                        // prevent tracing for user as access is already traced
							_send_to_server(fd,::move(jerr)) ;
						goto NoReply ;
						case Proc::Decode :
						case Proc::Encode :
							_exec_trace(jerr.date,snake(proc)+".req",jerr.txt) ;
							_send_to_server(fd,::move(jerr)) ;
						goto NoReply ;
						case Proc::ChkDeps :
							_exec_trace(jerr.date,"chk_deps.req") ;
							delayed_check_deps[fd] = ::move(jerr) ;
						goto NoReply ;                                                                                                // if sync, reply is delayed as well
						case Proc::Panic :
							if (!panic_seen) {                                                                                        // report only first panic
								_exec_trace(jerr.date,"panic",jerr.txt) ;
								set_status(Status::Err,jerr.txt) ;
								kill() ;
								panic_seen = true ;
							}
						[[fallthrough]] ;
						case Proc::Trace :
							_exec_trace(jerr.date,"trace",jerr.txt) ;
							trace(jerr.txt) ;
						break ;
					DF}
					if (sync_) sync( fd , JobExecRpcReply(proc) ) ;
				NoReply : ;
				} break ;
			DF}
		}
	}
Return :
	SWEAR(!_child) ;                                                                                                                  // _child must have been waited by now
	trace("done",status) ;
	SWEAR(status!=Status::New) ;
	reorder(true/*at_end*/) ;                                                                                                         // ensure server sees a coherent view
	return status ;
}

// reorder accesses in chronological order and suppress implied dependencies :
// - when a file is depended upon, its uphill directories are implicitly depended upon under the following conditions, no need to keep them and this significantly decreases the number of deps
//   - either file exists
//   - or dir is only accessed as link
// - suppress dir when one of its sub-files appears before            (and condition above is satisfied)
// - suppress dir when one of its sub-files appears immediately after (and condition above is satisfied)
void Gather::reorder(bool at_end) {
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	Trace trace("reorder") ;
	::stable_sort(                                                                  // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b ) -> bool {
			return a.second.first_read().first < b.second.first_read().first ;
		}
	) ;
	// 1st pass (backward) : note dirs immediately preceding sub-files
	::vector<::vmap_s<AccessInfo>::reverse_iterator> lasts   ;                      // because of parallel deps, there may be several last deps
	Pdate                                            last_pd = Pdate::Future ;
	for( auto it=accesses.rbegin() ; it!=accesses.rend() ; it++ ) {
		::string const& file   = it->first         ;
		::AccessDigest& digest = it->second.digest ;
		if (digest.write!=No)                   goto NextDep ;
		if (+digest.dflags  ) { lasts.clear() ; goto NextDep ; }
		if (!digest.accesses)                   goto NextDep ;
		for( auto last : lasts ) {
			if (!( last->first.starts_with(file) && last->first[file.size()]=='/' ))                                                                                            continue     ;
			if (   last->second.dep_info.exists()==Yes                             ) { trace("skip_from_next"  ,file) ; digest.accesses  = {}           ;                       goto NextDep ; }
			else                                                                     { trace("no_lnk_from_next",file) ; digest.accesses &= ~Access::Lnk ; if (!digest.accesses) goto NextDep ; }
		}
		if ( Pdate pd=it->second.first_read().first ; pd<last_pd ) {
			lasts.clear() ;                                                         // not a parallel dep => clear old ones that are no more last
			last_pd = pd ;
		}
		lasts.push_back(it) ;
	NextDep : ;
	}
	// 2nd pass (forward) : suppress dirs of seen files and previously noted dirs
	::umap_s<bool/*sub-file exists*/> dirs  ;
	size_t                            i_dst = 0     ;
	bool                              cpy   = false ;
	for( auto& access : accesses ) {
		::string const& file   = access.first         ;
		::AccessDigest& digest = access.second.digest ;
		if ( digest.write==No && !digest.dflags && !digest.tflags ) {
			auto it = dirs.find(file+'/') ;
			if (it!=dirs.end()) {
				if (it->second) { trace("skip_from_prev"  ,file) ; digest.accesses  = {}           ; }
				else            { trace("no_lnk_from_prev",file) ; digest.accesses &= ~Access::Lnk ; }
			}
			if (!digest.accesses) {
				if (!at_end) access_map.erase(file) ;
				cpy = true ;
				continue ;
			}
		}
		bool exists = access.second.dep_info.exists()==Yes ;
		for( ::string dir_s=dir_name_s(file) ; +dir_s&&dir_s!="/" ; dir_s=dir_name_s(dir_s) ) {
			auto [it,inserted] = dirs.try_emplace(dir_s,exists) ;
			if (!inserted) {
				if (it->second>=exists) break ;                                     // all uphill dirs are already inserted if a dir has been inserted
				it->second = exists ;                                               // record existence of a sub-file as soon as one if found
			}
		}
		if (cpy) accesses[i_dst] = ::move(access) ;
		i_dst++ ;
	}
	accesses.resize(i_dst) ;
	for( NodeIdx i : iota(accesses.size()) ) access_map.at(accesses[i].first) = i ; // always recompute access_map as accesses has been sorted
}
