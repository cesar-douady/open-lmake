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

ENUM_1( Order               // order of incoming date wrt AccessDigest object
,	Write = InbetweenWrites // <Write means before first write
,	Before                  // date<first_read
,	BetweenReadAndWrite     //      first_read<date<first_write
,	InbetweenWrites         //                      first_write<date<last_write
,	After                   //                                       last_write<date
)

//
// Gather::AccessInfo
//

::ostream& operator<<( ::ostream& os , Gather::AccessInfo const& ai ) {
	/**/                                                         os << "AccessInfo("                                            ;
	if ( +ai.digest.accesses                                   ) os << "R:" <<ai.first_read                               <<',' ;
	if (  ai.digest.write!=No                                  ) os << "W1:"<<ai.first_write<<(ai.first_confirmed?"?":"") <<',' ;
	if (  ai.digest.write!=No && ai.first_write!=ai.last_write ) os << "WL:"<<ai.last_write <<(ai.last_confirmed ?"?":"") <<',' ;
	if ( +ai.crc_date                                          ) os << ai.crc_date                                        <<',' ;
	/**/                                                         os << ai.digest                                                ;
	if ( +ai.digest.accesses                                   ) os <<','<< ai.parallel_id                                      ;
	if (  ai.seen                                              ) os <<",seen"                                                   ;
	return                                                       os <<')'                                                       ;
}

void Gather::AccessInfo::chk() const {
	if (!digest.accesses ) SWEAR( !crc_date                                          , crc_date                              ) ; // cannot know about the file    without accessing it
	if (!digest.accesses ) SWEAR( !seen                                                                                      ) ; // cannot see a file as existing without accessing it
	if (!digest          ) SWEAR( !first_read && !first_write && !last_write         , first_read , first_write , last_write ) ;
	else                   SWEAR( +first_read && +first_write && +last_write         , first_read , first_write , last_write ) ;
	if (+digest          ) SWEAR( first_read<=first_write && first_write<=last_write , first_read , first_write , last_write ) ; // check access order
	if ( digest.write==No) SWEAR(                            first_write==last_write ,              first_write , last_write ) ; // first_read may be earlier if a 2nd read access made it so
	if (!digest.accesses ) SWEAR( first_read==first_write                            , first_read , first_write              ) ; // first_read                 is  set to first_write
	if ( digest.write==No) SWEAR( first_confirmed==last_confirmed                    , first_confirmed , last_confirmed      ) ; // need 2 accesses to distinguish between first and last write
}

void Gather::AccessInfo::update( PD pd , bool phony_ok_ , AccessDigest ad , CD const& cd , Bool3 confirm , NodeIdx parallel_id_ ) {
	Order order = Order::Before/*garbage*/ ;
	if (confirm==No  ) ad.write = No ;              // if confirm==No, writes have not occurred
	if (!ad          ) return ;
	if ( ad.write!=No) phony_ok |= phony_ok_ ;
	if (!digest      ) {
		/**/              digest                                = ad           ;
		/**/              first_confirmed = last_confirmed      = confirm==Yes ;
		if (+digest     ) first_read = first_write = last_write = pd           ;
		if (+ad.accesses) {
			crc_date    = cd                   ;
			parallel_id = parallel_id_         ;
			seen        = cd.seen(ad.accesses) ;
		}
		return ;
	}
	//
	digest.tflags       |= ad.tflags       ;
	digest.extra_tflags |= ad.extra_tflags ;
	digest.dflags       |= ad.dflags       ;
	digest.extra_dflags |= ad.extra_dflags ;
	//
	order =
		pd<first_read  ? Order::Before
	:	pd<first_write ? Order::BetweenReadAndWrite
	:	pd<last_write  ? Order::InbetweenWrites
	:                    Order::After
	;
	// manage read access
	if (order==Order::Before) {
		if ( ad.write!=No && confirm==Yes ) {       // if written before first read, wash read access
			digest.accesses = {}    ;
			seen            = false ;
		}
		if (+ad.accesses) {
			crc_date    = cd           ;
			parallel_id = parallel_id_ ;
			first_read  = pd           ;
		}
	}
	if ( order<Order::Write || !first_confirmed ) { // this is slightly pessimistic as there may be a confirmed write later, maintain a confirmed_write date if this is important
		digest.accesses |= ad.accesses          ;
		seen            |= cd.seen(ad.accesses) ;   // if read before first write, record if we have seen a file
	}
	// manage write access
	if (ad.write!=No) {
		switch (order) {
			case Order::Before              : first_read = first_write = pd ; first_confirmed = confirm==Yes ;                       break ;
			case Order::After               :              last_write  = pd ; last_confirmed  = confirm==Yes ; if (digest.write!=No) break ; [[fallthrough]] ; // also first write if no previous ones
			case Order::BetweenReadAndWrite :              first_write = pd ; first_confirmed = confirm==Yes ;                       break ;
			case Order::InbetweenWrites     : SWEAR(digest.write!=No) ;                                                              break ;                   // if idle, first_write==last_write
		DF}
		if ( digest.write==No || order==Order::After ) digest.write = ad.write ;                                                                               // last action survives
	}
}

