// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "hash.hh"
#include "trace.hh"
#include "thread.hh"
#include "time.hh"

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
	if (+ai.file_date                         ) os <<','<< "F:"<<ai.file_date          ;
	if (!ai.digest.idle()                     ) os <<','<< ai.tflags                   ;
	if (+ai.digest.accesses                   ) os <<','<< ai.parallel_id              ;
	return                                      os <<')' ;
}

void GatherDeps::AccessInfo::update( PD pd , DD dd , AccessDigest const& ad , NodeIdx parallel_id_ ) {
	AccessOrder order =
		pd<access_date      ? AccessOrder::Before
	:	digest.idle()       ? AccessOrder::BetweenReadAndWrite
	:	pd<first_write_date ? AccessOrder::BetweenReadAndWrite
	:	pd<last_write_date  ? AccessOrder::InbetweenWrites
	:                         AccessOrder::After
	;
	if (
		( +ad.accesses || !ad.idle() )
	&&	(	order==AccessOrder::Before                                         // date becomes earlier
		||	( !digest.accesses && (digest.idle()||order<AccessOrder::Write) )  // date becomes later
		)
	) {
		if (+ad.accesses ) file_date   = dd           ;
		/**/               access_date = pd           ;
		/**/               parallel_id = parallel_id_ ;
	}
	if (!ad.idle()) {
		if      (digest.idle()            ) first_write_date = last_write_date = pd ;
		else if (order==AccessOrder::After)                    last_write_date = pd ;
		else if (order< AccessOrder::Write) first_write_date                   = pd ;
	}
	//
	AccessDigest old_ad = digest ;                                                                                   // for trace only
	digest.update(ad,order) ;                                                                                        // execute actions in actual order as provided by dates
	SWEAR( !( (old_ad.neg_tflags|old_ad.pos_tflags) & ~(digest.neg_tflags|digest.pos_tflags) ) , old_ad , digest ) ; // digest.tflags must become less and less transparent
	tflags = ( tflags & ~digest.neg_tflags ) | digest.pos_tflags ;                                                   // thus we can recompute new tfs from old value
}

//
// GatherDeps
//

::ostream& operator<<( ::ostream& os , GatherDeps const& gd ) {
	/**/             os << "GatherDeps(" << gd.accesses ;
	if (gd.seen_tmp) os <<",seen_tmp"                   ;
	return           os << ')'                          ;
}

bool/*new*/ GatherDeps::_new_access( PD pd , ::string const& file , DD dd , AccessDigest const& ad , NodeIdx parallel_id_ , ::string const& comment ) {
	SWEAR( !file.empty() , comment ) ;
	AccessInfo* info   = nullptr/*garbage*/    ;
	auto        it     = access_map.find(file) ;
	bool        is_new = it==access_map.end()  ;
	if (is_new) {
		access_map[file] = accesses.size() ;
		accesses.emplace_back(file,AccessInfo(pd,tflags_cb(file))) ;
		info = &accesses.back().second ;
	} else {
		info = &accesses[it->second].second ;
	}
	AccessInfo old_info = *info ;                                              // for tracing only
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info->update(pd,dd,ad,parallel_id_) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (*info!=old_info) Trace("_new_access", STR(is_new) , pd , file , dd , ad , parallel_id , comment , *info ) ; // only trace if something changes
	return is_new ;
}

void GatherDeps::static_deps( PD pd , ::vmap_s<DepDigest> const& static_deps , ::string const& stdin ) {
	SWEAR( accesses.empty() , accesses ) ;                                                             // ensure we do not insert static deps after hidden ones
	parallel_id++ ;
	for( auto const& [f,d] : static_deps )
		if (f==stdin) _new_access( pd , f , file_date(f)              , {d.accesses|Access::Reg,d.dflags} , parallel_id , "stdin"       ) ;
		else          _new_access( pd , f , +d.accesses?d.date():DD() , {d.accesses            ,d.dflags} , parallel_id , "static_deps" ) ;
}

