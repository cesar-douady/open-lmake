// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
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

::string& operator+=( ::string& os , Gather::AccessInfo const& ai ) {                                             // START_OF_NO_COV
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
}                                                                                                                 // END_OF_NO_COV

void Gather::AccessInfo::update( PD pd , AccessDigest ad , DI const& di ) {
	digest.tflags       |= ad.tflags       ;
	digest.extra_tflags |= ad.extra_tflags ;
	digest.dflags       |= ad.dflags       ;
	digest.extra_dflags |= ad.extra_dflags ;
	//
	bool tfi =        ad.extra_tflags[ExtraTflag::Ignore] ;
	bool dfi = tfi || ad.extra_dflags[ExtraDflag::Ignore] ;               // tfi also prevents reads from being visible
	//
	for( Access a : iota(All<Access>) ) if (read[+a]<=pd) goto NotFirst ;
	dep_info = dfi ? DI() : di ;                                          // if ignore, record empty info (which will mask further info)
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

::string& operator+=( ::string& os , Gather const& gd ) { // START_OF_NO_COV
	/**/             os << "Gather(" << gd.accesses ;
	if (gd.seen_tmp) os <<",seen_tmp"               ;
	return           os << ')'                      ;
}                                                         // END_OF_NO_COV

void Gather::_new_access( Fd fd , PD pd , ::string&& file , AccessDigest ad , DI const& di , Comment c , CommentExts ces ) {
	SWEAR( +file , c , ces        ) ;
	SWEAR( +pd   , c , ces , file ) ;
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
		if (+c) {
			if (is_new) _exec_trace( pd , c , ces , accesses.back().first ) ;                                                   // file has been ::move()'ed
			else        _exec_trace( pd , c , ces , file                  ) ;
		}
		Trace("_new_access", fd , STR(is_new) , pd , ad , di , _parallel_id , c , ces , old_info , "->" , *info , it->first ) ; // only trace if something changes
	}
}

void Gather::new_dep( PD pd , ::string&& dep , DepDigest&& dd , ::string const& stdin ) {
		bool is_stdin = dep==stdin ;
		if (is_stdin) {                               // stdin is read
			if (!dd.accesses) dd.sig(FileInfo(dep)) ; // record now if not previously accessed
			dd.accesses |= Access::Reg ;
		}
		_new_access( pd , ::move(dep) , {.accesses=dd.accesses,.dflags=dd.dflags} , dd , is_stdin?Comment::stdin:Comment::staticDep ) ;
}

void Gather::new_exec( PD pd , ::string const& exe , Comment c ) {
	RealPath              rp       { autodep_env }                    ;
	RealPath::SolveReport sr       = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(sr) )
		if (!Record::s_is_simple(f)) _new_access( pd , ::move(f) , {.accesses=a} , FileInfo(f) , c ) ;
}

bool/*sent*/ Gather::_send_to_server( JobMngtRpcReq const& jmrr ) {
	Trace trace("_send_to_server",jmrr) ;
	for( int i=3 ;; i-- ) {                                                      // retry if server exists and cannot be reached
		bool sent = false ;
		try {
			ClientSockFd csfd { service_mngt } ;                                 // ensure csfd is closed only after sent = true
			//vvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send( csfd , jmrr ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^
			sent = true ;
		} catch (::string const& e) {
			if (i>1 ) { trace("retry"               ,i,STR(sent)) ; continue ; }
			if (sent) { trace("server_not_available",e          ) ; throw    ; } // server exists but could not be reached (error when closing socket)
			else      { trace("no_server"                       ) ; break    ; }
		}
		return true/*sent*/ ;
	}
	return false/*sent*/ ;
}