//
// Gather
//

::ostream& operator<<( ::ostream& os , Gather const& gd ) {
	/**/             os << "Gather(" << gd.accesses ;
	if (gd.seen_tmp) os <<",seen_tmp"               ;
	return           os << ')'                      ;
}

void Gather::_new_access( Fd fd , PD pd , bool phony_ok , ::string&& file , AccessDigest ad , CD const& cd , Bool3 confirm , bool parallel , ::string const& comment ) {
	SWEAR( +file , comment ) ;
	AccessInfo* info        = nullptr/*garbage*/                       ;
	auto        [it,is_new] = access_map.emplace(file,accesses.size()) ;
	if (is_new) {
		accesses.emplace_back(::move(file),AccessInfo()) ;
		info = &accesses.back().second ;
	} else {
		info = &accesses[it->second].second ;
	}
	if (!parallel) parallel_id++ ;
	AccessInfo old_info = *info ;                                                                                                                                     // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update( pd , phony_ok , ad , cd , confirm , parallel_id ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || *info!=old_info ) Trace("_new_access", fd , STR(is_new) , pd , ad , cd , confirm , parallel_id , comment , old_info , "->" , *info , it->first ) ; // only trace if something changes
}

void Gather::new_deps( PD pd , ::vmap_s<DepDigest>&& deps , ::string const& stdin ) {
	bool parallel = false ;
	for( auto& [f,dd] : deps ) {
		bool is_stdin = f==stdin ;
		if (is_stdin) {                               // stdin is read
			if (!dd.accesses) dd.date(file_date(f)) ; // record now if not previously accessed
			dd.accesses |= Access::Reg ;
		}
		_new_access( pd , ::move(f) , {.accesses=dd.accesses,.dflags=dd.dflags} , dd , Yes/*confirm*/ , parallel , is_stdin?"stdin"s:"s_deps"s ) ;
		parallel = true ;
	}
}

