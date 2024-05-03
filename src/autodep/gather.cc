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
	if (+ai.digest.accesses                       ) os <<",//"<< ai.parallel_id                                 ;
	if ( ai.seen!=Pdate::Future                   ) os <<",seen"                                                ;
	return                                          os <<')'                                                    ;
}

void Gather::AccessInfo::update( PD pd , AccessDigest ad , DI const& di , NodeIdx parallel_id_ ) {
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
		dep_info    = di           ;
		parallel_id = parallel_id_ ;
	NotFirst : ;
	}
	//
	for( Access a : All<Access> ) { PD& d=read[+a] ; if ( ad.accesses[a]                     && pd<d ) { d = pd ; digest.accesses.set(a,!dfi) ; } }
	/**/                          { PD& d=write    ; if ( ad.write==Yes                      && pd<d ) { d = pd ; digest.write = Yes &  !tfi  ; } }
	/**/                          { PD& d=target   ; if ( ad.extra_tflags[ExtraTflag::Allow] && pd<d )   d = pd ;                                 }
	/**/                          { PD& d=seen     ; if ( !dfi && di.seen(ad.accesses)       && pd<d )   d = pd ;                                 }
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

void Gather::_new_access( Fd fd , PD pd , ::string&& file , AccessDigest ad , DI const& di , bool parallel , ::string const& comment ) {
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
	if (!parallel) _parallel_id++ ;
	AccessInfo old_info = *info ;                                                                                                                            // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update( pd , ad , di , _parallel_id ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || *info!=old_info ) Trace("_new_access", fd , STR(is_new) , pd , ad , di , _parallel_id , comment , old_info , "->" , *info , it->first ) ; // only trace if something changes
}

void Gather::new_deps( PD pd , ::vmap_s<DepDigest>&& deps , ::string const& stdin ) {
	bool parallel = false ;
	for( auto& [f,dd] : deps ) {
		bool is_stdin = f==stdin ;
		if (is_stdin) {                             // stdin is read
			if (!dd.accesses) dd.sig(FileInfo(f)) ; // record now if not previously accessed
			dd.accesses |= Access::Reg ;
		}
		_new_access( pd , ::move(f) , {.accesses=dd.accesses,.dflags=dd.dflags} , dd , parallel , is_stdin?"stdin"s:"s_deps"s ) ;
		parallel = true ;
	}
}

void Gather::new_exec( PD pd , ::string const& exe , ::string const& c ) {
	RealPath              rp       { autodep_env }                    ;
	RealPath::SolveReport sr       = rp.solve(exe,false/*no_follow*/) ;
	bool                  parallel = false                            ;
	for( auto&& [f,a] : rp.exec(sr) ) {
		_new_access( pd , ::move(f) , {.accesses=a} , FileInfo(f) , parallel , c ) ;
		parallel = true ;
	}
}

void _child_wait_thread_func( int* wstatus , Pdate* end_time , pid_t pid , Fd fd ) {
	static constexpr uint64_t One = 1 ;
	do { ::waitpid(pid,wstatus,0) ; } while (WIFSTOPPED(*wstatus)) ;
	*end_time = New ;
	swear_prod(::write(fd,&One,8)==8,"cannot report child wstatus",wstatus) ;
}

