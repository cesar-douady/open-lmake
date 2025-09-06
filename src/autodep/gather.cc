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
using namespace Re   ;
using namespace Time ;

//
// Gather::AccessInfo
//

::string& operator+=( ::string& os , Gather::AccessInfo const& ai ) { // START_OF_NO_COV
	Pdate fr = ai.first_read() ;
	/**/                          os << "AccessInfo("           ;
	if (fr       !=Pdate::Future) os << "R:" <<fr         <<',' ;
	if (ai._allow!=Pdate::Future) os << "A:" <<ai._allow  <<',' ;
	if (ai._write!=Pdate::Future) os << "W:" <<ai._write  <<',' ;
	if (+ai.dep_info            ) os << ai.dep_info       <<',' ;
	/**/                          os << ai.flags                ;
	if (ai._seen!=Pdate::Future ) os <<",seen"                  ;
	return                        os <<')'                      ;
}                                                                     // END_OF_NO_COV

Pdate Gather::AccessInfo::_max_read(bool phys) const {
	if (_washed) {
		if (phys                       ) return {} ;                                  // washing has a physical impact
		if (flags.tflags[Tflag::Target]) return {} ;                                  // if a target, washing is a logical write
	}
	PD                                         res = ::min( _read_ignore , _write ) ;
	if ( !phys && !flags.dep_and_target_ok() ) res = ::min( res          , _allow ) ; // logically, once file is a target, reads are ignored, unless it is also a dep
	return res ;
}

Accesses Gather::AccessInfo::accesses() const {
	PD       ma  = _max_read(false/*phys*/) ;
	Accesses res ;
	for( Access a : iota(All<Access>) ) if (_read[+a]<=ma) res |= a ;
	return res ;
}

Pdate Gather::AccessInfo::first_read() const {
	PD    res = PD::Future               ;
	Pdate mr  = _max_read(false/*phys*/) ;
	//
	for( Access a : iota(All<Access>) ) if (_read[+a]<res) res = _read[+a] ;
	/**/                                if (_read_dir<res) res = _read_dir ;
	/**/                                if (_required<res) res = _required ;
	//
	if (res<=mr) return res           ;
	else         return Pdate::Future ;
}

Pdate Gather::AccessInfo::first_write() const {
	if ( _washed && flags.tflags[Tflag::Target] ) return {}         ;
	if ( _write<=_max_write()                   ) return _write     ;
	else                                          return PD::Future ;
}

::pair<Pdate,bool> Gather::AccessInfo::sort_key() const {
	PD fr = first_read() ;
	if (fr<PD::Future) return { fr            , false } ;
	else               return { first_write() , true  } ;
}

void Gather::AccessInfo::update( PD pd , AccessDigest ad , bool late , DI const& di ) {
	SWEAR(ad.write!=Maybe) ;                                                                                                        // this must have been solved by caller
	if ( ad.flags.extra_tflags[ExtraTflag::Ignore] ) ad.flags.extra_dflags |= ExtraDflag::Ignore ;                                  // ignore target implies ignore dep
	if ( ad.write==Yes && late                     ) ad.flags.extra_tflags |= ExtraTflag::Late   ;
	flags |= ad.flags ;
	//
	if (+di) {
		for( Access a : iota(All<Access>) ) if (_read[+a]<=pd) goto NotFirst ;
		dep_info = di ;
	NotFirst : ;
	}
	for( Access a : iota(All<Access>) )   if ( pd<_read[+a] && ad.accesses[a]                           ) _read[+a] = pd   ;
	/**/                                  if ( pd<_read_dir && ad.read_dir                              ) _read_dir = pd   ;
	if (late)                           { if ( pd<_write    && ad.write==Yes                            ) _write    = pd   ; }
	else                                { if (                 ad.write==Yes                            ) _washed   = true ; }
	/**/                                  if ( pd<_allow    && ad.flags.extra_tflags[ExtraTflag::Allow] ) _allow    = pd   ;
	/**/                                  if ( pd<_required && ad.flags.dflags[Dflag::Required]         ) _required = pd   ;
	/**/                                  if ( pd<_seen     && di.seen(ad.accesses)                     ) _seen     = pd   ;
	if (+pd) pd-- ;                                                                                                                 // ignore applies to simultaneous accesses
	/**/                                  if ( pd<_read_ignore  && ad.flags.extra_dflags[ExtraDflag::Ignore] ) _read_ignore  = pd ;
	/**/                                  if ( pd<_write_ignore && ad.flags.extra_tflags[ExtraTflag::Ignore] ) _write_ignore = pd ;
}