void Gather::new_exec( PD pd , ::string const& exe , ::string const& c ) {
	RealPath              rp       { autodep_env }                    ;
	RealPath::SolveReport sr       = rp.solve(exe,false/*no_follow*/) ;
	bool                  parallel = false                            ;
	for( auto&& [f,a] : rp.exec(sr) ) {
		_new_access( pd , ::move(f) , {.accesses=a} , file_date(f) , Yes/*confirm*/ , parallel , c ) ;
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
	Trace trace("kill",sig,pid,STR(as_session),child_stdout,child_stderr,mk_key_uset(slaves)) ;
	::unique_lock lock      { _pid_mutex }           ;
	int           kill_sig  = sig>=0 ? sig : SIGKILL ;
	bool          killed_   = false                  ;
	killed = true ;                                                                                                  // prevent child from starting if killed before
	//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	if (pid>1) killed_ = kill_process(pid,kill_sig,as_session/*as_group*/) ;
	//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (sig<0) {                                                                                                     // kill all processes (or process groups) connected to a stream we wait for
		pid_t         ctl_pid = getpid() ;
		::uset_s      fd_strs ;
		::uset<pid_t> to_kill ;                                                                                      // maintain an ordered tab to favor repeatability
		if (+child_stdout)                 fd_strs.insert( read_lnk(to_string("/proc/self/fd/",child_stdout.fd)) ) ;
		if (+child_stderr)                 fd_strs.insert( read_lnk(to_string("/proc/self/fd/",child_stderr.fd)) ) ;
		for( auto const& [fd,_] : slaves ) fd_strs.insert( read_lnk(to_string("/proc/self/fd/",fd          .fd)) ) ;
		for( ::string const& proc_entry : lst_dir("/proc")  ) {
			for( char c : proc_entry ) if (c>'9'||c<'0') goto NextProc ;
			try {
				pid_t child_pid = from_string<pid_t>(proc_entry) ;
				if (child_pid==ctl_pid) goto NextProc ;
				if (as_session        ) child_pid = ::getpgid(child_pid) ;
				if (child_pid<=1      ) goto NextProc ;                                                              // no pgid available, ignore
				for( ::string const& fd_entry : lst_dir(to_string("/proc/",proc_entry,"/fd")) ) {
					::string fd_str = read_lnk(to_string("/proc/",proc_entry,"/fd/",fd_entry)) ;
					if ( !fd_str                   ) continue ;                                                      // fd has disappeared, ignore
					if ( !fd_strs.contains(fd_str) ) continue ;                                                      // not any of the fd's we are looking for
					to_kill.insert(child_pid) ;
					break ;
				}
			} catch(::string const&) {}                                                                              // if we cannot read /proc/pid, process is dead, ignore
		NextProc : ;
		}
		trace("to_kill",to_kill) ;
		//                                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( pid_t p : to_kill ) killed_ |= kill_process(p,kill_sig,as_session/*as_group*/) ;
		//                                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	trace("done",STR(killed_)) ;
	return killed_ ;
}

void Gather::_fix_auto_date( Fd fd , JobExecRpcReq& jerr ) {
		SWEAR( jerr.proc>=Proc::HasFiles && jerr.auto_date ) ;
		::vmap_s<Ddate> files     ;
		RealPath        real_path { autodep_env , ::move(jerr.cwd) } ;                                     // auto_date is set to false, cwd is not used anymore
		bool            read      = +jerr.digest.accesses            ;
		for( auto const& [f,dd] : jerr.files ) {
			SWEAR(+f) ;
			RealPath::SolveReport sr = real_path.solve(f,jerr.no_follow) ;
			// cf Record::_solve for explanation                                                                                                        parallel
			for( ::string& lnk : sr.lnks )    _new_access( fd , jerr.date , ::move  (lnk    ) , {.accesses=Access::Lnk} , file_date(lnk) , jerr.confirm , false , "auto_date.lnk" ) ;
			if      (read                   ) {}
			else if (sr.file_accessed==Yes  ) _new_access( fd , jerr.date , ::copy  (sr.real) , {.accesses=Access::Lnk} , Ddate()        , jerr.confirm , false , "auto_date.lst" ) ;
			else if (sr.file_accessed==Maybe) _new_access( fd , jerr.date , dir_name(sr.real) , {.accesses=Access::Lnk} , Ddate()        , jerr.confirm , false , "auto_date.lst" ) ;
			//
			seen_tmp |= sr.file_loc==FileLoc::Tmp && jerr.digest.write==Yes && !read && jerr.confirm!=No ; // if reading before writing, then we cannot populate tmp
			if (sr.file_loc>FileLoc::Repo) jerr.digest.write = No ;
			if (sr.file_loc>FileLoc::Dep ) continue ;
			if (+dd                      ) files.emplace_back( sr.real , dd                 ) ;
			else                           files.emplace_back( sr.real , file_date(sr.real) ) ;
		}
		jerr.date      = New           ;                                                                   // ensure date is posterior to links encountered while solving
		jerr.files     = ::move(files) ;
		jerr.auto_date = false         ;                                                                   // files are now real and dated
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
	{	::unique_lock lock{_pid_mutex} ;
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
	PD                              end                ;
	::umap<Fd/*reply*/,ServerReply> server_replies     ;
	::umap<Fd         ,Jerr       > delayed_check_deps ; // check_deps events are delayed to ensure all previous deps are taken into account
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
		if (status==Status::New)   status = status_ ;                                                                               // else there is already another reason
		if (+msg_              ) { set_nl(msg) ; msg += msg_ ; }
	} ;
	//
	SWEAR(!slaves) ;
	//
	if (+timeout) {
		end = PD(New) + timeout ;
		trace("set_timeout",timeout,end) ;
	}
	if (cstdout==Child::Pipe) epoll.add_read(child_stdout=child.stdout,Kind::Stdout  ) ;
	if (cstderr==Child::Pipe) epoll.add_read(child_stderr=child.stderr,Kind::Stderr  ) ;
	/**/                      epoll.add_read(child_fd                 ,Kind::ChildEnd) ;
	/**/                      epoll.add_read(master_fd                ,Kind::Master  ) ;
	while (epoll.cnt) {
		uint64_t wait_ns = Epoll::Forever ;
		if (+end) {
			PD now = New ;
			if (now>=end) {
				trace("fire_timeout",now) ;
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
		if ( !events && +delayed_check_deps ) {                                                                                     // process delayed check deps after all other events
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
					if      (!cnt              ) { trace("close") ; epoll.del(fd) ;                                   }             // /!\ dont close as fd is closed upon child destruction
					else if (kind==Kind::Stderr) { stderr.append(buf_view) ; child_stderr={} ;                        }             // tell kill not to wait for this one
					else                         { stdout.append(buf_view) ; child_stdout={} ;live_out_cb(buf_view) ; }             // .
				} break ;
				case Kind::ChildEnd : {
					uint64_t one = 0/*garbage*/            ;
					int      cnt = ::read( fd , &one , 8 ) ; SWEAR( cnt==8 && one==1 , cnt , one ) ;
					{	::unique_lock lock{_pid_mutex} ;
						child.pid = -1 ;                                                                                            // too late to kill job
					}
					if      (WIFEXITED  (wstatus)) set_status( WEXITSTATUS(wstatus)!=0        ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(wstatus)) set_status( is_sig_sync(WTERMSIG(wstatus)) ? Status::Err : Status::LateLost ) ;  // synchronous signals are actually errors
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
					Jerr jerr         ;
					auto it           = slaves.find(fd) ;
					auto& slave_entry = it->second      ;
					try         { if (!slave_entry.first.receive_step(fd,jerr)) continue ; }
					catch (...) { trace("no_jerr",jerr) ; jerr.proc = Proc::None ;           }                                // fd was closed, ensure no partially received jerr
					Proc proc  = jerr.proc ;                                                                                  // capture essential info so as to be able to move jerr
					bool sync_ = jerr.sync ;                                                                                  // .
					if ( proc!=Proc::Access                     ) trace(kind,fd,epoll.cnt,proc) ;                             // there may be too many Access'es, only trace within _new_accesses
					if ( proc>=Proc::HasFiles && jerr.auto_date ) _fix_auto_date(fd,jerr)       ;
					switch (proc) {
						case Proc::Confirm :
							for( Jerr& j : slave_entry.second ) { j.confirm = jerr.confirm ; _new_accesses(fd,::move(j)) ; }
							slave_entry.second.clear() ;
						break ;
						case Proc::None :
							epoll.close(fd) ;
							for( Jerr& j : slave_entry.second ) _new_accesses(fd,::move(j)) ;                                 // process deferred entries although with uncertain outcome
							slaves.erase(it) ;
						break ;
						case Proc::Access   :
							// for read accesses, trying is enough to trigger a dep, so confirm is useless
							if ( jerr.digest.write!=No && jerr.confirm==Maybe ) slave_entry.second.push_back(::move(jerr)) ;  // defer until confirm resolution
							else                                                _new_accesses(fd,::move(jerr))             ;
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
			return ::pair(a.second.first_read,a.second.parallel_id) < ::pair(b.second.first_read,b.second.parallel_id) ;
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
		if ( digest.write==No && !digest.dflags ) {
			if (!digest.accesses   ) { trace("skip_from_next",file) ; { if (!at_end) access_map.erase(file) ; } cpy = true ; continue ; }
			if (dirs.contains(file)) { trace("skip_from_prev",file) ; { if (!at_end) access_map.erase(file) ; } cpy = true ; continue ; }
		}
		for( ::string dir=dir_name(file) ; +dir ; dir=dir_name(dir) ) if (!dirs.insert(dir).second) break ; // all uphill dirs are already inserted if a dir has been inserted
		if (cpy) accesses[i_dst] = ::move(access) ;
		i_dst++ ;
	}
	accesses.resize(i_dst) ;
	if (at_end) access_map.clear() ;                                                                        // safer not to leave outdated info
	else        for( NodeIdx i=0 ; i<accesses.size() ; i++ ) access_map.at(accesses[i].first) = i ;         // always recompute access_map as accesses has been sorted
}
