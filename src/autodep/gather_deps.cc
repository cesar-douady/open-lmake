// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "thread.hh"

#include "ptrace.hh"

#include "gather_deps.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

//
// GatherDeps::AccessInfo
//

::ostream& operator<<( ::ostream& os , GatherDeps::AccessInfo const& ai ) {
	/**/                                        os << "AccessInfo("                    ;
	if (+ai.digest.accesses                   ) os << "R:" <<ai.access_date      <<',' ;
	if (!ai.digest.idle()                     ) os << "W1:"<<ai.first_write_date <<',' ;
	if (ai.first_write_date<ai.last_write_date) os << "WL:"<<ai.last_write_date  <<',' ;
	/**/                                        os << ai.digest                        ;
	if (+ai.digest.accesses                   ) os <<','<< ai.parallel_id              ;
	if ( ai.seen                              ) os <<",seen"                           ;
	if ( ai.target_ok                         ) os <<",target_ok"                      ;
	return                                      os <<')' ;
}

void GatherDeps::AccessInfo::update( PD pd , AccessDigest const& ad , bool tok , NodeIdx parallel_id_ ) {
	AccessOrder order =
		pd<access_date      ? AccessOrder::Before
	:	digest.idle()       ? AccessOrder::BetweenReadAndWrite
	:	pd<first_write_date ? AccessOrder::BetweenReadAndWrite
	:	pd<last_write_date  ? AccessOrder::InbetweenWrites
	:                         AccessOrder::After
	;
	if (
		+ad
	&&	(	order==AccessOrder::Before                                                                     // access_date becomes earlier
		||	( !digest.accesses && (digest.idle()||order<AccessOrder::Write) )                              // access_date becomes later
		)
	) {
		access_date = pd           ;
		parallel_id = parallel_id_ ;
	}
	if (!ad.idle()) {
		if      (digest.idle()             ) first_write_date = last_write_date = pd    ;
		else if (order==AccessOrder::After )                    last_write_date = pd    ;
		else if (order< AccessOrder::Write ) first_write_date                   = pd    ;
		if      (order==AccessOrder::Before) seen                               = false ;                  // file has been written before first known read, wash read access
	}
	seen      |= order<AccessOrder::Write && +ad.accesses && (ad.is_date?+ad.date():ad.crc()!=Crc::None) ; // record this read access if done before first known write
	target_ok |= tok                                                                                     ; // user explicitely allowed writing to this file
	//
	digest.update(ad,order) ;                                                                              // execute actions in actual order as provided by dates
	chk() ;
}

//
// GatherDeps
//

::ostream& operator<<( ::ostream& os , GatherDeps const& gd ) {
	/**/             os << "GatherDeps(" << gd.accesses ;
	if (gd.seen_tmp) os <<",seen_tmp"                   ;
	return           os << ')'                          ;
}

void GatherDeps::_new_access( Fd fd , PD pd , ::string&& file , AccessDigest const& ad , bool target_ok , ::string const& comment ) {
	SWEAR( +file , comment ) ;
	AccessInfo* info        = nullptr/*garbage*/                       ;
	auto        [it,is_new] = access_map.emplace(file,accesses.size()) ;
	if (is_new) {
		accesses.emplace_back(::move(file),AccessInfo(pd)) ;
		info = &accesses.back().second ;
	} else {
		info = &accesses[it->second].second ;
	}
	AccessInfo old_info = *info ;                                                                                                                      // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update(pd,ad,target_ok,parallel_id) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || *info!=old_info ) Trace("_new_access", fd , STR(is_new) , pd , it->first , ad , parallel_id , comment , old_info , "->" , *info ) ; // only trace if something changes
}