void Gather::_send_to_server( Fd fd , Jerr&& jerr ) {
	Trace trace("_send_to_server",fd,jerr) ;
	//
	if (!jerr.sync) fd = {} ;                                                                                            // dont reply if not sync
	JobMngtRpcReq jmrr { .proc=JobMngtProc::None , .seq_id=seq_id , .job=job , .fd=fd } ;
	//
	switch (jerr.proc) {
		case Proc::ChkDeps :
			_exec_trace( jerr.date , jerr.comment , jerr.comment_exts ) ;
			jmrr.proc = JobMngtProc::ChkDeps ;
			reorder(false/*at_end*/) ;                                                                                   // ensure server sees a coherent view
			jmrr.deps = cur_deps_cb() ;
		break ;
		case Proc::DepVerbose : {
			auto                it    = _dep_verboses.find(fd) ; SWEAR(it!=_dep_verboses.end(),fd,_dep_verboses) ;
			::vmap_s<FileInfo>& files = it->second             ;
			jmrr.proc = JobMngtProc::DepVerbose ;
			jmrr.deps.reserve(files.size()) ;
			for( auto& [f,fi] : files ) {
				_exec_trace( jerr.date , jerr.comment , jerr.comment_exts , f ) ;
				_new_access( fd , jerr.date , ::copy(f) , jerr.digest , fi , jerr.comment , jerr.comment_exts ) ;
				jmrr.deps.emplace_back( ::move(f) , DepDigest(jerr.digest.accesses,fi,{}/*dflags*/,true/*parallel*/) ) ; // no need for flags to ask info
			}
			_dep_verboses.erase(it) ;
		} break ;
		case Proc::Decode :
		case Proc::Encode : {
			SWEAR( jerr.sync==Yes , jerr ) ;
			auto      it   = _codecs.find(fd)  ; SWEAR(it!=_codecs.end(),fd,_codecs) ;
			::string& file = it->second.first  ;
			::string& ctx  = it->second.second ;
			if (jerr.proc==Proc::Encode) { jmrr.proc = JobMngtProc::Encode ; jmrr.min_len = jerr.min_len() ; _codec_files[fd] = Codec::mk_encode_node( file , ctx , jerr.file ) ; }
			else                         { jmrr.proc = JobMngtProc::Decode ;                                 _codec_files[fd] = Codec::mk_decode_node( file , ctx , jerr.file ) ; }
			jmrr.file = ::move(file     ) ;
			jmrr.ctx  = ::move(ctx      ) ;
			jmrr.txt  = ::move(jerr.file) ;
			_codecs.erase(it) ;
		} break ;
	DF}                                                                                                                  // NO_COV
	if (_send_to_server(jmrr)) { _n_server_req_pending++ ; trace("wait_server",_n_server_req_pending) ; }
	else                         sync(fd,{}) ;                                                                           // send an empty reply, job will invent something reasonable
}

void Gather::_ptrace_child( Fd report_fd , ::latch* ready ) {
	t_thread_key = 'P' ;
	AutodepPtrace::s_init(autodep_env) ;
	_child.pre_exec = AutodepPtrace::s_prepare_child  ;
	//vvvvvvvvvvvv
	_child.spawn() ;                                                        // /!\ although not mentioned in man ptrace, child must be launched by the tracing thread
	//^^^^^^^^^^^^
	ready->count_down() ;                                                   // signal main thread that _child.pid is available
	AutodepPtrace autodep_ptrace{_child.pid} ;
	wstatus = autodep_ptrace.process() ;
	ssize_t cnt = ::write(report_fd,&::ref(char()),1) ; SWEAR(cnt==1,cnt) ; // report child end
	Record::s_close_reports() ;
}