void Gather::AccessInfo::no_hot( PD pd ) {
	if (pd<_no_hot) _no_hot = pd ;
}

//
// Gather
//

::string& operator+=( ::string& os , Gather::JobSlaveEntry const& jse ) { // START_OF_NO_COV
	First first ;
	/**/                  os << "JobSlaveEntry("               ;
	if (+jse.pushed_deps) os <<first("",",")<< jse.pushed_deps ;
	if ( jse.buf_sz     ) os <<first("",",")<< jse.buf_sz      ;
	return                os <<')'                             ;
}                                                                         // END_OF_NO_COV

::string& operator+=( ::string& os , Gather const& gd ) { // START_OF_NO_COV
	/**/             os << "Gather(" << gd.accesses ;
	if (gd.seen_tmp) os <<",seen_tmp"               ;
	return           os << ')'                      ;
}                                                         // END_OF_NO_COV

void Gather::new_access( Fd fd , PD pd , ::string&& file , AccessDigest ad , DI const& di , Bool3 late , Comment c , CommentExts ces ) {
	SWEAR( +file , c , ces        ) ;
	SWEAR( +pd   , c , ces , file ) ;
	if (late==Maybe) SWEAR( ad.write==No ) ;                                                                          // when writing, we must know if job is started
	size_t                old_sz   = accesses.size()  ;
	::pair_s<AccessInfo>& file_info = _access_info(::move(file)) ;
	bool                  is_new   = accesses.size() > old_sz    ;
	::string       const& f        = file_info.first             ;
	AccessInfo&           info     = file_info.second            ;
	AccessInfo            old_info = info                        ;                                                    // for tracing only
	if (ad.write==Maybe) {
		// wait until file state can be safely inspected as in case of interrupted write, syscall may continue past end of process
		// this may be long, but is exceptionnal
		(pd+network_delay).sleep_until() ;
		if (info.dep_info.is_a<DepInfoKind::Crc>()) ad.write = No | (Crc    (f)!=info.dep_info.crc()) ;
		else                                        ad.write = No | (FileSig(f)!=info.dep_info.sig()) ;
	}
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info.update( pd , ad , late==Yes , di ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || info!=old_info ) {
		if (+c) _exec_trace( pd , c , ces , f ) ;
		Trace("new_access", fd , STR(is_new) , pd , ad , di , _parallel_id , c , ces , old_info , "->" , info , f ) ; // only trace if something changes
	}
}

void Gather::new_exec( PD pd , ::string const& exe , Comment c ) {
	RealPath              rp { autodep_env }                    ;
	RealPath::SolveReport sr = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(::move(sr)) )
		if (!Record::s_is_simple(f)) new_access( pd , ::move(f) , {.accesses=a} , FileInfo(f) , c ) ;
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

void Gather::_send_to_server( Fd fd , Jerr&& jerr , JobSlaveEntry&/*inout*/ jse=::ref(JobSlaveEntry()) ) {
	Trace trace("_send_to_server",fd,jerr) ;
	//
	if (!jerr.sync) fd = {} ;                                                                                     // dont reply if not sync
	JobMngtRpcReq jmrr   ;
	jmrr.seq_id = seq_id ;
	jmrr.job    = job    ;
	jmrr.fd     = fd     ;
	//
	switch (jerr.proc) {
		case Proc::ChkDeps :
			_exec_trace( jerr.date , jerr.comment , jerr.comment_exts ) ;
			jmrr.proc = JobMngtProc::ChkDeps ;
			reorder(false/*at_end*/)                              ;                                               // ensure server sees a coherent view
			chk_deps_cb( /*out*/jmrr.targets , /*out*/jmrr.deps ) ;
		break ;
		case Proc::DepDirect  :
		case Proc::DepVerbose : {
			bool verbose = jerr.proc==Proc::DepVerbose ;
			jmrr.proc = verbose ? JobMngtProc::DepVerbose : JobMngtProc::DepDirect ;
			jmrr.deps.reserve(jse.pushed_deps.size()) ;
			for( auto const& f : jse.pushed_deps )
				jmrr.deps.emplace_back( ::copy(f) , DepDigest(jerr.digest.accesses,Dflags(),true/*parallel*/) ) ; // no need for flags to ask info
		} break ;
		case Proc::Decode :
		case Proc::Encode : {
			SWEAR( jerr.sync==Yes , jerr ) ;
			if (jerr.proc==Proc::Encode) { jmrr.proc = JobMngtProc::Encode ; jmrr.min_len = jerr.min_len() ; jse.codec.name = Codec::mk_encode_node( jse.codec.file , jse.codec.ctx , jerr.file ) ; }
			else                         { jmrr.proc = JobMngtProc::Decode ;                                 jse.codec.name = Codec::mk_decode_node( jse.codec.file , jse.codec.ctx , jerr.file ) ; }
			jmrr.file      = ::move(jse.codec.file) ;
			jmrr.ctx       = ::move(jse.codec.ctx ) ;
			jmrr.txt       = ::move(jerr.file     ) ;
			jse.codec.file = {}                     ;
			jse.codec.ctx  = {}                     ;
		} break ;
	DF}                                                                                                           // NO_COV
	if (_send_to_server(jmrr)) { _n_server_req_pending++ ; trace("wait_server",_n_server_req_pending) ; }
	else                         sync(fd,{}) ;                                                                    // send an empty reply, job will invent something reasonable
}