void GatherDeps::new_exec( PD pd , ::string const& exe , ::string const& c ) {
	Disk::RealPath              rp { autodep_env }                    ;
	Disk::RealPath::SolveReport sr = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(sr) ) {
		DD       dd = file_date(f) ;
		new_dep( pd , ::move(f) , dd , a , {} , c ) ;
	}
}

ENUM( GatherDepsKind , Stdout , Stderr , ServerReply , ChildEnd , Master , Slave )
struct ServerReply {
	IMsgBuf buf ;                      // buf to assemble the reply
	Fd      fd  ;                      // fd to forward reply to
} ;

static inline void _set_status( Status& status , Status new_status ) {
	if (status==Status::New) status = new_status ;                             // else there is already another reason
}

void _child_wait_thread_func( int* wstatus , pid_t pid , Fd fd ) {
	static constexpr uint64_t One = 1 ;
	do { ::waitpid(pid,wstatus,0) ; } while (WIFSTOPPED(*wstatus)) ;
	swear_prod(::write(fd,&One,8)==8,"cannot report child wstatus",wstatus) ;
}

Status GatherDeps::exec_child( ::vector_s const& args , Fd child_stdin , Fd child_stdout , Fd child_stderr ) {
	using Kind = GatherDepsKind ;
	using Proc = JobExecRpcProc ;
	Trace trace("exec_child",STR(create_group),method,autodep_env,args) ;
	if (env) trace("env",*env) ;
	Child child ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service  = master_fd.service(addr) ;
	autodep_env.root_dir = *g_root_dir             ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	::map_ss add_env {{"LMAKE_AUTODEP_ENV",autodep_env}} ;                     // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	{	::unique_lock lock{_pid_mutex} ;
		if (killed) return Status::Killed ;                                    // dont start if we are already killed before starting
		if (method==AutodepMethod::Ptrace) {
			// XXX : splitting responsibility is no more necessary. Can directly report child termination from within autodep_ptrace.process using same ifce as _child_wait_thread_func
			// we split the responsability into 2 processes :
			// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
			// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
			bool in_parent = child.spawn( create_group/*as_group*/ , {} , child_stdin , child_stdout , child_stderr ) ;
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
				grand_child.waited() ;                                             // grand_child has already been waited
				if      (WIFEXITED  (wstatus)) ::_exit(WEXITSTATUS(wstatus)) ;
				else if (WIFSIGNALED(wstatus)) ::_exit(2                   ) ;
				fail_prod("ptraced child did not exit and was not signaled : wstatus : ",wstatus) ;
			}
		} else {
			if (method>=AutodepMethod::Ld) {
				bool     is_audit = method==AutodepMethod::LdAudit ;
				::string env_var  ;
				//
				if (is_audit) { env_var = "LD_AUDIT"   ; add_env[env_var] = *g_lmake_dir+"/_lib/ld_audit.so"   ; }
				else          { env_var = "LD_PRELOAD" ; add_env[env_var] = *g_lmake_dir+"/_lib/ld_preload.so" ; }
				//
				if (env) { if (env->contains(env_var)) add_env[env_var] += ':' + env->at(env_var) ; }
				else     { if (has_env      (env_var)) add_env[env_var] += ':' + get_env(env_var) ; }
			}
			new_exec( PD::s_now() , args[0] ) ;
			try {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				child.spawn(
					create_group , args
				,	child_stdin  , child_stdout , child_stderr
				,	env          , &add_env
				,	chroot
				,	cwd
				) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} catch(::string const& e) {
				if (child_stderr==Child::Pipe) stderr = e ;
				else                           child_stderr.write(e) ;
				return Status::EarlyErr ;
			}
			trace("pid",child.pid) ;
		}
		pid = child.pid ;
	}
	//
	Fd                                child_fd           = ::eventfd(0,EFD_CLOEXEC)                                    ;
	::jthread                         wait_jt            { _child_wait_thread_func , &wstatus , child.pid , child_fd } ; // thread dedicated to wating child
	Epoll                             epoll              { New }                                                       ;
	Status                            status             = Status::New                                                 ;
	PD                                end                ;
	::umap<Fd         ,IMsgBuf      > slaves             ;
	::umap<Fd/*reply*/,ServerReply  > server_replies     ;
	::umap<Fd         ,JobExecRpcReq> delayed_check_deps ;                                  // check_deps events are delayed to ensure all previous deps are taken into account
	auto handle_req_to_server = [&]( Fd fd , JobExecRpcReq&& jerr ) -> bool/*still_sync*/ {
		trace("slave",fd,jerr) ;
		reorder() ;                                        // ensure server sees a coherent view
		Proc   proc     = jerr.proc               ;        // capture essential info before moving to server_cb
		size_t sz       = jerr.files.size()       ;        // .
		bool   sync_    = jerr.sync               ;        // .
		Fd     reply_fd = server_cb(::move(jerr)) ;
		trace("reply",reply_fd) ;
		if (reply_fd) {
			epoll.add_read(reply_fd,Kind::ServerReply) ;
			server_replies[reply_fd].fd = sync_ ? fd : Fd() ;
		} else if (jerr.sync) {
			JobExecRpcReply sync_reply ;
			sync_reply.proc  = proc ;
			sync_reply.ok    = Yes                                          ; // try to mimic server as much as possible when none is available
			sync_reply.infos = ::vector<pair<Bool3/*ok*/,Crc>>(sz,{Yes,{}}) ; // .
			sync(fd,sync_reply) ;
		}
		return false ;
	} ;
	//
	if (+timeout                 ) end = PD::s_now() + timeout ;
	if (child_stdout==Child::Pipe) epoll.add_read(child.stdout,Kind::Stdout  ) ;
	if (child_stderr==Child::Pipe) epoll.add_read(child.stderr,Kind::Stderr  ) ;
	/**/                           epoll.add_read(child_fd    ,Kind::ChildEnd) ;
	/**/                           epoll.add_read(master_fd   ,Kind::Master  ) ;
	while (epoll.cnt) {
		uint64_t wait_ns = Epoll::Forever ;
		if (+end) {
			PD now = PD::s_now() ;
			if (now>=end) {
				_set_status(status,Status::Timeout) ;
				kill_job_cb() ;
			}
			wait_ns = (end-now).nsec() ;
		}
		if (!delayed_check_deps.empty()) wait_ns = 0 ;
		::vector<Epoll::Event> events = epoll.wait(wait_ns) ;
		if ( events.empty() && !delayed_check_deps.empty() ) {                                  // process delayed check deps after all other events
			for( auto& [fd,jerr] : delayed_check_deps ) handle_req_to_server(fd,::move(jerr)) ;
			delayed_check_deps.clear() ;
			continue ;
		}
		for( Epoll::Event const& event : events ) {
			Kind kind = event.data<Kind>() ;
			Fd   fd   = event.fd()         ;
			SWEAR(!delayed_check_deps.contains(fd)) ;                          // while we are waiting for an answer, we should not be receiving any more requests
			switch (kind) {
				case Kind::Stdout :
				case Kind::Stderr : {
					char          buf[4096] ;
					int           cnt       = ::read( fd , buf , sizeof(buf) ) ; SWEAR( cnt>=0 , cnt ) ;
					::string_view buf_view  { buf , size_t(cnt) }                                      ;
					if      (!cnt              ) { trace("close",kind) ; epoll.close(fd) ;           }
					else if (kind==Kind::Stderr) { stderr.append(buf_view) ;                         }
					else                         { stdout.append(buf_view) ; live_out_cb(buf_view) ; }
				} break ;
				case Kind::ChildEnd : {
					uint64_t one = 0/*garbage*/            ;
					int      cnt = ::read( fd , &one , 8 ) ; SWEAR( cnt==8 && one==1 , cnt , one ) ;
					{	::unique_lock lock{_pid_mutex} ;
						child.pid = -1 ;                                       // too late to kill job
					}
					if      (WIFEXITED  (wstatus)) _set_status( status , WEXITSTATUS(wstatus)!=0       ?Status::Err:Status::Ok       ) ;
					else if (WIFSIGNALED(wstatus)) _set_status( status , is_sig_sync(WTERMSIG(wstatus))?Status::Err:Status::LateLost ) ; // synchronous signals are actually errors
					else                           fail("unexpected wstatus : ",wstatus) ;
					trace("status",status,::hex,wstatus,::dec) ;
					epoll.close(fd) ;
					epoll.cnt-- ;                                              // do not wait for new connections on master socket, but if one arrives before all flows are closed, process it
				} break ;
				case Kind::Master : {
					Fd slave{master_fd.accept()} ;                             // sync to tell child we have seen the connection
					epoll.add_read(slave,Kind::Slave) ;
					slaves[slave] ;                                            // allocate entry
					trace("master",slave) ;
				} break ;
				case Kind::ServerReply : {
					JobRpcReply jrr ;
					auto it = server_replies.find(fd) ;
					SWEAR(it!=server_replies.end()) ;
					try         { if (!it->second.buf.receive_step(fd,jrr)) continue ; }
					catch (...) {                                                      } // server disappeared, give up
					trace("server_reply",fd,jrr) ;
					//                                                                                              vvvvvvvvvvvvv
					if      ( jrr.proc==JobProc::ChkDeps && jrr.ok==Maybe ) { _set_status(status,Status::ChkDeps) ; kill_job_cb() ;                            }
					else if ( +it->second.fd                              )                                         sync(it->second.fd,JobExecRpcReply(jrr)) ;
					//                                                                                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					server_replies.erase(it) ;
					epoll.close(fd) ;
				} break ;
				case Kind::Slave : {
					JobExecRpcReq jerr  ;
					try         { if (!slaves.at(fd).receive_step(fd,jerr)) continue ; }
					catch (...) {                                                      } // server disappeared, give up
					bool sync_ = jerr.sync ;                                             // capture essential info so as to be able to move jerr
					Proc proc  = jerr.proc ;                                             // .
					switch (proc) {
						case Proc::None      :                                                     goto Close ;
						case Proc::Tmp       : seen_tmp = true ; trace("slave",fd,jerr) ;          break      ;
						//                     vvvvvvvvvvvvvvvvvvv
						case Proc::Access    : _new_accesses(jerr) ;                                                         break ;
						case Proc::DepInfos  : _new_accesses(jerr) ; handle_req_to_server(fd,::move(jerr)) ; sync_ = false ; break ;      // if sync, handle_req_to_server replies
						//                     ^^^^^^^^^^^^^^^^^^^
						case Proc::ChkDeps   : delayed_check_deps[fd] = ::move(jerr) ;                       sync_ = false ; break ;      // if sync, reply is delayed as well
						case Proc::Trace     : trace("from_job",jerr.comment) ;                                              break ;
						default : fail(proc) ;
					}
					//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					if (sync_) sync( fd , JobExecRpcReply(proc) ) ;
					//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					break ;
				Close :
					trace("slave","close",proc,fd) ;
					epoll.close (fd) ;
					slaves.erase(fd) ;
				} break ;
				default : FAIL(kind) ;
			}
		}
	}
	reorder() ;                                                                // ensure server sees a coherent view
	return status ;
}

void GatherDeps::reorder() {
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	Trace trace("reorder") ;
	::stable_sort(                                                             // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b ) -> bool {
			return ::pair(a.second.access_date,a.second.parallel_id) < ::pair(b.second.access_date,b.second.parallel_id) ;
		}
	) ;
	// first pass : note stat accesses that are directories of immediately following file accesses as these are already implicit deps (through Uphill rule)
	::uset<size_t> to_del ;
	size_t         last   = Npos ;                                             // XXX : replace with a vector to manage parallel deps
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
			for( ::string dir=dir_name(file) ; !dir.empty() ; dir=dir_name(dir) )
				if (!dirs.insert(dir).second) break ;                             // all uphill dirs are already inserted if a dir has been inserted
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