Fd Gather::_spawn_child() {
	SWEAR(+cmd_line) ;
	Trace trace("_spawn_child",child_stdin,child_stdout,child_stderr) ;
	//
	Fd   child_fd  ;
	Fd   report_fd ;
	bool is_ptrace = method==AutodepMethod::Ptrace ;
	//
	_add_env          = { {"LMAKE_AUTODEP_ENV",autodep_env} } ;                                    // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	_child.as_session = as_session                            ;
	_child.stdin_fd   = child_stdin                           ;
	_child.stdout_fd  = child_stdout                          ;
	_child.stderr_fd  = child_stderr                          ;
	_child.first_pid  = first_pid                             ;
	if (is_ptrace) {                                                                               // PER_AUTODEP_METHOD : handle case
		// we split the responsability into 2 threads :
		// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
		// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
		AcPipe pipe { New , 0/*flags*/ , true/*no_std*/ } ;
		child_fd  = pipe.read .detach() ;
		report_fd = pipe.write.detach() ;
	} else {
		if (method>=AutodepMethod::Ld) {                                                           // PER_AUTODEP_METHOD : handle case
			::string env_var ;
			switch (method) {                                                                      // PER_AUTODEP_METHOD : handle case
				#if HAS_32
					#define DOLLAR_LIB "$LIB"                                                      // 32 bits is supported, use ld.so automatic detection feature
				#else
					#define DOLLAR_LIB "lib"                                                       // 32 bits is not supported, use standard name
				#endif
				#if HAS_LD_AUDIT
					case AutodepMethod::LdAudit           : env_var = "LD_AUDIT"   ; _add_env[env_var] = *g_lmake_root_s + "_d" DOLLAR_LIB "/ld_audit.so"            ; break ;
				#endif
					case AutodepMethod::LdPreload         : env_var = "LD_PRELOAD" ; _add_env[env_var] = *g_lmake_root_s + "_d" DOLLAR_LIB "/ld_preload.so"          ; break ;
					case AutodepMethod::LdPreloadJemalloc : env_var = "LD_PRELOAD" ; _add_env[env_var] = *g_lmake_root_s + "_d" DOLLAR_LIB "/ld_preload_jemalloc.so" ; break ;
				#undef DOLLAR_LIB
			DF}                                                                                    // NO_COV
			if (env) { if (env->contains(env_var)) _add_env[env_var] += ':' + env->at(env_var) ; }
			else     { if (has_env      (env_var)) _add_env[env_var] += ':' + get_env(env_var) ; }
		}
		new_exec( New , mk_glb(cmd_line[0],autodep_env.sub_repo_s) ) ;
	}
	start_date      = New                    ;                                                     // record job start time as late as possible
	_child.cmd_line = cmd_line               ;
	_child.env      = env                    ;
	_child.add_env  = &_add_env              ;
	_child.cwd_s    = autodep_env.sub_repo_s ;
	if (is_ptrace) {
		::latch ready{1} ;
		_ptrace_thread = ::jthread( _s_ptrace_child , this , report_fd , &ready ) ;                // /!\ _child must be spawned from tracing thread
		ready.wait() ;                                                                             // wait until _child.pid is available
	} else {
		//vvvvvvvvvvvv
		_child.spawn() ;
		//^^^^^^^^^^^^
	}
	trace("child_pid",_child.pid) ;
	return child_fd ;                                                                              // child_fd is only used with ptrace
}

struct JobSlaveEntry {
	using Jerr = JobExecRpcReq ;
	static constexpr size_t BufSz = 1<<16 ;
	static_assert(BufSz>2*Jerr::MaxSz) ;                          // buf must be able to hold at least 2 messages
	// data
	::umap<Jerr::Id,::vector<Jerr>> jerrs      ;                  // jerrs are waiting for confirmation
	size_t                          buf_sz     = 0 ;              // part of buf actually filled
	char                            buf[BufSz] ;
} ;
::string& operator+=( ::string& os , JobSlaveEntry const& jse ) { // START_OF_NO_COV
	First first ;
	/**/            os << "JobSlaveEntry("          ;
	if (+jse.jerrs) os <<first("",",")<< jse.jerrs  ;
	if (jse.buf_sz) os <<first("",",")<< jse.buf_sz ;
	return          os <<')'                        ;
}                                                                 // END_OF_NO_COV