void Gather::_kill( KillStep kill_step , Child const& child ) {
	Trace trace("kill",kill_step,STR(as_session),child.pid) ;
	SWEAR(kill_step>=KillStep::Kill) ;
	uint8_t kill_idx = kill_step-KillStep::Kill   ;
	bool    last     = kill_idx>=kill_sigs.size() ;
	if (!_kill_reported) {
		if (!_wait[Kind::ChildEnd] ) {
			trace("no_child",_wait) ;
			const char* pfx = "killed while waiting" ;
			if (_wait[Kind::Stdout]) { append_to_string( msg , pfx , " stdout" ) ; pfx = " and" ; }
			if (_wait[Kind::Stderr])   append_to_string( msg , pfx , " stderr" ) ;
			msg.push_back('\n') ;
		}
		_kill_reported = true ;
	}
	if (_wait[Kind::ChildEnd]) {
		int sig = kill_sigs[kill_idx] ;
		trace("kill",sig) ;
		//                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if ( sig && child.pid>1 ) kill_process(child.pid,sig,as_session/*as_group*/) ;
		//                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	if (last) {
		pid_t              ctl_pid = as_session ? ::getpgrp() : ::getpid() ;
		::umap_s<    Kind> fd_strs ;
		::umap<pid_t,Kind> to_kill ;
		if (_wait[Kind::Stdout]) fd_strs[ read_lnk(to_string("/proc/self/fd/",child.stdout.fd)) ] = Kind::Stdout ;
		if (_wait[Kind::Stderr]) fd_strs[ read_lnk(to_string("/proc/self/fd/",child.stderr.fd)) ] = Kind::Stderr ;
		for( ::string const& proc_entry : lst_dir("/proc")  ) {
			for( char c : proc_entry ) if (c>'9'||c<'0') goto NextProc ;
			try {
				pid_t child_pid = from_string<pid_t>(proc_entry) ;
				if (as_session        ) child_pid = ::getpgid(child_pid) ;
				if (child_pid==ctl_pid) goto NextProc ;
				if (child_pid<=1      ) goto NextProc ;                                           // no pgid available, ignore
				for( ::string const& fd_entry : lst_dir(to_string("/proc/",proc_entry,"/fd")) ) {
					::string fd_str = read_lnk(to_string("/proc/",proc_entry,"/fd/",fd_entry)) ;
					if (!fd_str                  ) continue ;                                     // fd has disappeared, ignore
					auto it = fd_strs.find(fd_str) ;
					if (it==fd_strs.end()        ) continue ;
					to_kill[child_pid] = it->second ;
					break ;
				}
			} catch(::string const&) {}                                                           // if we cannot read /proc/pid, process is dead, ignore
		NextProc : ;
		}
		trace("last_kill",ctl_pid,child.stdout,child.stderr,fd_strs,to_kill) ;
		//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( auto [p,_] : to_kill ) kill_process(p,SIGKILL,as_session/*as_group*/) ;
		//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	trace("done") ;
}

void Gather::_solve( Fd fd , JobExecRpcReq& jerr ) {
		SWEAR( jerr.proc>=Proc::HasFiles && jerr.solve ) ;
		::vmap_s<FileInfo> files     ;
		RealPath           real_path { autodep_env , ::move(jerr.cwd) } ;             // solve is set to false, cwd is not used anymore
		bool               read      = +jerr.digest.accesses            ;
		for( auto const& [f,fi] : jerr.files ) {
			SWEAR(+f) ;
			RealPath::SolveReport sr = real_path.solve(f,jerr.no_follow) ;
			// cf Record::_solve for explanation                                                                                        parallel
			for( ::string& lnk : sr.lnks )    _new_access( fd , jerr.date , ::move  (lnk    ) , {.accesses=Access::Lnk} , FileInfo(lnk) , false , "solve.lnk" ) ;
			if      (read                   ) {}
			else if (sr.file_accessed==Yes  ) _new_access( fd , jerr.date , ::copy  (sr.real) , {.accesses=Access::Lnk} , FileInfo()    , false , "solve.lst" ) ;
			else if (sr.file_accessed==Maybe) _new_access( fd , jerr.date , dir_name(sr.real) , {.accesses=Access::Lnk} , FileInfo()    , false , "solve.lst" ) ;
			//
			seen_tmp |= sr.file_loc==FileLoc::Tmp && jerr.digest.write!=No && !read ; // if reading before writing, then we cannot populate tmp
			if (sr.file_loc>FileLoc::Repo) jerr.digest.write = No ;
			if (sr.file_loc>FileLoc::Dep ) continue ;
			if (+fi                      ) files.emplace_back( sr.real , fi                ) ;
			else                           files.emplace_back( sr.real , FileInfo(sr.real) ) ;
		}
		jerr.files = ::move(files) ;
		jerr.solve = false         ;                                                  // files are now real and dated
}

void Gather::_send_to_server ( Fd fd , Jerr&& jerr ) {
	Trace trace("_send_to_server",fd,jerr) ;
	//
	Proc   proc = jerr.proc         ;                                         // capture essential info before moving to server_cb
	size_t sz   = jerr.files.size() ;                                         // .
	switch (proc) {
		case Proc::ChkDeps    : reorder(false/*at_end*/) ;       break ;      // ensure server sees a coherent view
		case Proc::DepVerbose : _new_accesses(fd,::copy(jerr)) ; break ;
		//
		case Proc::Decode : SWEAR( jerr.sync && jerr.files.size()==1 ) ; _codec_files[fd] = Codec::mk_decode_node( jerr.files[0].first , jerr.ctx , jerr.txt ) ; break ;
		case Proc::Encode : SWEAR( jerr.sync && jerr.files.size()==1 ) ; _codec_files[fd] = Codec::mk_encode_node( jerr.files[0].first , jerr.ctx , jerr.txt ) ; break ;
		default : ;
	}
	if (!jerr.sync) fd = {} ;                                                 // dont reply if not sync
	try {
		JobMngtRpcReq jmrr ;
		if (jerr.proc==JobExecProc::ChkDeps) jmrr = { JobMngtProc::ChkDeps , seq_id , job , fd , cur_deps_cb() } ;
		else                                 jmrr = {                        seq_id , job , fd , ::move(jerr)  } ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send( ClientSockFd(service_mngt) , jmrr ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} catch (...) {
		trace("no_server") ;
		JobExecRpcReply sync_reply ;
		sync_reply.proc      = proc                                         ;
		sync_reply.ok        = Yes                                          ; // try to mimic server as much as possible when none is available
		sync_reply.dep_infos = ::vector<pair<Bool3/*ok*/,Crc>>(sz,{Yes,{}}) ; // .
		sync(fd,::move(sync_reply)) ;
	}
}

void Gather::_spawn_child( Child& child , ::vector_s const& args , Fd cstdin , Fd cstdout , Fd cstderr ) {
	Trace trace("_spawn_child",args,cstdin,cstdout,cstderr) ;
	//
	::map_ss add_env { {"LMAKE_AUTODEP_ENV",autodep_env} } ;               // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	if (method==AutodepMethod::Ptrace) {                                   // PER_AUTODEP_METHOD : handle case
		// we split the responsability into 2 processes :
		// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
		// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
		bool in_parent = child.spawn( as_session , {} , cstdin , cstdout , cstderr ) ;
		if (in_parent) {
			start_time = New ;                                             // record job start time as late as possible
		} else {
			Child grand_child ;
			AutodepPtrace::s_autodep_env = new AutodepEnv{autodep_env} ;
			try {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				grand_child.spawn(
					false/*as_group*/ , args                               // first level child has created the group
				,	Fd::Stdin , Fd::Stdout , Fd::Stderr
				,	env , &add_env
				,	chroot
				,	cwd
				,	AutodepPtrace::s_prepare_child
				) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} catch(::string const& e) {
				exit(Rc::System,e) ;
			}
			trace("grand_child_pid",grand_child.pid) ;
			AutodepPtrace autodep_ptrace { grand_child.pid }        ;
			int           wstatus        = autodep_ptrace.process() ;
			grand_child.waited() ;                                         // grand_child has already been waited
			if      (WIFEXITED  (wstatus)) ::_exit(WEXITSTATUS(wstatus)) ;
			else if (WIFSIGNALED(wstatus)) ::_exit(+Rc::System         ) ;
			fail_prod("ptraced child did not exit and was not signaled : wstatus : ",wstatus) ;
		}
	} else {
		if (method>=AutodepMethod::Ld) {                                                                                                                  // PER_AUTODEP_METHOD : handle case
			::string env_var ;
			//
			switch (method) {                                                                                                                             // PER_AUTODEP_METHOD : handle case
				case AutodepMethod::LdAudit           : env_var = "LD_AUDIT"   ; add_env[env_var] = *g_lmake_dir+"/_lib/ld_audit.so"            ; break ;
				case AutodepMethod::LdPreload         : env_var = "LD_PRELOAD" ; add_env[env_var] = *g_lmake_dir+"/_lib/ld_preload.so"          ; break ;
				case AutodepMethod::LdPreloadJemalloc : env_var = "LD_PRELOAD" ; add_env[env_var] = *g_lmake_dir+"/_lib/ld_preload_jemalloc.so" ; break ;
			DF}
			if (env) { if (env->contains(env_var)) add_env[env_var] += ':' + env->at(env_var) ; }
			else     { if (has_env      (env_var)) add_env[env_var] += ':' + get_env(env_var) ; }
		}
		new_exec( New , args[0] ) ;
		start_time = New ;                                                                                                                                // record job start time as late as possible
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
		child.spawn(
			as_session , args
		,	cstdin , cstdout , cstderr
		,	env , &add_env
		,	chroot
		,	cwd
		) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	trace("child_pid",child.pid) ;
}
Status Gather::exec_child( ::vector_s const& args , Fd cstdin , Fd cstdout , Fd cstderr ) {
	Trace trace("exec_child",STR(as_session),method,autodep_env,args) ;
	if (env) trace("env",*env) ;
	ServerSockFd job_master_fd { New } ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service = job_master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	Child                                 child              ;
	AutoCloseFd                           child_fd           ;
	::jthread                             wait_jt            ;                             // thread dedicated to wating child
	Epoll                                 epoll              { New }       ;
	Status                                status             = Status::New ;
	::umap<Fd,Jerr>                       delayed_check_deps ;                             // check_deps events are delayed to ensure all previous deps are received
	KillStep                              kill_step          = {}          ;
	Pdate                                 event_date         ;
	size_t                                live_out_pos       = 0           ;
	::umap<Fd,pair<IMsgBuf,vector<Jerr>>> slaves             ;                             // Jerr's are waiting for confirmation
	//
	auto set_status = [&]( Status status_ , ::string const& msg_={} )->void {
		if ( status==Status::New || status==Status::Ok ) status = status_ ;                // else there is already another reason
		if ( +msg_                                     ) append_line_to_string(msg,msg_) ;
	} ;
	auto done = [&](Kind k)->void {
		SWEAR(_wait[k]) ;
		_wait &= ~k ;
		if (!_wait) {                                                                      // if job is dead for good
			event_date = Pdate(New)+network_delay ;                                        // wait at most network_delay for reporting to settle down
			/**/                   epoll.cnt-- ;                                           // dont wait for new connections from job (but process those that come)
			if (+server_master_fd) epoll.cnt-- ;                                           // idem for connections from server
		}
	} ;
	//
	if (+timeout) {
		event_date = Pdate(New) + timeout ;
		trace("set_timeout",timeout,event_date) ;
	}
	if (+server_master_fd) {
		epoll.add_read(server_master_fd,Kind::ServerMaster) ;
		trace("read_server_master",server_master_fd) ;
	}
	_wait = Kind::ChildStart ;
	while ( epoll.cnt || _wait[Kind::ChildStart] ) {
		Delay wait_for = Delay::Forever ;
		if ( kill_step==KillStep::Kill || +event_date ) {
			Pdate now { New } ;
			if ( kill_step==KillStep::Kill || now>=event_date ) {
				switch (kill_step) {
					case KillStep::Report :
						goto Return ;
					case KillStep::None :
						trace("fire_timeout") ;
						if (+_wait) set_status(Status::Err,to_string("timout after "                     ,timeout      .short_str())) ;
						else        set_status(Status::Err,to_string("still active after being dead for ",network_delay.short_str())) ;
						kill_step = KillStep::Kill ;
					[[fallthrough]] ;
					default :
						SWEAR(kill_step>=KillStep::Kill) ;
						if (_wait[Kind::ChildStart]) goto Return ;                                                                                                    // killed before job start
						_kill(kill_step,child) ;
						if (!event_date) event_date = now ;
						if (uint8_t(kill_step-KillStep::Kill)==kill_sigs.size()) { kill_step = KillStep::Report ;                     event_date += network_delay ; }
						else                                                     { kill_step++                  ; SWEAR(+kill_step) ; event_date += Delay(1)      ; } // ensure no wrap around
				}
			}
			wait_for = event_date-now ;
		}
		if ( +delayed_check_deps || _wait[Kind::ChildStart] ) wait_for = {} ;
		::vector<Epoll::Event> events = epoll.wait(wait_for) ;
		if (!events) {
			if (+delayed_check_deps) {      // process delayed check deps after all other events
				trace("delayed_chk_deps") ;
				for( auto& [fd,jerr] : delayed_check_deps ) _send_to_server(fd,::move(jerr)) ;
				delayed_check_deps.clear() ;
				continue ;
			}
			if (_wait[Kind::ChildStart]) {  // handle case where we are killed before starting : create child when we have processed waiting connections from server
				try {
					_spawn_child( child , args , cstdin , cstdout , cstderr ) ;
					_wait &= ~Kind::ChildStart ;
				} catch(::string const& e) {
					if (cstderr==Child::Pipe) stderr = e ;
					else                      cstderr.write(e) ;
					status = Status::EarlyErr ;
					goto Return ;
				}
				child_fd = ::eventfd(0,EFD_CLOEXEC)                                                               ;
				wait_jt  = ::jthread( _child_wait_thread_func , &wstatus , &end_time , child.pid , Fd(child_fd) ) ;                                             // thread dedicated to wating child
				if (cstdout==Child::Pipe) { epoll.add_read(child.stdout ,Kind::Stdout   ) ; _wait |= Kind::Stdout   ; trace("read_stdout"    ,child.stdout) ; }
				if (cstderr==Child::Pipe) { epoll.add_read(child.stderr ,Kind::Stderr   ) ; _wait |= Kind::Stderr   ; trace("read_stderr"    ,child.stderr) ; }
				/**/                        epoll.add_read(child_fd     ,Kind::ChildEnd ) ; _wait |= Kind::ChildEnd ; trace("read_child "    ,child_fd     ) ;
				/**/                        epoll.add_read(job_master_fd,Kind::JobMaster) ;                           trace("read_job_master",job_master_fd) ;
			}
		}
		for( Epoll::Event const& event : events ) {
			Kind kind = event.data<Kind>() ;
			Fd   fd   = event.fd()         ;
			if (kind!=Kind::JobSlave) trace(kind,fd,epoll.cnt) ;
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
						epoll.close(fd) ;
						trace("close",kind,fd) ;
						done(kind) ;
					}
				} break ;
				case Kind::ChildEnd : {
					uint64_t one = 0/*garbage*/            ;
					int      cnt = ::read( fd , &one , 8 ) ; SWEAR( cnt==8 && one==1 , cnt , one ) ;
					if      (WIFEXITED  (wstatus)) set_status( WEXITSTATUS(wstatus)!=0        ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(wstatus)) set_status( is_sig_sync(WTERMSIG(wstatus)) ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                           fail("unexpected wstatus : ",wstatus) ;
					epoll.close(fd) ;
					done(kind)      ;
					trace("close",kind,status,::hex,wstatus,::dec) ;
				} break ;
				case Kind::JobMaster    :
				case Kind::ServerMaster : {
					bool is_job = kind==Kind::JobMaster ;
					SWEAR( fd==(is_job?job_master_fd:server_master_fd) , fd , is_job , job_master_fd , server_master_fd ) ;
					Fd slave = (kind==Kind::JobMaster?job_master_fd:server_master_fd).accept() ;
					epoll.add_read(slave,is_job?Kind::JobSlave:Kind::ServerSlave) ;
					trace("read_slave",STR(is_job),slave) ;
					slaves[slave] ;                                                                                                // allocate entry
				} break ;
				case Kind::ServerSlave : {
					JobMngtRpcReply jmrr ;
					auto it           = slaves.find(fd) ;
					auto& slave_entry = it->second      ;
					try         { if (!slave_entry.first.receive_step(fd,jmrr)) continue ; }
					catch (...) { trace("no_jmrr",jmrr) ; jmrr.proc = {} ;                 }                                       // fd was closed, ensure no partially received jmrr
					trace(kind,jmrr) ;
					Fd rfd = jmrr.fd ;                                                                                             // capture before move
					if (jmrr.seq_id==seq_id) {
						switch (jmrr.proc) {
							case JobMngtProc::DepVerbose :
							case JobMngtProc::Heartbeat  :                                                                                               break ;
							case JobMngtProc::Kill       :
							case JobMngtProc::None       :                       set_status(Status::Killed ) ; kill_step = KillStep::Kill ;              break ; // server died
							case JobMngtProc::ChkDeps    : if (jmrr.ok==Maybe) { set_status(Status::ChkDeps) ; kill_step = KillStep::Kill ; rfd = {} ; } break ;
							case JobMngtProc::Decode :
							case JobMngtProc::Encode : {
								SWEAR(+jmrr.fd) ;
								auto it = _codec_files.find(jmrr.fd) ;
								_new_access( rfd , PD(New) , ::move(it->second) , {.accesses=Access::Reg} , jmrr.crc , false/*parallel*/ , ::string(snake(jmrr.proc)) ) ;
								_codec_files.erase(it) ;
							} break ;
						DF}
						//        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						if (+rfd) sync( rfd , JobExecRpcReply(::move(jmrr)) ) ;
						//        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					}
					epoll.close(fd) ;
					trace("close",kind,fd) ;
				} break ;
				case Kind::JobSlave : {
					Jerr jerr         ;
					auto it           = slaves.find(fd) ;
					auto& slave_entry = it->second      ;
					try         { if (!slave_entry.first.receive_step(fd,jerr)) continue ; }
					catch (...) { trace("no_jerr",jerr) ; jerr.proc = Proc::None ;         }                                      // fd was closed, ensure no partially received jerr
					Proc proc  = jerr.proc ;                                                                                      // capture essential info so as to be able to move jerr
					bool sync_ = jerr.sync ;                                                                                      // .
					if ( proc!=Proc::Access                 ) trace(kind,fd,epoll.cnt,proc) ;                                     // there may be too many Access'es, only trace within _new_accesses
					if ( proc>=Proc::HasFiles && jerr.solve ) _solve(fd,jerr)               ;
					switch (proc) {
						case Proc::Confirm :
							for( Jerr& j : slave_entry.second ) { j.digest.write = jerr.digest.write ; _new_accesses(fd,::move(j)) ; }
							slave_entry.second.clear() ;
						break ;
						case Proc::None :
							epoll.close(fd) ;
							trace("close",kind,fd) ;
							for( Jerr& j : slave_entry.second ) _new_accesses(fd,::move(j)) ;                                     // process deferred entries although with uncertain outcome
							slaves.erase(it) ;
						break ;
						case Proc::Access   :
							// for read accesses, trying is enough to trigger a dep, so confirm is useless
							if ( jerr.digest.write==Maybe ) slave_entry.second.push_back(::move(jerr)) ;                          // defer until confirm resolution
							else                            _new_accesses(fd,::move(jerr))             ;
						break ;
						case Proc::Tmp        : seen_tmp = true ;                                               break           ;
						case Proc::Guard      : _new_guards(fd,::move(jerr)) ;                                  break           ;
						case Proc::DepVerbose :
						case Proc::Decode     :
						case Proc::Encode     : _send_to_server(fd,::move(jerr)) ;                              goto NoReply    ;
						case Proc::ChkDeps    : delayed_check_deps[fd] = ::move(jerr) ;                         goto NoReply    ; // if sync, reply is delayed as well
						case Proc::Panic      : set_status(Status::Err,jerr.txt) ; kill_step = KillStep::Kill ; [[fallthrough]] ;
						case Proc::Trace      : trace(jerr.txt) ;                                               break           ;
					DF}
					if (sync_) sync( fd , JobExecRpcReply(proc) ) ;
				NoReply : ;
				} break ;
			DF}
		}
	}
Return :
	child.waited() ;
	trace("done",status) ;
	SWEAR(status!=Status::New) ;
	reorder(true/*at_end*/) ;                                                                                                     // ensure server sees a coherent view
	return status ;
}

// reorder accesses in chronological order and suppress implied dependencies :
// - when a file is depended upon, its uphill directories are implicitely depended upon, no need to keep them and this significantly decreases the number of deps
// - suppress dir when one of its sub-files appear before
// - suppress dir when one of its sub-files appear immediately after
void Gather::reorder(bool at_end) {
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	Trace trace("reorder") ;
	::stable_sort(                                                                                          // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b ) -> bool {
			return ::pair(a.second.first_read().first,a.second.parallel_id) < ::pair(b.second.first_read().first,b.second.parallel_id) ;
		}
	) ;
	// first pass (backward) : note dirs of immediately following files
	::string const* last = nullptr ;
	for( auto it=accesses.rbegin() ; it!=accesses.rend() ; it++ ) {                                         // XXX : manage parallel deps
		::string const& file   = it->first         ;
		::AccessDigest& digest = it->second.digest ;
		if (
			last
		&&	( digest.write==No        && !digest.dflags            )
		&&	( last->starts_with(file) && (*last)[file.size()]=='/' )
		)    digest.accesses = {}    ;                                                                      // keep original last which is better
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
		for( ::string dir=dir_name(file) ; +dir ; dir=dir_name(dir) ) if (!dirs.insert(dir).second) break ; // all uphill dirs are already inserted if a dir has been inserted
		if (cpy) accesses[i_dst] = ::move(access) ;
		i_dst++ ;
	}
	accesses.resize(i_dst) ;
	for( NodeIdx i=0 ; i<accesses.size() ; i++ ) access_map.at(accesses[i].first) = i ;                     // always recompute access_map as accesses has been sorted
}
