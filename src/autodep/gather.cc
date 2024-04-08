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
	if (+ai.crc_date                              ) os << ai.crc_date                                     <<',' ;
	/**/                                            os << ai.digest                                             ;
	if (+ai.digest.accesses                       ) os <<",//"<< ai.parallel_id                                 ;
	if ( ai.seen!=Pdate::Future                   ) os <<",seen"                                                ;
	return                                          os <<')'                                                    ;
}

void Gather::AccessInfo::update( PD pd , AccessDigest ad , CD const& cd , NodeIdx parallel_id_ ) {
	digest.tflags       |= ad.tflags       ;
	digest.extra_tflags |= ad.extra_tflags ;
	digest.dflags       |= ad.dflags       ;
	digest.extra_dflags |= ad.extra_dflags ;
	//
	bool ti =       ad.extra_tflags[ExtraTflag::Ignore] ;
	bool di = ti || ad.extra_dflags[ExtraDflag::Ignore] ; // ti also prevents reads from being visible
	//
	if (!di) {
		for( Access a : All<Access> ) if (read[+a]<=pd) goto NotFirst ;
		crc_date    = cd           ;
		parallel_id = parallel_id_ ;
	NotFirst : ;
	}
	//
	for( Access a : All<Access> ) { PD& d=read[+a] ; if ( ad.accesses[a]                     && pd<d ) { d = pd ; digest.accesses.set(a,!di) ; } }
	/**/                          { PD& d=write    ; if ( ad.write==Yes                      && pd<d ) { d = pd ; digest.write = Yes &  !ti  ; } }
	/**/                          { PD& d=target   ; if ( ad.extra_tflags[ExtraTflag::Allow] && pd<d )   d = pd ;                                }
	/**/                          { PD& d=seen     ; if ( !di && cd.seen(ad.accesses)        && pd<d )   d = pd ;                                }
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

void Gather::_new_access( Fd fd , PD pd , ::string&& file , AccessDigest ad , CD const& cd , bool parallel , ::string const& comment ) {
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
	if (!parallel) parallel_id++ ;
	AccessInfo old_info = *info ;                                                                                                                           // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update( pd , ad , cd , parallel_id ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || *info!=old_info ) Trace("_new_access", fd , STR(is_new) , pd , ad , cd , parallel_id , comment , old_info , "->" , *info , it->first ) ; // only trace if something changes
}

void Gather::new_deps( PD pd , ::vmap_s<DepDigest>&& deps , ::string const& stdin ) {
	bool parallel = false ;
	for( auto& [f,dd] : deps ) {
		bool is_stdin = f==stdin ;
		if (is_stdin) {                               // stdin is read
			if (!dd.accesses) dd.date(file_date(f)) ; // record now if not previously accessed
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
		_new_access( pd , ::move(f) , {.accesses=a} , file_date(f) , parallel , c ) ;
		parallel = true ;
	}
}

ENUM( GatherKind , Stdout , Stderr , ServerReply , ChildEnd , Master , Slave )

void _child_wait_thread_func( int* wstatus , pid_t pid , Fd fd ) {
	static constexpr uint64_t One = 1 ;
	do { ::waitpid(pid,wstatus,0) ; } while (WIFSTOPPED(*wstatus)) ;
	swear_prod(::write(fd,&One,8)==8,"cannot report child wstatus",wstatus) ;
}

bool/*done*/ Gather::kill(int sig) {
	Trace trace("kill",sig,pid,STR(as_session),child_stdout,child_stderr) ;
	Lock lock { _pid_mutex }           ;
	int       kill_sig  = sig>=0 ? sig : SIGKILL ;
	bool      killed_   = false                  ;
	killed = true ;                                                                               // prevent child from starting if killed before
	//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	if (pid>1) killed_ = kill_process(pid,kill_sig,as_session/*as_group*/) ;
	//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (sig<0) {                                                                                  // kill all processes (or process groups) connected to a stream we wait for
		pid_t                    ctl_pid = as_session ? ::getpgrp() : ::getpid() ;
		::umap_s<    GatherKind> fd_strs ;
		::umap<pid_t,GatherKind> to_kill ;
		trace("ctl",ctl_pid,mk_key_uset(slaves)) ;
		if (+child_stdout) fd_strs[ read_lnk(to_string("/proc/self/fd/",child_stdout.fd)) ] = GatherKind::Stdout ;
		if (+child_stderr) fd_strs[ read_lnk(to_string("/proc/self/fd/",child_stderr.fd)) ] = GatherKind::Stderr ;
		trace("fds",fd_strs) ;
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
		trace("to_kill",to_kill) ;
		//                                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( auto [p,_] : to_kill ) killed_ |= kill_process(p,kill_sig,as_session/*as_group*/) ;
		//                                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	trace("done",STR(killed_)) ;
	return killed_ ;
}

void Gather::_solve( Fd fd , JobExecRpcReq& jerr ) {
		SWEAR( jerr.proc>=Proc::HasFiles && jerr.solve ) ;
		::vmap_s<Ddate> files     ;
		RealPath        real_path { autodep_env , ::move(jerr.cwd) } ;                // solve is set to false, cwd is not used anymore
		bool            read      = +jerr.digest.accesses            ;
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR(+f) ;
			RealPath::SolveReport sr = real_path.solve(f,jerr.no_follow) ;
			// cf Record::_solve for explanation                                                                                         parallel
			for( ::string& lnk : sr.lnks )    _new_access( fd , jerr.date , ::move  (lnk    ) , {.accesses=Access::Lnk} , file_date(lnk) , false , "solve.lnk" ) ;
			if      (read                   ) {}
			else if (sr.file_accessed==Yes  ) _new_access( fd , jerr.date , ::copy  (sr.real) , {.accesses=Access::Lnk} , Ddate()        , false , "solve.lst" ) ;
			else if (sr.file_accessed==Maybe) _new_access( fd , jerr.date , dir_name(sr.real) , {.accesses=Access::Lnk} , Ddate()        , false , "solve.lst" ) ;
			//
			seen_tmp |= sr.file_loc==FileLoc::Tmp && jerr.digest.write!=No && !read ; // if reading before writing, then we cannot populate tmp
			if (sr.file_loc>FileLoc::Repo) jerr.digest.write = No ;
			if (sr.file_loc>FileLoc::Dep ) continue ;
			if (+dd                      ) files.emplace_back( sr.real , dd                 ) ;
			else                           files.emplace_back( sr.real , file_date(sr.real) ) ;
		}
		jerr.files = ::move(files) ;
		jerr.solve = false         ;                                                  // files are now real and dated
}

Status Gather::exec_child( ::vector_s const& args , Fd cstdin , Fd cstdout , Fd cstderr ) {
	using Kind = GatherKind ;
	Trace trace("exec_child",STR(as_session),method,autodep_env,args) ;
	if (env) trace("env",*env) ;
	Child child ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service = master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	::map_ss add_env {{"LMAKE_AUTODEP_ENV",autodep_env}} ;                     // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	{	Lock lock{_pid_mutex} ;
		if (killed                       ) return Status::Killed ;             // dont start if we are already killed before starting
		if (method==AutodepMethod::Ptrace) {                                   // PER_AUTODEP_METHOD : handle case
			// XXX : splitting responsibility is no more necessary. Can directly report child termination from within autodep_ptrace.process using same ifce as _child_wait_thread_func
			// we split the responsability into 2 processes :
			// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
			// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
			bool in_parent = child.spawn( as_session , {} , cstdin , cstdout , cstderr ) ;
			if (!in_parent) {
				Child grand_child ;
				AutodepPtrace::s_autodep_env = new AutodepEnv{autodep_env} ;
				try {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					grand_child.spawn(
						as_session , args
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
				trace("pid",grand_child.pid) ;
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
			try {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
				child.spawn(
					as_session , args
				,	cstdin , cstdout , cstderr
				,	env , &add_env
				,	chroot
				,	cwd
				) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} catch(::string const& e) {
				if (cstderr==Child::Pipe) stderr = e ;
				else                      cstderr.write(e) ;
				return Status::EarlyErr ;
			}
			trace("pid",child.pid) ;
		}
		pid = child.pid ;
	}
	//
	Fd                              child_fd           = ::eventfd(0,EFD_CLOEXEC)                                    ;
	::jthread                       wait_jt            { _child_wait_thread_func , &wstatus , child.pid , child_fd } ;                                        // thread dedicated to wating child
	Epoll                           epoll              { New }                                                       ;
	Status                          status             = Status::New                                                 ;
	uint8_t                         n_active           = 0                                                           ;
	::umap<Fd/*reply*/,ServerReply> server_replies     ;
	::umap<Fd         ,Jerr       > delayed_check_deps ; // check_deps events are delayed to ensure all previous deps are taken into account
	Pdate                           job_end            ;
	Pdate                           reporting_end      ;
	//
	auto handle_req_to_server = [&]( Fd fd , Jerr&& jerr ) -> bool/*still_sync*/ {
		trace("slave",fd,jerr) ;
		Proc     proc       = jerr.proc         ;                                                                                   // capture essential info before moving to server_cb
		size_t   sz         = jerr.files.size() ;                                                                                   // .
		bool     sync_      = jerr.sync         ;                                                                                   // .
		::string codec_file ;
		switch (proc) {
			case Proc::ChkDeps  : reorder(false/*at_end*/) ;                                                                break ; // ensure server sees a coherent view
			case Proc::DepInfos : _new_accesses(fd,::copy(jerr)) ;                                                          break ;
			case Proc::Decode   : codec_file = Codec::mk_decode_node( jerr.files[0].first/*file*/ , jerr.ctx , jerr.txt ) ; break ;
			case Proc::Encode   : codec_file = Codec::mk_encode_node( jerr.files[0].first/*file*/ , jerr.ctx , jerr.txt ) ; break ;
			default : ;
		}
		Fd reply_fd = server_cb(::move(jerr)) ;
		if (+reply_fd) {
			epoll.add_read(reply_fd,Kind::ServerReply) ;
			trace("read_reply",reply_fd) ;
			ServerReply& sr = server_replies[reply_fd] ;
			if (sync_) sr.fd         = fd         ;
			/**/       sr.codec_file = codec_file ;
		} else if (sync_) {
			JobExecRpcReply sync_reply ;
			sync_reply.proc      = proc ;
			sync_reply.ok        = Yes                                          ;                                                   // try to mimic server as much as possible when none is available
			sync_reply.dep_infos = ::vector<pair<Bool3/*ok*/,Crc>>(sz,{Yes,{}}) ;                                                   // .
			sync(fd,sync_reply) ;
		}
		return false ;
	} ;
	auto set_status = [&]( Status status_ , ::string const& msg_={} )->void {
		if ( status==Status::New || status==Status::Ok ) status = status_ ;                // else there is already another reason
		if ( +msg_                                     ) append_line_to_string(msg,msg_) ;
	} ;
	auto dec_active = [&]()->void {
		SWEAR(n_active) ;
		n_active-- ;
		if ( !n_active && +network_delay ) reporting_end = Pdate(New)+network_delay ;      // once job is dead, wait at most network_delay (if set) for reporting to calm down
	} ;
	//
	SWEAR(!slaves) ;
	//
	if (+timeout) {
		job_end = Pdate(New) + timeout ;
		trace("set_timeout",timeout,job_end) ;
	}
	if (cstdout==Child::Pipe) { epoll.add_read(child_stdout=child.stdout,Kind::Stdout  ) ; n_active++ ; trace("read_stdout",child_stdout) ; }
	if (cstderr==Child::Pipe) { epoll.add_read(child_stderr=child.stderr,Kind::Stderr  ) ; n_active++ ; trace("read_stderr",child_stderr) ; }
	/**/                      { epoll.add_read(child_fd                 ,Kind::ChildEnd) ; n_active++ ; trace("read_child ",child_fd    ) ; }
	/**/                      { epoll.add_read(master_fd                ,Kind::Master  ) ;              trace("read_master",master_fd   ) ; }
	while (epoll.cnt) {
		uint64_t wait_ns = Epoll::Forever ;
		if (+reporting_end) {
			Pdate now = {New} ;
			if (now<reporting_end) wait_ns = (reporting_end-now).nsec() ;
			else                   break ;
		} else if ( !killed && +job_end ) {
			Pdate now = {New} ;
			if (now<job_end) {
				wait_ns = (job_end-now).nsec() ;
			} else {
				trace("fire_timeout",now) ;
				killed = true ;
				set_status(Status::Err,to_string("timout after ",timeout.short_str())) ;
				kill_job_cb() ;
				wait_ns = {} ;
			}
		}
		if (+delayed_check_deps) wait_ns = 0 ;
		::vector<Epoll::Event> events = epoll.wait(wait_ns) ;
		if ( !events && +delayed_check_deps ) {                                                                                    // process delayed check deps after all other events
			trace("delayed_chk_deps") ;
			for( auto& [fd,jerr] : delayed_check_deps ) handle_req_to_server(fd,::move(jerr)) ;
			delayed_check_deps.clear() ;
			continue ;
		}
		for( Epoll::Event const& event : events ) {
			Kind kind = event.data<Kind>() ;
			Fd   fd   = event.fd()         ;
			if (kind!=Kind::Slave) trace(kind,fd,epoll.cnt) ;
			switch (kind) {
				case Kind::Stdout :
				case Kind::Stderr : {
					char          buf[4096] ;
					int           cnt       = ::read( fd , buf , sizeof(buf) ) ; SWEAR( cnt>=0 , cnt ) ;
					::string_view buf_view  { buf , size_t(cnt) }                                      ;
					if (cnt) {
						if (kind==Kind::Stderr)   stderr.append(buf_view) ;
						else                    { stdout.append(buf_view) ; live_out_cb(buf_view) ; }
					} else {
						epoll.del(fd) ;                                                                                            // /!\ dont close as fd is closed upon child destruction
						trace("close",kind,fd) ;
						if (kind==Kind::Stderr) child_stderr = {} ;                                                                // tell kill not to wait for this one
						else                    child_stdout = {} ;
						dec_active() ;
					}
				} break ;
				case Kind::ChildEnd : {
					uint64_t one = 0/*garbage*/            ;
					int      cnt = ::read( fd , &one , 8 ) ; SWEAR( cnt==8 && one==1 , cnt , one ) ;
					{	Lock lock{_pid_mutex} ;
						child.pid = -1 ;                                                                                           // too late to kill job
					}
					if      (WIFEXITED  (wstatus)) set_status( WEXITSTATUS(wstatus)!=0        ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(wstatus)) set_status( is_sig_sync(WTERMSIG(wstatus)) ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                           fail("unexpected wstatus : ",wstatus) ;
					epoll.close(fd) ;
					epoll.cnt-- ;               // do not wait for new connections, but if one arrives before all flows are closed, process it, so wait Master no more
					dec_active() ;
					trace("close",kind,status,::hex,wstatus,::dec) ;
				} break ;
				case Kind::Master : {
					SWEAR(fd==master_fd) ;
					Fd slave = master_fd.accept() ;
					epoll.add_read(slave,Kind::Slave) ;
					trace("read_slave",slave) ;
					slaves[slave] ;             // allocate entry
				} break ;
				case Kind::ServerReply : {
					JobRpcReply jrr ;
					auto it = server_replies.find(fd) ;
					SWEAR(it!=server_replies.end()) ;
					try         { if (!it->second.buf.receive_step(fd,jrr)) continue ; }
					catch (...) {                                                      }                                      // server disappeared, give up
					trace(jrr) ;
					Fd rfd = it->second.fd ;                                                                                  // capture before move
					switch (jrr.proc) {
						case JobProc::ChkDeps  : if (jrr.ok==Maybe) { set_status(Status::ChkDeps) ; kill_job_cb() ; } break ;
						case JobProc::DepInfos :                                                                      break ;
						case JobProc::Decode   :
						case JobProc::Encode   : _codec(::move(it->second),jrr) ;                                     break ;
						case JobProc::None     : kill_job_cb() ;                                                      break ; // server died
					DF}
					//        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					if (+rfd) sync( rfd , JobExecRpcReply(jrr) ) ;
					//        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					server_replies.erase(it) ;
					epoll.close(fd) ;
					trace("close",kind,fd) ;
				} break ;
				case Kind::Slave : {
					Jerr jerr         ;
					auto it           = slaves.find(fd) ;
					auto& slave_entry = it->second      ;
					try         { if (!slave_entry.first.receive_step(fd,jerr)) continue ; }
					catch (...) { trace("no_jerr",jerr) ; jerr.proc = Proc::None ;         }                                  // fd was closed, ensure no partially received jerr
					Proc proc  = jerr.proc ;                                                                                  // capture essential info so as to be able to move jerr
					bool sync_ = jerr.sync ;                                                                                  // .
					if ( proc!=Proc::Access                 ) trace(kind,fd,epoll.cnt,proc) ;                                 // there may be too many Access'es, only trace within _new_accesses
					if ( proc>=Proc::HasFiles && jerr.solve ) _solve(fd,jerr)               ;
					switch (proc) {
						case Proc::Confirm :
							for( Jerr& j : slave_entry.second ) { j.digest.write = jerr.digest.write ; _new_accesses(fd,::move(j)) ; }
							slave_entry.second.clear() ;
						break ;
						case Proc::None :
							epoll.close(fd) ;
							trace("close",kind,fd) ;
							for( Jerr& j : slave_entry.second ) _new_accesses(fd,::move(j)) ;                                 // process deferred entries although with uncertain outcome
							slaves.erase(it) ;
						break ;
						case Proc::Access   :
							// for read accesses, trying is enough to trigger a dep, so confirm is useless
							if ( jerr.digest.write==Maybe ) slave_entry.second.push_back(::move(jerr)) ;                      // defer until confirm resolution
							else                            _new_accesses(fd,::move(jerr))             ;
						break ;
						case Proc::Tmp      : seen_tmp = true ;                                  break           ;
						case Proc::Guard    : _new_guards(fd,::move(jerr)) ;                     break           ;
						case Proc::DepInfos :
						case Proc::Decode   :
						case Proc::Encode   : handle_req_to_server(fd,::move(jerr)) ;            goto NoReply    ;
						case Proc::ChkDeps  : delayed_check_deps[fd] = ::move(jerr) ;            goto NoReply    ;            // if sync, reply is delayed as well
						case Proc::Panic    : set_status(Status::Err,jerr.txt) ; kill_job_cb() ; [[fallthrough]] ;
						case Proc::Trace    : trace(jerr.txt) ;                                  break           ;
					DF}
					if (sync_) sync( fd , JobExecRpcReply(proc) ) ;
				NoReply : ;
				} break ;
			DF}
		}
	}
	trace("done") ;
	reorder(true/*at_end*/) ;                                                                                                 // ensure server sees a coherent view
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
			return ::pair(a.second.first_read(),a.second.parallel_id) < ::pair(b.second.first_read(),b.second.parallel_id) ;
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