Status Gather::exec_child() {
	//
	using Event = Epoll<Kind>::Event ;
	Trace trace("exec_child",STR(as_session),method,autodep_env,cmd_line) ;
	//
	if (env) { trace("env",*env) ; swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ; }
	else                           swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	//
	ServerSockFd             job_master_fd      { New }       ;
	AcFd                     fast_report_fd     ;                                                     // always open, never waited for
	AcFd                     child_fd           ;
	Epoll<Kind>              epoll              { New }       ;
	Status                   status             = Status::New ;
	::umap<Fd,Jerr>          delayed_check_deps ;                                                     // check_deps events are delayed to ensure all previous deps are received
	size_t                   live_out_pos       = 0           ;
	::umap<Fd,IMsgBuf      > server_slaves      ;
	::umap<Fd,JobSlaveEntry> job_slaves         ;                                                     // Jerr's are waiting for confirmation
	bool                     panic_seen         = false       ;
	PD                       end_timeout        = PD::Future  ;
	PD                       end_child          = PD::Future  ;
	PD                       end_kill           = PD::Future  ;
	PD                       end_heartbeat      = PD::Future  ;                                       // heartbeat to probe server when waiting for it
	bool                     timeout_fired      = false       ;
	size_t                   kill_step          = 0           ;
	//
	auto set_status = [&]( Status status_ , ::string const& msg_={} )->void {
		if (status==Status::New) status = status_ ;                                                   // only record first status
		if (+msg_              ) msg << set_nl << msg_ ;
	} ;
	auto kill = [&](bool next_step=false)->void {
		trace("kill",STR(next_step),kill_step,STR(as_session),_child.pid,_wait) ;
		if      (next_step             ) SWEAR(kill_step<=kill_sigs.size()) ;
		else if (kill_step             ) return ;
		if      (!_wait[Kind::ChildEnd]) return ;
		int   sig = kill_step==kill_sigs.size() ? SIGKILL : kill_sigs[kill_step] ;
		Pdate now { New }                                                        ;
		trace("kill_sig",sig) ;
		//                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if ( sig && _child.pid>1 ) kill_process(_child.pid,sig,as_session/*as_group*/) ;
		//                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		set_status(Status::Killed) ;
		if      (kill_step==kill_sigs.size()) end_kill = Pdate::Future      ;
		else if (end_kill==Pdate::Future    ) end_kill = now      +Delay(1) ;
		else                                  end_kill = end_kill +Delay(1) ;
		_exec_trace( now , Comment::kill , {} , cat(sig) ) ;
		kill_step++ ;
		trace("kill_done",end_kill) ;
	} ;
	auto open_fast_report_fd = [&]()->void {
		SWEAR(+autodep_env.fast_report_pipe) ;
		fast_report_fd = ::open(autodep_env.fast_report_pipe.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK) ; // O_NONBLOCK is important to avoid blocking waiting for child, no impact on epoll-controled ops
		//
		if (+fast_report_fd) {                                                         // work w/o fast report if it does not work, XXX! : seen on some instances of Centos7
			trace("open_fast_report_fd",autodep_env.fast_report_pipe,fast_report_fd) ;
			epoll.add_read( fast_report_fd , Kind::JobSlave ) ;
			epoll.dec() ;                                                              // fast_report_fd is always open and never waited for as we never know when a job may want to report on this fd
			job_slaves[fast_report_fd] ;                                               // allocate entry
		} else {
			trace("open_fast_report_fd",autodep_env.fast_report_pipe,::strerror(errno)) ;
			autodep_env.fast_report_pipe.clear() ;
		}
	} ;
	//
	autodep_env.service = job_master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	if (+autodep_env.fast_report_pipe) {
		if ( ::mkfifo( autodep_env.fast_report_pipe.c_str() , 0666 )<0 ) SWEAR(errno=EEXIST,errno) ; // if it already exists, assume it is already a fifo
		open_fast_report_fd() ;
	}
	if (+server_master_fd) {
		epoll.add_read(server_master_fd,Kind::ServerMaster) ;
		trace("read_server_master",server_master_fd,"wait",_wait,+epoll) ;
	}
	_wait = Kind::ChildStart ;
	trace("start","wait",_wait,+epoll) ;
	for(;;) {
		Pdate now = New ;
		if (now>end_child) {
			_exec_trace(now,Comment::stillAlive) ;
			if (!_wait[Kind::ChildEnd]) {
				SWEAR( _wait[Kind::Stdout] || _wait[Kind::Stderr] , _wait , now , end_child ) ;      // else we should already have exited
				::string msg_ ;
				if ( _wait[Kind::Stdout]                        ) msg_ += "stdout " ;
				if ( _wait[Kind::Stdout] && _wait[Kind::Stderr] ) msg_ += "and "    ;
				if (                        _wait[Kind::Stderr] ) msg_ += "stderr " ;
				msg_ << "still open after job having been dead for " << network_delay.short_str() ;
				set_status(Status::Err,msg_) ;
			}
			else if ( kill_step && kill_step< kill_sigs.size() ) set_status(Status::Err,"still alive after having been killed "s+kill_step       +" times"                      ) ;
			else if (              kill_step==kill_sigs.size() ) set_status(Status::Err,"still alive after having been killed "s+kill_sigs.size()+" times followed by a SIGKILL") ;
			else if ( timeout_fired                            ) set_status(Status::Err,"still alive after having timed out and been killed with SIGKILL"                       ) ;
			else                                                 FAIL("dont know why still active") ;                                                                               // NO_COV
			break ;
		}
		if (now>end_kill) {
			kill(true/*next*/) ;
		}
		if ( now>end_timeout && !timeout_fired ) {
			_exec_trace(now,Comment::timeout) ;
			set_status(Status::Err,"timeout after "+timeout.short_str()) ;
			kill() ;
			timeout_fired = true          ;
			end_timeout   = Pdate::Future ;
		}
		if (!kill_step) {
			if (end_heartbeat==Pdate::Future) { if ( _n_server_req_pending) end_heartbeat = now + HeartbeatTick ; }
			else                              { if (!_n_server_req_pending) end_heartbeat = Pdate::Future       ; }
			if (now>end_heartbeat) {
				trace("server_heartbeat") ;
				if (_send_to_server({.proc=JobMngtProc::Heartbeat,.seq_id=seq_id,.job=job})) end_heartbeat += HeartbeatTick ;
				else                                                                         kill() ;
			}
		}
		Delay wait_for ;
		if ( (+epoll||+_wait) && !delayed_check_deps && !_wait[Kind::ChildStart] ) {
			Pdate event_date =                     end_child       ;
			/**/  event_date = ::min( event_date , end_kill      ) ;
			/**/  event_date = ::min( event_date , end_timeout   ) ;
			/**/  event_date = ::min( event_date , end_heartbeat ) ;
			wait_for = event_date<Pdate::Future ? event_date-now : Delay::Forever ;
		}
		::vector<Event> events = epoll.wait(wait_for) ;
		if (!events) {
			if (+delayed_check_deps) {            // process delayed check deps after all other events
				trace("delayed_chk_deps") ;
				for( auto& [fd,jerr] : delayed_check_deps ) _send_to_server(fd,::move(jerr)) ;
				delayed_check_deps.clear() ;
			} else if (_wait[Kind::ChildStart]) { // handle case where we are killed before starting : create child when we have processed waiting connections from server
				try {
					/**/          child_fd     = _spawn_child()       ;
					/**/          _wait       &= ~Kind::ChildStart    ;
					if (+timeout) end_timeout  = start_date + timeout ;
					_exec_trace( start_date , Comment::startJob ) ;
					trace("started","wait",_wait,+epoll) ;
				} catch(::string const& e) {
					if (child_stderr==Child::PipeFd) stderr = ensure_nl(e) ;
					else                             child_stderr.write(ensure_nl(e)) ;
					status = Status::EarlyErr ;
					goto Return ;
				}
				if (child_stdout==Child::PipeFd) { epoll.add_read( _child.stdout  , Kind::Stdout     ) ; _wait |= Kind::Stdout   ; trace("read_stdout    ",_child.stdout ,"wait",_wait,+epoll) ; }
				if (child_stderr==Child::PipeFd) { epoll.add_read( _child.stderr  , Kind::Stderr     ) ; _wait |= Kind::Stderr   ; trace("read_stderr    ",_child.stderr ,"wait",_wait,+epoll) ; }
				if (+child_fd                  ) { epoll.add_read( child_fd       , Kind::ChildEndFd ) ; _wait |= Kind::ChildEnd ; trace("read_child     ",child_fd      ,"wait",_wait,+epoll) ; }
				else                             { epoll.add_pid ( _child.pid     , Kind::ChildEnd   ) ; _wait |= Kind::ChildEnd ; trace("read_child_proc",               "wait",_wait,+epoll) ; }
				/**/                               epoll.add_read( job_master_fd  , Kind::JobMaster  ) ;                           trace("read_job_master",job_master_fd ,"wait",_wait,+epoll) ;
			} else if (!wait_for) {
				break ;                           // if we wait for nothing and no events come, we are done
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
							if (live_out)
								if ( size_t pos = buf_view.rfind('\n')+1 ;  pos ) {
									size_t len = old_sz + pos - live_out_pos ;
									trace("live_out",live_out_pos,len) ;
									//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
									OMsgBuf().send( ClientSockFd(service_mngt) , JobMngtRpcReq({ .proc=JobMngtProc::LiveOut , .seq_id=seq_id , .job=job , .txt=stdout.substr(live_out_pos,len) }) ) ;
									//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
									live_out_pos += len ;
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
					trace(kind,fd,_child.pid,ws) ;
					SWEAR(!WIFSTOPPED(ws),_child.pid) ;                                                   // child must have ended if we are here
					end_date  = New                      ;
					end_child = end_date + network_delay ;                                                // wait at most network_delay for reporting & stdout & stderr to settle down
					_exec_trace( end_date , Comment::endJob , {}/*CommentExt*/ , to_hex(uint16_t(ws)) ) ;
					if      (WIFEXITED  (ws)) set_status(             WEXITSTATUS(ws)!=0 ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(ws)) set_status( is_sig_sync(WTERMSIG   (ws))   ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                      FAIL("unexpected wstatus : ",ws) ;                                              // NO_COV defensive programming
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
					if (is_job) job_slaves   [slave] ;                                                                        // allocate entry
					else        server_slaves[slave] ;                                                                        // .
				} break ;
				case Kind::ServerSlave : {
					JobMngtRpcReply jmrr ;
					IMsgBuf&        buf  = server_slaves.at(fd) ;
					try         { if (!buf.receive_step(fd,jmrr)) { trace(kind,fd,"...") ; continue ; } }
					catch (...) { trace("no_jmrr",jmrr) ; jmrr.proc = {} ;                              }                     // fd was closed, ensure no partially received jmrr
					trace(kind,fd,jmrr) ;
					Fd rfd = jmrr.fd ;                                                                                        // capture before move
					if (jmrr.seq_id==seq_id) {
						switch (jmrr.proc) {
							case JobMngtProc::DepVerbose : {
								Pdate now { New } ;
								_n_server_req_pending-- ; trace("resume_server",_n_server_req_pending) ;
								for( auto const& [ok,crc] : jmrr.dep_infos ) switch (ok) {
									case Yes   : _exec_trace( now , Comment::depend , CommentExt::Reply , ::string(crc) ) ; break ;
									case Maybe : _exec_trace( now , Comment::depend , CommentExt::Reply , "???"         ) ; break ;
									case No    : _exec_trace( now , Comment::depend , CommentExt::Reply , "error"       ) ; break ;
								}
							} break ;
							case JobMngtProc::Heartbeat :                                                                                                      break ;
							case JobMngtProc::Kill      : _exec_trace( New , Comment::kill       , CommentExt::Reply ) ; set_status(Status::Killed) ; kill() ; break ;
							case JobMngtProc::None      : _exec_trace( New , Comment::lostServer                     ) ; set_status(Status::Killed) ; kill() ; break ;
							case JobMngtProc::ChkDeps :
								_n_server_req_pending-- ; trace("resume_server",_n_server_req_pending) ;
								if (jmrr.ok==Maybe) {
									_exec_trace( New , Comment::chkDeps , {CommentExt::Reply,CommentExt::Killed} , jmrr.txt ) ;
									set_status( Status::ChkDeps , "waiting dep : "+jmrr.txt ) ;
									kill() ;
									rfd = {} ;                                                                                // dont reply to ensure job waits if sync
								} else {
									_exec_trace( New , Comment::chkDeps , +jmrr.ok?CommentExts(CommentExt::Reply):CommentExts(CommentExt::Reply,CommentExt::Err) , jmrr.txt ) ;
								}
							break ;
							case JobMngtProc::Decode :
							case JobMngtProc::Encode : {
								SWEAR(+jmrr.fd) ;
								_n_server_req_pending-- ; trace("resume_server",_n_server_req_pending) ;
								_exec_trace( New , jmrr.proc==JobMngtProc::Encode?Comment::encode:Comment::decode , CommentExt::Reply , jmrr.txt ) ;
								auto it = _codec_files.find(jmrr.fd) ;
								SWEAR(it!=_codec_files.end(),jmrr,_codec_files) ;
								_new_access( rfd , PD(New) , ::move(it->second) , {.accesses=Access::Reg} , jmrr.crc , jmrr.proc==JobMngtProc::Encode?Comment::encode:Comment::decode ) ;
								_codec_files.erase(it) ;
							} break ;
							case JobMngtProc::AddLiveOut : {
								trace("add_live_out",STR(live_out),live_out_pos) ;
								if (!live_out) {
									live_out     = true                 ;
									live_out_pos = stdout.rfind('\n')+1 ;
								}
								if (live_out_pos)
									//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
									OMsgBuf().send( ClientSockFd(service_mngt) , JobMngtRpcReq({ .proc=JobMngtProc::AddLiveOut , .seq_id=seq_id , .job=job , .txt=stdout.substr(0,live_out_pos) }) ) ;
									//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							} break ;
						DF}                                                                                                   // NO_COV
						if (+rfd) {
							JobExecRpcReply jerr ;
							switch (jmrr.proc) {
								case JobMngtProc::None       :                                                                                                               break ;
								case JobMngtProc::ChkDeps    : SWEAR(jmrr.ok!=Maybe) ; jerr = { .proc=Proc::ChkDeps    , .ok=jmrr.ok                                     } ; break ;
								case JobMngtProc::DepVerbose :                         jerr = { .proc=Proc::DepVerbose ,               .dep_infos=::move(jmrr.dep_infos) } ; break ;
								case JobMngtProc::Decode     :                         jerr = { .proc=Proc::Decode     , .ok=jmrr.ok , .txt      =::move(jmrr.txt      ) } ; break ;
								case JobMngtProc::Encode     :                         jerr = { .proc=Proc::Encode     , .ok=jmrr.ok , .txt      =::move(jmrr.txt      ) } ; break ;
							DF}                                                                                                                                                      // NO_COV
							trace("reply",jerr) ;
							//vvvvvvvvvvvvvvvvvvvvvvvv
							sync( rfd , ::move(jerr) ) ;
							//^^^^^^^^^^^^^^^^^^^^^^^^
						}
					}
					epoll.close(false/*write*/,fd) ;
					trace("close",kind,fd,"wait",_wait,+epoll) ;
				} break ;
				case Kind::JobSlave : {
					auto  sit         = job_slaves.find(fd) ; SWEAR(sit!=job_slaves.end(),fd,job_slaves) ;
					auto& slave_entry = sit->second         ;
					//
					ssize_t cnt = ::read( fd , slave_entry.buf+slave_entry.buf_sz , JobSlaveEntry::BufSz-slave_entry.buf_sz ) ;
					if (cnt<=0) {
						SWEAR(slave_entry.buf_sz==0,slave_entry.buf_sz) ; // ensure no partial message is left unprocessed
						if (fd==fast_report_fd) {
							epoll.del(false/*write*/,fd,false/*wait*/) ;  // fast_report_fd is not waited as it is always open and will be closed as it is an AcFd
							open_fast_report_fd() ;                       // reopen as job may close the pipe and reopen it later
						} else {
							epoll.close(false/*write*/,fd) ;
						}
						trace("close",kind,fd,"wait",_wait,+epoll) ;
						for( auto& [_,um] : slave_entry.jerrs )
							for( JobExecRpcReq& j : um )
								_new_access(fd,::move(j)) ;               // process deferred entries although with uncertain outcome
						job_slaves.erase(sit) ;
					} else {
						slave_entry.buf_sz += cnt ;
						size_t pos = 0 ;
						for(;;) {
							{ if (pos+sizeof(MsgBuf::Len)   >slave_entry.buf_sz) break ; } MsgBuf::Len sz = decode_int<MsgBuf::Len>(slave_entry.buf+pos) ; // read message size
							{ if (pos+sizeof(MsgBuf::Len)+sz>slave_entry.buf_sz) break ; } pos += sizeof(MsgBuf::Len) ;                                    // read message
							auto jerr = deserialize<Jerr>({ slave_entry.buf+pos , sz }) ;
							pos += sz ;
							//
							Proc proc  = jerr.proc      ;                                                                                                  // capture before jerr is ::move()'ed
							bool sync_ = jerr.sync==Yes ;                                                                                                  // Maybe means not sync, only for transport
							if ( fd==fast_report_fd          ) SWEAR(!sync_) ;                                                                             // cannot reply on fast_report_fd
							if ( proc!=Proc::Access || sync_ ) trace(kind,fd,proc,STR(sync_)) ;                                                            // accesses are traced when processed
							switch (proc) {
								case Proc::DepVerbosePush : _dep_verboses[fd].emplace_back( ::move(jerr.file) , jerr.file_info ) ; trace(kind,fd,proc) ; break ;
								case Proc::CodecFile      : _codecs      [fd].first  =      ::move(jerr.file)                    ; trace(kind,fd,proc) ; break ;
								case Proc::CodecCtx       : _codecs      [fd].second =      ::move(jerr.file)                    ; trace(kind,fd,proc) ; break ;
								case Proc::Guard          : _new_guard(fd,::move(jerr)) ;                                                                break ;
								case Proc::DepVerbose     :
								case Proc::Decode         :
								case Proc::Encode         : _send_to_server(fd,::move(jerr)) ;      sync_ = false ; break ;                                // reply is delayed until server reply
								case Proc::ChkDeps        : delayed_check_deps[fd] = ::move(jerr) ; sync_ = false ; break ;                                // if sync, reply is delayed as well
								case Proc::Confirm : {
									trace("confirm",kind,fd,jerr.digest.write,jerr.id) ;
									Trace trace2 ;
									auto it = slave_entry.jerrs.find(jerr.id) ; SWEAR(it!=slave_entry.jerrs.end(),jerr.id,slave_entry.jerrs) ;
									SWEAR(jerr.digest.write!=Maybe) ;                                                                                      // ensure we confirm/infirm
									for ( JobExecRpcReq& j : it->second ) {
										SWEAR(j.digest.write==Maybe) ;
										/**/                    j.digest.write  = jerr.digest.write ;
										if (!jerr.digest.write) j.comment_exts |= CommentExt::Err   ;
										_new_access(fd,::move(j)) ;
									}
									slave_entry.jerrs.erase(it) ;
								} break ;
								case Proc::None :
									if (fd==fast_report_fd) {
										epoll.del(false/*write*/,fd,false/*wait*/) ; // fast_report_fd is not waited as it is always open and will be closed as it is an AcFd
										open_fast_report_fd() ;                      // reopen as job may close the pipe and reopen it later
									} else {
										epoll.close(false/*write*/,fd) ;
									}
									trace("close",kind,fd,"wait",_wait,+epoll) ;
									for( auto& [_,um] : slave_entry.jerrs )
										for( JobExecRpcReq& j : um )
											_new_access(fd,::move(j)) ;              // process deferred entries although with uncertain outcome
									job_slaves.erase(sit) ;
								break ;
								case Proc::Access :
									// for read accesses, trying is enough to trigger a dep, so confirm is useless
									if (jerr.digest.write==Maybe) { trace("maybe",jerr) ; slave_entry.jerrs[jerr.id].push_back(::move(jerr)) ; } // delay until confirmed/infirmed
									else                          _new_access(fd,::move(jerr))                                               ;
								break ;
								case Proc::Tmp :
									if (!seen_tmp) {
										if (no_tmp) {
											_exec_trace( jerr.date , Comment::tmp , CommentExt::Err ) ;
											set_status(Status::Err,"tmp access with no tmp dir") ;
											kill() ;
										} else {
											_exec_trace( jerr.date , Comment::tmp ) ;
										}
										seen_tmp = true ;
									}
								break ;
								case Proc::Panic :                                                                                               // START_OF_NO_COV defensive programming
									if (!panic_seen) {                                                                                           // report only first panic
										_exec_trace( jerr.date , Comment::panic , {} , jerr.file ) ;
										set_status(Status::Err,jerr.file) ;
										kill() ;
										panic_seen = true ;
									}
								[[fallthrough]] ;                                                                                                // END_OF_NO_COV
								case Proc::Trace :                                                                                               // START_OF_NO_COV debug only
									_exec_trace( jerr.date , Comment::trace , {} , jerr.file ) ;
									trace(jerr.file) ;
								break ;                                                                                                          // END_OF_NO_COV
							DF}                                                                                                                  // NO_COV
							if (sync_) sync( fd , {.proc=proc} ) ;
						}
						slave_entry.buf_sz -= pos ;
						::memmove( slave_entry.buf , slave_entry.buf+pos , slave_entry.buf_sz ) ;
					}
				} break ;
			DF}                                                                                                                                  // NO_COV
		}
	}
Return :
	//
	SWEAR(!_child) ;                                                                                                                             // _child must have been waited by now
	trace("done",status) ;
	SWEAR(status!=Status::New) ;
	reorder(true/*at_end*/) ;                                                                                                                    // ensure server sees a coherent view
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
		if (digest.write!=No)               goto NextDep ;
		if (digest.dflags!=DflagsDfltDyn) { lasts.clear() ; goto NextDep ; }
		if (!digest.accesses)               goto NextDep ;
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
		if ( digest.write==No && digest.dflags==DflagsDfltDyn && !digest.tflags ) {
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