void GatherDeps::new_deps( PD pd , ::vmap_s<DepDigest>&& deps , ::string const& stdin ) {
	for( auto& [f,dd] : deps ) {
		bool is_stdin = f==stdin ;
		if (is_stdin) {                               // stdin is read
			if (!dd.accesses) dd.date(file_date(f)) ; // record now if not previously accessed
			dd.accesses |= Access::Reg ;
		}
		_new_access( pd , ::move(f) , dd , is_stdin?"stdin"s:"s_deps"s ) ;
	}
}

void GatherDeps::new_exec( PD pd , ::string const& exe , ::string const& c ) {
	RealPath              rp { autodep_env }                    ;
	RealPath::SolveReport sr = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(sr) ) {
		parallel_id++ ;
		_new_access( pd , ::move(f) , {a,file_date(f)} , c ) ;
	}
}

ENUM( GatherDepsKind , Stdout , Stderr , ServerReply , ChildEnd , Master , Slave )

void _child_wait_thread_func( int* wstatus , pid_t pid , Fd fd ) {
	static constexpr uint64_t One = 1 ;
	do { ::waitpid(pid,wstatus,0) ; } while (WIFSTOPPED(*wstatus)) ;
	swear_prod(::write(fd,&One,8)==8,"cannot report child wstatus",wstatus) ;
}

bool/*done*/ GatherDeps::kill(int sig) {
	Trace trace("kill",sig,pid,STR(create_group),child_stdout,child_stderr) ;
	::unique_lock lock{_pid_mutex} ;
	killed = true ;                                                                                                     // prevent child from starting if killed before
	int  kill_sig = sig>=0 ? sig : SIGKILL ;
	bool killed_  = false                  ;
	//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	if (pid>1) killed_ = kill_process(pid,kill_sig,create_group) ;
	//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( sig<0 && (+child_stdout||+child_stderr) ) {                                                                    // kill all processes (or process groups) connected to a stream we wait for
		pid_t        ctl_pid   = getpid() ;
		::string     stdout_fn ; if (+child_stdout) stdout_fn = read_lnk(to_string("/proc/self/fd/",child_stdout.fd)) ;
		::string     stderr_fn ; if (+child_stderr) stderr_fn = read_lnk(to_string("/proc/self/fd/",child_stderr.fd)) ;
		::set<pid_t> to_kill   ;                                                                                        // maintain an ordered tab to favor repeatability
		for( ::string const& proc_entry : lst_dir("/proc")  ) {
			for( char c : proc_entry ) if (c>'9'||c<'0') goto NextProc ;
			try {
				pid_t child_pid = from_string<pid_t>(proc_entry) ;
				if (child_pid==ctl_pid) goto NextProc ;
				if (create_group      ) child_pid = ::getpgid(child_pid) ;
				if (child_pid<=1      ) goto NextProc ;                                                                 // no pgid available, ignore
				for( ::string const& fd_entry : lst_dir(to_string("/proc/",proc_entry,"/fd")) ) {
					::string fd_fn = read_lnk(to_string("/proc/",proc_entry,"/fd/",fd_entry)) ;
					if ( !fd_fn                               ) continue ;                                              // fd has disappeared, ignore
					if ( fd_fn!=stdout_fn && fd_fn!=stderr_fn ) continue ;                                              // not the fd we are looking for
					to_kill.insert(child_pid) ;
					break ;
				}
			} catch(::string const&) {}                                                                                 // if we cannot read /proc/pid, process is dead, ignore
		NextProc : ;
		}
		trace("to_kill",to_kill) ;
		//                                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( pid_t p : to_kill ) killed_ |= kill_process(p,kill_sig,create_group) ;
		//                                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	trace("done",STR(killed_)) ;
	return killed_ ;
}