void Gather::_ptrace_child( Fd report_fd , ::latch* ready ) {
	t_thread_key = 'P' ;
	AutodepPtrace::s_init(autodep_env) ;
	_child.pre_exec = AutodepPtrace::s_prepare_child  ;
	//vvvvvvvvvvvv
	_child.spawn() ;                                                            // /!\ although not mentioned in man ptrace, child must be launched by the tracing thread
	//^^^^^^^^^^^^
	ready->count_down() ;                                                       // signal main thread that _child.pid is available
	AutodepPtrace autodep_ptrace{_child.pid} ;
	wstatus = autodep_ptrace.process() ;
	ssize_t cnt = ::write(report_fd,&::ref(char()),1) ; SWEAR( cnt==1 , cnt ) ; // report child end
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
	_child.nice       = nice                                  ;
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

Status Gather::exec_child() {
	//
	using Event = Epoll<Kind>::Event ;
	Trace trace("exec_child",STR(as_session),method,autodep_env,cmd_line) ;
	//
	if (env) { trace("env",*env) ; swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ; }
	else                           swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	//
	ServerSockFd             job_master_fd  { New }       ;
	AcFd                     fast_report_fd ;                                                                   // always open, never waited for
	AcFd                     child_fd       ;
	Epoll<Kind>              epoll          { New }       ;
	Status                   status         = Status::New ;
	::umap<Fd,Jerr>          delayed_jerrs  ;                                                                   // events that analyze deps and targets are delayed until all accesses are processed ...
	size_t                   live_out_pos   = 0           ;                                                     // ... to ensure complete info
	::umap<Fd,IMsgBuf      > server_slaves  ;
	::umap<Fd,JobSlaveEntry> job_slaves     ;                                                                   // Jerr's waiting for confirmation
	bool                     panic_seen     = false       ;
	PD                       end_timeout    = PD::Future  ;
	PD                       end_child      = PD::Future  ;
	PD                       end_kill       = PD::Future  ;
	PD                       end_heartbeat  = PD::Future  ;                                                     // heartbeat to probe server when waiting for it
	bool                     timeout_fired  = false       ;
	size_t                   kill_step      = 0           ;
	//
	auto set_status = [&]( Status status_ , ::string const& msg_={} )->void {
		if (status==Status::New) status = status_ ;                                                             // only record first status
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
		fast_report_fd = AcFd( autodep_env.fast_report_pipe , true/*err_ok*/ , {.flags=O_RDONLY|O_NONBLOCK} ) ; // avoid blocking waiting for child, no impact on epoll-controled ops
		//
		if (+fast_report_fd) {                                                         // work w/o fast report if it does not work (seen on some instances of Centos7)
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
		if ( ::mkfifo( autodep_env.fast_report_pipe.c_str() , 0666 )!=0 ) SWEAR(errno=EEXIST,errno) ; // if it already exists, assume it is already a fifo
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
		if (now>=end_child) {
			_exec_trace(now,Comment::stillAlive) ;
			if (!_wait[Kind::ChildEnd]) {
				SWEAR( _wait[Kind::Stdout] || _wait[Kind::Stderr] , _wait , now , end_child ) ;       // else we should already have exited
				::string msg_ ;
				if ( _wait[Kind::Stdout]                        ) msg_ += "stdout " ;
				if ( _wait[Kind::Stdout] && _wait[Kind::Stderr] ) msg_ += "and "    ;
				if (                        _wait[Kind::Stderr] ) msg_ += "stderr " ;
				msg_ << "still open after job having been dead for " << network_delay.short_str() ;
				set_status(Status::Err,msg_) ;
			}
			else if ( kill_step && kill_step< kill_sigs.size() ) set_status(Status::Err,cat("still alive after having been killed ",kill_step       ," times"                      )) ;
			else if (              kill_step==kill_sigs.size() ) set_status(Status::Err,cat("still alive after having been killed ",kill_sigs.size()," times followed by a SIGKILL")) ;
			else if ( timeout_fired                            ) set_status(Status::Err,cat("still alive after having timed out and been killed with SIGKILL"                      )) ;
			else                                                 FAIL("dont know why still active") ;                                                                                   // NO_COV
			break ;                                                                                                                                                                     // exit loop
		}
		if (now>=end_kill) {
			kill(true/*next*/) ;
		}
		if ( now>=end_timeout && !timeout_fired ) {
			_exec_trace(now,Comment::timeout) ;
			set_status(Status::Err,"timeout after "+timeout.short_str()) ;
			kill() ;
			timeout_fired = true          ;
			end_timeout   = Pdate::Future ;
		}
		if (!kill_step) {
			if (end_heartbeat==Pdate::Future) { if ( _n_server_req_pending) end_heartbeat = now + HeartbeatTick ; }
			else                              { if (!_n_server_req_pending) end_heartbeat = Pdate::Future       ; }
			if (now>=end_heartbeat) {
				trace("server_heartbeat") ;
				JobMngtRpcReq jmrr ;
				jmrr.seq_id = seq_id                 ;
				jmrr.job    = job                    ;
				jmrr.proc   = JobMngtProc::Heartbeat ;
				if (_send_to_server(jmrr)) end_heartbeat += HeartbeatTick ;
				else                       kill() ;
			}
		}
		bool  must_wait = +epoll || +_wait ;
		Delay wait_for  ;
		if ( must_wait && !delayed_jerrs && !_wait[Kind::ChildStart] ) {
			Pdate event_date =                     end_child       ;
			/**/  event_date = ::min( event_date , end_kill      ) ;
			/**/  event_date = ::min( event_date , end_timeout   ) ;
			/**/  event_date = ::min( event_date , end_heartbeat ) ;
			wait_for = event_date<Pdate::Future ? event_date-now : Delay::Forever ;
		}
		::vector<Event> events = epoll.wait(wait_for) ;
		if (!events) {
			if (+delayed_jerrs) {                        // process delayed check deps after all other events
				for( auto& [fd,jerr] : delayed_jerrs ) {
					trace("delayed_jerr",fd,jerr) ;
					switch (jerr.proc) {
						case Proc::ChkDeps : _send_to_server( fd , ::move(jerr) ) ; break ;
						case Proc::List    : {
							CommentExts            ces     ;
							::vmap_s<TargetDigest> targets ;
							::vmap_s<DepDigest   > deps    ;
							JobExecRpcReply        reply   { .proc=Proc::List } ;
							chk_deps_cb( /*out*/targets , /*out*/deps ) ;
							if (jerr.digest.write!=No ) { ces |= CommentExt::Write ; for( auto& [f,d] : targets ) reply.files.push_back(::move(f)) ; }
							if (jerr.digest.write!=Yes) { ces |= CommentExt::Read  ; for( auto& [f,d] : deps    ) reply.files.push_back(::move(f)) ; }
							_exec_trace( jerr.date , Comment::list , ces ) ;
							sync( fd , ::move(reply) ) ;
						} break ;
					DF}
				}
				delayed_jerrs.clear() ;
			} else if (_wait[Kind::ChildStart]) {        // handle case where we are killed before starting : create child when we have processed waiting connections from server
				try {
					child_fd = _spawn_child() ;
				} catch(::string const& e) {
					trace("spawn_failed",e) ;
					if (child_stderr==Child::PipeFd) stderr = ensure_nl(e) ;
					else                             child_stderr.write(ensure_nl(e)) ;
					status = Status::EarlyErr ;
					break ;                              // cannot start, exit loop
				}
				if (+timeout) end_timeout = start_date + timeout ;
				_exec_trace( start_date , Comment::startJob ) ;
				trace("started","wait",_wait,+epoll) ;
				//
				if (child_stdout==Child::PipeFd) { epoll.add_read( _child.stdout , Kind::Stdout     ) ; _wait |= Kind::Stdout   ; trace("read_stdout    ",_child.stdout ,"wait",_wait,+epoll) ; }
				if (child_stderr==Child::PipeFd) { epoll.add_read( _child.stderr , Kind::Stderr     ) ; _wait |= Kind::Stderr   ; trace("read_stderr    ",_child.stderr ,"wait",_wait,+epoll) ; }
				if (+child_fd                  ) { epoll.add_read( child_fd      , Kind::ChildEndFd ) ; _wait |= Kind::ChildEnd ; trace("read_child     ",child_fd      ,"wait",_wait,+epoll) ; }
				else                             { epoll.add_pid ( _child.pid    , Kind::ChildEnd   ) ; _wait |= Kind::ChildEnd ; trace("read_child_proc",               "wait",_wait,+epoll) ; }
				/**/                               epoll.add_read( job_master_fd , Kind::JobMaster  ) ;                           trace("read_job_master",job_master_fd ,"wait",_wait,+epoll) ;
				_wait &= ~Kind::ChildStart ;
			} else if (!must_wait) {
				break ;                                  // we are done, exit loop
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
									size_t        len  = old_sz + pos - live_out_pos ;
									JobMngtRpcReq jmrr ;
									jmrr.seq_id = seq_id                          ;
									jmrr.job    = job                             ;
									jmrr.proc   = JobMngtProc::LiveOut            ;
									jmrr.txt    = stdout.substr(live_out_pos,len) ;
									//vvvvvvvvvvvvvvvvvvv
									_send_to_server(jmrr) ;
									//^^^^^^^^^^^^^^^^^^^
									trace("live_out",live_out_pos,len) ;
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
					if (kind==Kind::ChildEnd) { ::waitpid(_child.pid,&ws,0/*flags*/) ;                    wstatus = ws      ; } // wstatus is atomic, cant take its addresss as a int*
					else                      { int cnt=::read(fd,&::ref(char()),1) ; SWEAR(cnt==1,cnt) ; ws      = wstatus ; } // wstatus is already set, just flush fd
					trace(kind,fd,_child.pid,ws) ;
					SWEAR( !WIFSTOPPED(ws) , _child.pid ) ;                                               // child must have ended if we are here
					end_date  = New                      ;
					end_child = end_date + network_delay ;                                                // wait at most network_delay for reporting & stdout & stderr to settle down
					_exec_trace( end_date , Comment::endJob , {}/*CommentExt*/ , to_hex(uint16_t(ws)) ) ;
					if      (WIFEXITED  (ws)) set_status(             WEXITSTATUS(ws)!=0 ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(ws)) set_status( is_sig_sync(WTERMSIG   (ws))   ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                      FAIL("unexpected wstatus : ",ws) ;                                              // NO_COV defensive programming
					if (kind==Kind::ChildEnd) epoll.del_pid(_child.pid       ) ;
					else                      epoll.del    (false/*write*/,fd) ;
					_child.waited() ;                                                                                         // _child has been waited without calling _child.wait()
					_wait &= ~Kind::ChildEnd ;
					/**/                   epoll.dec() ;                                                                      // dont wait for new connections from job (but process those that come)
					if (+server_master_fd) epoll.dec() ;                                                                      // idem for connections from server
					trace("close",kind,status,"wait",_wait,+epoll) ;
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
							case JobMngtProc::DepDirect  :
							case JobMngtProc::DepVerbose : {
								_n_server_req_pending-- ; trace("resume_server",_n_server_req_pending) ;
								JobSlaveEntry& jse     = job_slaves.at(rfd)                 ;
								bool           verbose = jmrr.proc==JobMngtProc::DepVerbose ;
								Pdate          now     { New }                              ;
								//
								if (verbose)
									for( VerboseInfo const& vi : jmrr.verbose_infos )
										switch (vi.ok) {
											case Yes   : _exec_trace( now , Comment::depend , {CommentExt::Verbose,CommentExt::Reply} , ::string(vi.crc) ) ; break ;
											case Maybe : _exec_trace( now , Comment::depend , {CommentExt::Verbose,CommentExt::Reply} , "???"            ) ; break ;
											case No    : _exec_trace( now , Comment::depend , {CommentExt::Verbose,CommentExt::Reply} , "error"          ) ; break ;
										}
								else {
									NfsGuard nfs_guard { autodep_env.file_sync } ;
									for( ::string const& pd : jse.pushed_deps ) {
										nfs_guard.access(pd) ;
										_access_info(::copy(pd)).second.no_hot(now) ;                                         // dep has been built and we are guarded : it cannot be hot from now on
									}
									_exec_trace( now , Comment::depend , {CommentExt::Direct,CommentExt::Reply} ) ;
								}
								for( ::string const& pd : jse.pushed_deps )
									new_access( rfd , now , ::copy(pd) , jse.jerr.digest , FileInfo(pd) , Yes/*late*/ , jse.jerr.comment , jse.jerr.comment_exts ) ;
								jse.pushed_deps = {} ;
								jse.jerr        = {} ;
							} break ;
							case JobMngtProc::Heartbeat  :                                                                                                      break ;
							case JobMngtProc::Kill       : _exec_trace( New , Comment::kill       , CommentExt::Reply ) ; set_status(Status::Killed) ; kill() ; break ;
							case JobMngtProc::None       : _exec_trace( New , Comment::lostServer                     ) ; set_status(Status::Killed) ; kill() ; break ;
							case JobMngtProc::ChkDeps    :
							case JobMngtProc::ChkTargets : {
								bool        is_target = jmrr.proc==JobMngtProc::ChkTargets ;
								CommentExts ces       = CommentExt::Reply                  ;
								_n_server_req_pending-- ; trace("resume_server",_n_server_req_pending) ;
								switch (jmrr.ok) {
									case Maybe :
										ces |= CommentExt::Killed ;
										set_status( Status::ChkDeps , cat(is_target?"pre-existing target":"waiting dep"," : ",jmrr.txt) ) ;
										kill() ;
										rfd = {} ;                                                                            // dont reply to ensure job waits if sync
									break ;
									case No :
										ces |= CommentExt::Err ;
									break ;
								DN}
								_exec_trace( New , is_target?Comment::chkTargets:Comment::chkDeps , CommentExts(CommentExt::Reply) , jmrr.txt ) ;
							} break ;
							case JobMngtProc::Decode :
							case JobMngtProc::Encode : {
								SWEAR(+jmrr.fd) ;
								_n_server_req_pending-- ; trace("resume_server",_n_server_req_pending) ;
								auto           it  = job_slaves.find(jmrr.fd) ; SWEAR(it!=job_slaves.end(),jmrr)        ;
								JobSlaveEntry& jse = it->second                                                         ;
								Comment        c   = jmrr.proc==JobMngtProc::Encode ? Comment::encode : Comment::decode ;
								//
								_exec_trace( New , c , CommentExt::Reply , jmrr.txt ) ;
								new_access( rfd , PD(New) , ::move(jse.codec.name) , {.accesses=Access::Reg} , jmrr.crc , Yes/*late*/ , c ) ;
								jse.codec.name = {} ;
							} break ;
							case JobMngtProc::AddLiveOut : {
								trace("add_live_out",STR(live_out),live_out_pos) ;
								if (!live_out) {
									live_out     = true                 ;
									live_out_pos = stdout.rfind('\n')+1 ;
								}
								if (live_out_pos) {
									JobMngtRpcReq jmrr ;
									jmrr.seq_id = seq_id                        ;
									jmrr.job    = job                           ;
									jmrr.proc   = JobMngtProc::AddLiveOut       ;
									jmrr.txt    = stdout.substr(0,live_out_pos) ;
									//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
									OMsgBuf().send( ClientSockFd(service_mngt) , jmrr ) ;
									//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								}
							} break ;
						DF}                                                                                                   // NO_COV
						if (+rfd) {
							JobExecRpcReply jerr ;
							switch (jmrr.proc) {
								case JobMngtProc::None       :                                                                                                                        break ;
								case JobMngtProc::ChkDeps    : SWEAR(jmrr.ok!=Maybe) ; jerr = { .proc=Proc::ChkDeps    , .ok=jmrr.ok                                              } ; break ;
								case JobMngtProc::DepDirect  : SWEAR(jmrr.ok!=Maybe) ; jerr = { .proc=Proc::DepDirect  , .ok=jmrr.ok                                              } ; break ;
								case JobMngtProc::DepVerbose :                         jerr = { .proc=Proc::DepVerbose ,               .verbose_infos=::move(jmrr.verbose_infos ) } ; break ;
								case JobMngtProc::Decode     :                         jerr = { .proc=Proc::Decode     , .ok=jmrr.ok , .txt          =::move(jmrr.txt           ) } ; break ;
								case JobMngtProc::Encode     :                         jerr = { .proc=Proc::Encode     , .ok=jmrr.ok , .txt          =::move(jmrr.txt           ) } ; break ;
							DF}                                                                                                                                                               // NO_COV
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
					auto  sit = job_slaves.find(fd) ; SWEAR(sit!=job_slaves.end(),fd,job_slaves) ;
					auto& jse = sit->second         ;
					//
					ssize_t cnt = ::read( fd , jse.buf+jse.buf_sz , JobSlaveEntry::BufSz-jse.buf_sz ) ;
					if (cnt<=0) {
						SWEAR( jse.buf_sz==0 , jse.buf_sz ) ;            // ensure no partial message is left unprocessed
						if (fd==fast_report_fd) {
							epoll.del(false/*write*/,fd,false/*wait*/) ; // fast_report_fd is not waited as it is always open and will be closed as it is an AcFd
							open_fast_report_fd() ;                      // reopen as job may close the pipe and reopen it later
						} else {
							epoll.close(false/*write*/,fd) ;
						}
						trace("close",kind,fd,"wait",_wait,+epoll) ;
						for( auto& [_,um] : jse.to_confirm )
							for( Jerr& j : um )
								_new_access(fd,::move(j)) ;              // process deferred entries although with uncertain outcome
						job_slaves.erase(sit) ;
					} else {
						jse.buf_sz += cnt ;
						size_t pos = 0 ;
						for(;;) {
							{ if (pos+sizeof(MsgBuf::Len)   >jse.buf_sz) break ; } MsgBuf::Len sz = decode_int<MsgBuf::Len>(jse.buf+pos) ;  // read message size
							{ if (pos+sizeof(MsgBuf::Len)+sz>jse.buf_sz) break ; } pos += sizeof(MsgBuf::Len) ;                             // read message
							auto jerr = deserialize<Jerr>({ jse.buf+pos , sz }) ;
							pos += sz ;
							//
							Proc proc  = jerr.proc      ;                                                                                   // capture before jerr is ::move()'ed
							bool sync_ = jerr.sync==Yes ;                                                                                   // Maybe means not sync, only for transport
							if ( fd==fast_report_fd          ) SWEAR(!sync_) ;                                                              // cannot reply on fast_report_fd
							if ( proc!=Proc::Access || sync_ ) trace(kind,fd,proc,STR(sync_)) ;                                             // accesses are traced when processed
							switch (proc) {
								case Proc::DepPush    : jse.pushed_deps.emplace_back(::move(jerr.file)) ;  break ;
								case Proc::CodecFile  : jse.codec.file =             ::move(jerr.file)  ;  break ;
								case Proc::CodecCtx   : jse.codec.ctx  =             ::move(jerr.file)  ;  break ;
								case Proc::Guard      : _new_guard(fd,::move(jerr.file)) ;                 break ;
								case Proc::List       :
								case Proc::ChkDeps    : delayed_jerrs[fd] = ::move(jerr) ; sync_ = false ; break ;                          // if sync, reply is delayed as well
								case Proc::DepDirect  :
								case Proc::DepVerbose :
								case Proc::Decode     :
								case Proc::Encode     : {
									jse.jerr = ::move(jerr) ;
									_send_to_server( fd , ::move(jse.jerr) , jse ) ;
									sync_ = false ;                                                                                         // reply is delayed until server reply
								} break ;
								case Proc::Confirm : {
									trace("confirm",kind,fd,jerr.digest.write,jerr.id) ;
									Trace trace2 ;
									auto it = jse.to_confirm.find(jerr.id) ; SWEAR( it!=jse.to_confirm.end() , jerr.id , jse.to_confirm ) ;
									SWEAR(jerr.digest.write!=Maybe) ;                                                                       // ensure we confirm/infirm
									for ( Jerr& j : it->second ) {
										SWEAR(j.digest.write==Maybe) ;
										/**/                    j.digest.write  = jerr.digest.write ;
										if (!jerr.digest.write) j.comment_exts |= CommentExt::Err   ;
										_new_access(fd,::move(j)) ;
									}
									jse.to_confirm.erase(it) ;
								} break ;
								case Proc::None :
									if (fd==fast_report_fd) {
										epoll.del(false/*write*/,fd,false/*wait*/) ; // fast_report_fd is not waited as it is always open and will be closed as it is an AcFd
										open_fast_report_fd() ;                      // reopen as job may close the pipe and reopen it later
									} else {
										epoll.close(false/*write*/,fd) ;
									}
									trace("close",kind,fd,"wait",_wait,+epoll) ;
									for( auto& [_,um] : jse.to_confirm )
										for( Jerr& j : um )
											_new_access(fd,::move(j)) ;              // process deferred entries although with uncertain outcome
									job_slaves.erase(sit) ;
								break ;
								case Proc::Access :
									// for read accesses, trying is enough to trigger a dep, so confirm is useless
									if (jerr.digest.write==Maybe) { trace("maybe",jerr) ; jse.to_confirm[jerr.id].push_back(::move(jerr)) ; } // delay until confirmed/infirmed
									else                            _new_access(fd,::move(jerr)) ;
								break ;
								case Proc::AccessPattern :
									trace("access_pattern",kind,fd,jerr.date,jerr.digest,jerr.file) ;
									pattern_flags.emplace_back( jerr.file/*pattern*/ , ::pair(jerr.date,jerr.digest.flags) ) ;
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
								case Proc::Panic :                                                                                            // START_OF_NO_COV defensive programming
									if (!panic_seen) {                                                                                        // report only first panic
										_exec_trace( jerr.date , Comment::panic , {} , jerr.file ) ;
										set_status(Status::Err,jerr.file) ;
										kill() ;
										panic_seen = true ;
									}
								[[fallthrough]] ;                                                                                             // END_OF_NO_COV
								case Proc::Trace :                                                                                            // START_OF_NO_COV debug only
									_exec_trace( jerr.date , Comment::trace , {} , jerr.file ) ;
									trace(jerr.file) ;
								break ;                                                                                                       // END_OF_NO_COV
							DF}                                                                                                               // NO_COV
							if (sync_) sync( fd , {.proc=proc} ) ;
						}
						jse.buf_sz -= pos ;
						::memmove( jse.buf , jse.buf+pos , jse.buf_sz ) ;
					}
				} break ;
			DF}                                                                                                                               // NO_COV
		}
	}
	SWEAR(!_child) ;                                                                                                                          // _child must have been waited by now
	trace("done",status) ;
	SWEAR(status!=Status::New) ;
	reorder(true/*at_end*/) ;                                                                                                                 // ensure server sees a coherent view
	return status ;
}

// reorder accesses in chronological order and suppress implied dependencies :
// - when a file is depended upon, its uphill directories are implicitly depended upon under the following conditions, no need to keep them and this significantly decreases the number of deps
//   - either file exists
//   - or dir is only accessed as link
// - suppress dir when one of its sub-files appears before            (and condition above is satisfied)
// - suppress dir when one of its sub-files appears immediately after (and condition above is satisfied)
void Gather::reorder(bool at_end) {
	Trace trace("reorder") ;
	// update accesses to take pattern_flags into account
	if (+pattern_flags)                                                             // fast path : if no patterns, nothing to do
		for ( auto& [file,ai] : accesses ) {
			if (ai.flags.extra_dflags[ExtraDflag::NoStar]) continue ;
			for ( auto const& [re,date_flags] : pattern_flags )
				if (+re.match(file)) {
					trace("pattern_flags",file,date_flags) ;
					ai.update( date_flags.first , {.flags=date_flags.second} , date_flags.first<=start_date ) ;
				}
		}
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	::stable_sort(                                                                  // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b )->bool { return a.second.sort_key()<b.second.sort_key() ; }
	) ;
	// 1st pass (backward) : note dirs immediately preceding sub-files
	::vector<::vmap_s<AccessInfo>::reverse_iterator> lasts   ;                      // because of parallel deps, there may be several last deps
	Pdate                                            last_pd = Pdate::Future ;
	for( auto it=accesses.rbegin() ; it!=accesses.rend() ; it++ ) {
		{	AccessInfo&     ai       = it->second       ;
			Pdate           fw       = ai.first_write() ; if (fw<Pdate::Future              )                   goto NextDep ;
			/**/                                          if (ai.flags.dflags!=DflagsDfltDyn) { lasts.clear() ; goto NextDep ; }
			Accesses        accesses = ai.accesses()    ; if (!accesses                     )                   goto NextDep ;
			::string const& file     = it->first        ;
			for( auto last : lasts ) {
				if (!( last->first.starts_with(file) && last->first[file.size()]=='/' )) continue ;
				//
				if (last->second.dep_info.exists()==Yes) { trace("skip_from_next"  ,file) ; ai.clear_accesses() ;                     goto NextDep ; }
				else                                     { trace("no_lnk_from_next",file) ; ai.clear_lnk     () ; if (!ai.accesses()) goto NextDep ; }
			}
			if ( Pdate fr=ai.first_read() ; fr<last_pd ) {
				lasts.clear() ;                                                     // not a parallel dep => clear old ones that are no more last
				last_pd = fr ;
			}
			lasts.push_back(it) ;
		}
	NextDep : ;
	}
	// 2nd pass (forward) : suppress dirs of seen files and previously noted dirs
	::umap_s<bool/*sub-file exists*/> dirs  ;
	size_t                            i_dst = 0     ;
	bool                              cpy   = false ;
	for( auto& access : accesses ) {
		::string   const& file = access.first  ;
		AccessInfo      & ai   = access.second ;
		if ( ai.first_write()==Pdate::Future && ai.flags.dflags==DflagsDfltDyn && !ai.flags.tflags ) {
			auto it = dirs.find(file+'/') ;
			if (it!=dirs.end()) {
				if (it->second) { trace("skip_from_prev"  ,file) ; ai.clear_accesses() ; }
				else            { trace("no_lnk_from_prev",file) ; ai.clear_lnk     () ; }
			}
			if (ai.first_read()==PD::Future) {
				if (!at_end) access_map.erase(file) ;
				cpy = true ;
				continue ;
			}
		}
		bool exists = ai.dep_info.exists()==Yes ;
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