void GatherDeps::_fix_auto_date( Fd fd , JobExecRpcReq& jerr ) {
		SWEAR( jerr.proc>=Proc::HasFiles && jerr.auto_date ) ;
		::vmap_s<Ddate> files     ;
		RealPath        real_path { autodep_env , ::move(jerr.cwd) } ; // auto_date is set to false, cwd is not used anymore
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR(+f) ;
			RealPath::SolveReport sr = real_path.solve(f,jerr.no_follow) ;
			for( ::string& lnk : sr.lnks )               _new_access(fd,jerr.date,::move(lnk       ) ,{Access::Lnk,file_date(lnk)},jerr.ok,"auto_date.lnk"     ) ; // cf Record::_solve for explanation
			if ( !jerr.digest.accesses && +sr.last_lnk ) _new_access(fd,jerr.date,::move(sr.last_lnk),{Access::Lnk,Ddate()       },jerr.ok,"auto_date.last_lnk") ; // .
			//
			seen_tmp |= sr.file_loc==FileLoc::Tmp && jerr.digest.write ;
			if (sr.file_loc>FileLoc::Repo) jerr.digest.write = false ;
			if (sr.file_loc>FileLoc::Dep ) continue ;
			if (+dd                      ) files.emplace_back( sr.real , dd                 ) ;
			else                           files.emplace_back( sr.real , file_date(sr.real) ) ;
		}
		jerr.date      = Pdate::s_now() ; // ensure date is posterior to links encountered while solving
		jerr.files     = ::move(files)  ;
		jerr.auto_date = false          ; // files are now real and dated
}

Status GatherDeps::exec_child( ::vector_s const& args , Fd cstdin , Fd cstdout , Fd cstderr ) {
	using Kind = GatherDepsKind ;
	Trace trace("exec_child",STR(create_group),method,autodep_env,args) ;
	if (env) trace("env",*env) ;
	Child child ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service = master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	::map_ss add_env {{"LMAKE_AUTODEP_ENV",autodep_env}} ;                     // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	{	::unique_lock lock{_pid_mutex} ;
		if (killed) return Status::Killed ;                                    // dont start if we are already killed before starting
		if (method==AutodepMethod::Ptrace) {                                   // PER_AUTODEP_METHOD : handle case
			// XXX : splitting responsibility is no more necessary. Can directly report child termination from within autodep_ptrace.process using same ifce as _child_wait_thread_func
			// we split the responsability into 2 processes :
			// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
			// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
			bool in_parent = child.spawn( create_group/*as_group*/ , {} , cstdin , cstdout , cstderr ) ;
			if (!in_parent) {
				Child grand_child ;
				AutodepPtrace::s_autodep_env = new AutodepEnv{autodep_env} ;
				try {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					grand_child.spawn(
						false/*as_group*/ , args
					,	Fd::Stdin , Fd::Stdout , Fd::Stderr
					,	env , &add_env
					,	chroot
					,	cwd
					,	AutodepPtrace::s_prepare_child
					) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				} catch(::string const& e) {
					exit(2,e) ;
				}
				trace("pid",grand_child.pid) ;
				AutodepPtrace autodep_ptrace { grand_child.pid }        ;
				int           wstatus        = autodep_ptrace.process() ;
				grand_child.waited() ;                                         // grand_child has already been waited
				if      (WIFEXITED  (wstatus)) ::_exit(WEXITSTATUS(wstatus)) ;
				else if (WIFSIGNALED(wstatus)) ::_exit(2                   ) ;
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
			new_exec( PD::s_now() , args[0] ) ;
			try {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
				child.spawn(
					create_group , args
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
	Fd                                child_fd           = ::eventfd(0,EFD_CLOEXEC)                                    ;
	::jthread                         wait_jt            { _child_wait_thread_func , &wstatus , child.pid , child_fd } ;                                      // thread dedicated to wating child
	Epoll                             epoll              { New }                                                       ;
	Status                            status             = Status::New                                                 ;
	PD                                end                ;
	::umap<Fd         ,IMsgBuf      > slaves             ;
	::umap<Fd/*reply*/,ServerReply  > server_replies     ;
	::umap<Fd         ,JobExecRpcReq> delayed_check_deps ;                                  // check_deps events are delayed to ensure all previous deps are taken into account
	auto handle_req_to_server = [&]( Fd fd , JobExecRpcReq&& jerr ) -> bool/*still_sync*/ {
		trace("slave",fd,jerr) ;
		Proc     proc       = jerr.proc         ;                                                                                   // capture essential info before moving to server_cb
		size_t   sz         = jerr.files.size() ;                                                                                   // .
		bool     sync_      = jerr.sync         ;                                                                                   // .
		::string codec_file ;
		switch (proc) {
			case Proc::ChkDeps  : reorder() ;                                                                               break ; // ensure server sees a coherent view
			case Proc::DepInfos : _new_accesses(fd,::copy(jerr)) ;                                                          break ;
			case Proc::Decode   : codec_file = Codec::mk_decode_node( jerr.files[0].first/*file*/ , jerr.ctx , jerr.txt ) ; break ;
			case Proc::Encode   : codec_file = Codec::mk_encode_node( jerr.files[0].first/*file*/ , jerr.ctx , jerr.txt ) ; break ;
			default : ;
		}
		Fd reply_fd = server_cb(::move(jerr)) ;
		trace("reply",reply_fd) ;
		if (+reply_fd) {
			epoll.add_read(reply_fd,Kind::ServerReply) ;
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
		if (status==Status::New)   status = status_ ;                                                    // else there is already another reason
		if (+msg_              ) { set_nl(msg) ; msg += msg_ ; }
	} ;
	//
	if (+timeout            ) end = PD::s_now() + timeout ;
	if (cstdout==Child::Pipe) epoll.add_read(child_stdout=child.stdout,Kind::Stdout  ) ;
	if (cstderr==Child::Pipe) epoll.add_read(child_stderr=child.stderr,Kind::Stderr  ) ;
	/**/                      epoll.add_read(child_fd                 ,Kind::ChildEnd) ;
	/**/                      epoll.add_read(master_fd                ,Kind::Master  ) ;
	while (epoll.cnt) {
		uint64_t wait_ns = Epoll::Forever ;
		if (+end) {
			PD now = PD::s_now() ;
			if (now>=end) {
				if (!killed) {
					killed = true ;
					set_status(Status::Err,to_string("timout after ",timeout.short_str())) ;
				}
				kill_job_cb() ;
			}
			wait_ns = (end-now).nsec() ;
		}
		if (+delayed_check_deps) wait_ns = 0 ;
		::vector<Epoll::Event> events = epoll.wait(wait_ns) ;
		if ( !events && +delayed_check_deps ) {                                                          // process delayed check deps after all other events
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
					if      (!cnt              ) { trace("close") ; epoll.del(fd) ;                  }   // /!\ fd is closed when child is destructed, closing it twice may close somebody else's fd
					else if (kind==Kind::Stderr) { stderr.append(buf_view) ;                         }
					else                         { stdout.append(buf_view) ; live_out_cb(buf_view) ; }
				} break ;
				case Kind::ChildEnd : {
					uint64_t one = 0/*garbage*/            ;
					int      cnt = ::read( fd , &one , 8 ) ; SWEAR( cnt==8 && one==1 , cnt , one ) ;
					{	::unique_lock lock{_pid_mutex} ;
						child.pid = -1 ;                                                                                           // too late to kill job
					}
					if      (WIFEXITED  (wstatus)) set_status( WEXITSTATUS(wstatus)!=0        ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(wstatus)) set_status( is_sig_sync(WTERMSIG(wstatus)) ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                           fail("unexpected wstatus : ",wstatus) ;
					trace(status,::hex,wstatus,::dec) ;
					epoll.close(fd) ;
					epoll.cnt-- ;                       // do not wait for new connections, but if one arrives before all flows are closed, process it
				} break ;
				case Kind::Master : {
					SWEAR(fd==master_fd) ;
					Fd slave = master_fd.accept() ;
					trace(slave) ;
					epoll.add_read(slave,Kind::Slave) ;
					slaves[slave] ;                     // allocate entry
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
				} break ;
				case Kind::Slave : {
					JobExecRpcReq jerr  ;
					try         { if (!slaves.at(fd).receive_step(fd,jerr)) continue ; }
					catch (...) {                                                      }                                      // server disappeared, give up
					Proc proc  = jerr.proc ;                                                                                  // capture essential info so as to be able to move jerr
					bool sync_ = jerr.sync ;                                                                                  // .
					if ( proc!=Proc::Access                     ) trace(kind,fd,epoll.cnt,proc) ;                             // there may be too many Access'es, only trace within _new_accesses
					if ( proc>=Proc::HasFiles && jerr.auto_date ) _fix_auto_date(fd,jerr)       ;
					switch (proc) {
						case Proc::None     : epoll.close(fd) ; slaves.erase(fd) ;               break           ;
						case Proc::Tmp      : seen_tmp = true ;                                  break           ;
						//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						case Proc::Access   : _new_accesses(fd,::move(jerr)) ;                   break           ;
						case Proc::Guard    : _new_guards  (fd,::move(jerr)) ;                   break           ;
						case Proc::Confirm  : _confirm     (fd,::move(jerr)) ;                   break           ;
						//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
	reorder() ;                                                                                                               // ensure server sees a coherent view
	return status ;
}

void GatherDeps::reorder() {
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	Trace trace("reorder") ;
	::stable_sort(                                   // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b ) -> bool {
			return ::pair(a.second.access_date,a.second.parallel_id) < ::pair(b.second.access_date,b.second.parallel_id) ;
		}
	) ;
	// first pass : note stat accesses that are directories of immediately following file accesses as these are already implicit deps (through Uphill rule)
	::uset<size_t> to_del ;
	size_t         last   = Npos ;                   // XXX : replace with a vector to manage parallel deps
	for( size_t i1=accesses.size() ; i1>0 ; i1-- ) {
		size_t      i           = i1-1        ;
		auto const& [file,info] = accesses[i] ;
		if      ( !info.is_dep()                                                                      ) last = Npos ;
		else if ( last==Npos                                                                          ) last = i    ;
		else if ( info.digest.accesses!=Access::Stat                                                  ) last = i    ;
		else if (!( accesses[last].first.starts_with(file) && accesses[last].first[file.size()]=='/' )) last = i    ;
		else                                                                                            to_del.insert(i) ;
	}
	// second pass : suppress stat accesses that are directories of seen files as these are already implicit deps (through Uphill rule)
	::uset_s dirs ;
	size_t   n    = 0     ;
	bool     cpy  = false ;
	for( size_t i=0 ; i<accesses.size() ; i++ ) {
		auto const& [file,info] = accesses[i] ;
		if (to_del.contains(i)) { trace("skip_from_next",file) ; goto Skip ; }
		if ( info.is_dep() ) {
			if ( info.digest.accesses==Access::Stat && dirs.contains(file) ) { trace("skip_from_prev",file) ; goto Skip ; } // as soon as an entry is removed, we must copy the following ones
			for( ::string dir=dir_name(file) ; +dir ; dir=dir_name(dir) )
				if (!dirs.insert(dir).second) break ;                                                                       // all uphill dirs are already inserted if a dir has been inserted
		}
		if (cpy) accesses[n] = ::move(accesses[i]) ;
		n++ ;
		continue ;
	Skip :
		cpy = true ;
	}
	accesses.resize(n) ;
	// recompute access_map
	if (cpy) access_map.clear() ;                                                    // fast path : no need to clear if all elements will be refreshed
	for( NodeIdx i=0 ; i<accesses.size() ; i++ ) access_map[accesses[i].first] = i ; // reconstruct access_map as reorder may be called during execution (DepInfos or ChkDeps)
}
