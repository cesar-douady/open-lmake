// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "hash.hh"
#include "trace.hh"
#include "time.hh"

#include "ptrace.hh"

#include "gather_deps.hh"

using namespace Time ;
using namespace Hash ;

//
// AutodepEnv
//

::ostream& operator<<( ::ostream& os , AutodepEnv const& ade ) {
	os << "AutodepEnv(" << ade.service <<','<< ade.root_dir <<',' ;
	if (ade.auto_mkdir ) os <<",auto_mkdir"  ;
	if (ade.ignore_stat) os <<",ignore_stat" ;
	return os <<','<< ade.lnk_support <<')' ;
}

//
// GatherDeps
//

::ostream& operator<<( ::ostream& os , GatherDeps const& gd ) {
	os << "GatherDeps(" << gd.accesses ;
	if (gd._nxt_order!=DepOrder::Seq) os <<','<< gd._nxt_order ;
	if (gd.seen_tmp                 ) os <<",seen_tmp"         ;
	return os << ')' ;
}

::ostream& operator<<( ::ostream& os , GatherDeps::AccessInfo const& ai ) {
	os << "AccessInfo(" << '@'<<ai.access_date ;
	if (+ai.dfs      ) os <<','<< ai.dfs                             ;
	if (+ai.neg_tfs  ) os <<'-'<< ai.neg_tfs                         ;
	if (+ai.pos_tfs  ) os <<'+'<< ai.pos_tfs                         ;
	if (+ai.file_date) os <<','<< "Read:"<<ai.file_date              ;
	if (ai.write!=No ) os <<','<< (ai.write==Maybe?"Unlink":"Write") ;
	return os <<','<< ai.dep_order << ')' ;
}

::pair<GatherDeps::AccessInfo&,bool/*created*/> GatherDeps::_info(::string const& name) {
	auto it = access_map.find(name) ;
	if (it!=access_map.end()) return { accesses[it->second].second , false } ;
	access_map[name] = accesses.size() ;
	accesses.emplace_back(name,AccessInfo()) ;
	return { accesses.back().second , true } ;
}

void GatherDeps::_new_target( PD pd , ::string const& target , bool unlink , TFs neg_tfs , TFs pos_tfs , ::string const& comment ) {
	SWEAR(!target.empty()   ) ;
	SWEAR(!(neg_tfs&pos_tfs)) ;                                                // cannot suppress and add a flag simultaneously
	auto [info,created] = _info(target)                  ;
	bool stamp          = created || pd<info.access_date ;
	//
	Bool3  old_write   = info.write   ;
	TFlags old_neg_tfs = info.neg_tfs ;
	TFlags old_pos_tfs = info.pos_tfs ;
	//
	info.write   = Maybe|!unlink                   ;                           // for the write side, last action is the significant one
	info.neg_tfs = (info.neg_tfs&~pos_tfs)|neg_tfs ;                           // flags are accumulated in order
	info.pos_tfs = (info.pos_tfs&~neg_tfs)|pos_tfs ;                           // .
	//
	if (
		stamp
	||	info.write!=old_write || info.neg_tfs!=old_neg_tfs || info.pos_tfs!=old_pos_tfs
	) Trace trace("_new_target",STR(unlink),STR(created),pd,STR(stamp),info.write,info.neg_tfs,info.pos_tfs,pd,target,comment) ;
	//
	if (!stamp) return ;                                                       // existing file has already been accessed (if file did not exist, it is not an update)
	info.access_date = pd ;
	info.file_date   = {} ;                                                    // if first access is a write, no file_date is attached
}

void GatherDeps::_new_dep( PD pd , ::string const& dep , DD dd , bool update , DFs dfs , ::string const& comment ) {
	SWEAR(!dep.empty()) ;
	auto [info,created] = _info(dep)                     ;
	bool stamp          = created || pd<info.access_date ;
	//
	if (
		( stamp                              )
	||	( info.write==No && +(dfs&~info.dfs) )
	||	( update         && info.write!=Yes  )
	) Trace trace("_new_dep",STR(update),STR(created),pd,STR(stamp),dep,dfs,dd,comment) ;
	//
	if (info.write==No) info.dfs   |= dfs ;
	if (update        ) info.write  = Yes ;
	if (!stamp        ) return ;                                               // file has already been accessed, ignore read
	info.access_date = pd                 ;
	info.dep_order   = _nxt_order         ;
	_nxt_order       = DepOrder::Parallel ;
	info.file_date   = dd ;
}

ENUM( Kind , Stdout , Stderr , ServerReply , ChildEnd , Master , Slave )
struct ServerReply {
	IMsgBuf buf ;                      // buf to assemble the reply
	Fd      fd  ;                      // fd to forward reply to
} ;

Status GatherDeps::exec_child( ::vector_s const& args , Fd child_stdin , Fd child_stdout , Fd child_stderr ) {
	using Proc = JobExecRpcProc ;
	Trace trace("exec_child",STR(create_group),autodep_method,autodep_env,args) ;
	if (env) trace("env",*env) ;
	Child child ;
	//
	if (env) swear_prod( !env->contains("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	else     swear_prod( !has_env      ("LMAKE_AUTODEP_ENV") , "cannot run lmake under lmake" ) ;
	autodep_env.service  = master_sock.service(addr) ;
	autodep_env.root_dir = *g_root_dir               ;
	//
	::map_ss add_env {{"LMAKE_AUTODEP_ENV",autodep_env}} ;                     // required even with autodep_method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	if (autodep_method==AutodepMethod::Ptrace) {
		// cannot simultaneously watch for data & child events using ptrace as SIGCHLD is not delivered for sub-processes of tracee
		// so we split the responsability into 2 processes :
		// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
		// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
		bool in_parent = child.spawn( true/*as_group*/ , {} , child_stdin , child_stdout , child_stderr ) ;
		if (!in_parent) {
			Child grand_child ;
			AutodepPtrace::s_autodep_env = autodep_env ;
			try {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				grand_child.spawn(
					false/*as_group*/ , args
				,	Fd::Stdin , Fd::Stdout , Fd::Stderr
				,	env , &add_env
				,	chroot
				,	cwd
				,	AutodepPtrace::s_prepare_child
				) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} catch(::string const& e) {
				exit(2,e) ;
			}
			trace("pid",grand_child.pid) ;
			AutodepPtrace autodep_ptrace { grand_child.pid }        ;
			int           wstatus        = autodep_ptrace.process() ;
			grand_child.waited() ;                                             // grand_child has already been waited
			if      (WIFEXITED  (wstatus)) ::_exit  (WEXITSTATUS(wstatus)) ;
			else if (WIFSIGNALED(wstatus)) kill_self(WTERMSIG   (wstatus)) ;
			fail_prod("ptraced child did not exit and was not signaled : wstatus : ",wstatus) ;
		}
	} else {
		if (autodep_method>=AutodepMethod::Ld) {
			bool     is_audit = autodep_method==AutodepMethod::LdAudit ;
			::string env_var  ;
			//
			if (is_audit) { env_var = "LD_AUDIT"   ; add_env[env_var] = *g_lmake_dir+"/_lib/autodep_ld_audit.so"   ; }
			else          { env_var = "LD_PRELOAD" ; add_env[env_var] = *g_lmake_dir+"/_lib/autodep_ld_preload.so" ; }
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
			return Status::Err ;
		}
		trace("pid",child.pid) ;
	}
	//
	Fd                              child_fd       = open_sig_fd(SIGCHLD) ;
	Epoll                           epoll          { New }                ;
	Status                          status         = Status::New          ;
	size_t                          kill_cnt       = 0                    ;    // number of times child has been killed so far
	PD                              end            ;
	::umap<Fd         ,IMsgBuf    > slaves         ;
	::umap<Fd/*reply*/,ServerReply> server_replies ;
	auto kill_job = [&](Status s)->void {
		if (status!=Status::New) return ;
		status  = s           ;
		end     = PD::s_now() ;
	} ;
	//
	if (+timeout                 ) end = PD::s_now() + timeout ;
	if (child_stdout==Child::Pipe) epoll.add_read(child.stdout,Kind::Stdout  ) ;
	if (child_stderr==Child::Pipe) epoll.add_read(child.stderr,Kind::Stderr  ) ;
	/**/                           epoll.add_read(child_fd    ,Kind::ChildEnd) ;
	/**/                           epoll.add_read(master_sock ,Kind::Master  ) ;
	while (epoll.cnt) {
		uint64_t wait_ns = Epoll::Forever ;
		if (+end) {
			PD now = PD::s_now() ;
			if (now>=end) {
				if (status==Status::New) status = Status::Timeout ;
				/**/                     end    = now + Delay(1.) ;
				//
				if (kill_cnt<kill_sigs.size()) child.kill(kill_sigs[kill_cnt++]) ;
				else                           child.kill(SIGKILL              ) ;
			}
			wait_ns = (end-now).nsec() ;
		}
		::vector<Epoll::Event> events = epoll.wait(wait_ns) ;
		for( Epoll::Event event : events ) {
			Kind kind = event.data<Kind>() ;
			Fd   fd   = event.fd()         ;
			switch (kind) {
				case Kind::Stdout :
				case Kind::Stderr : {
					char buf[4096] ;
					int  cnt       = ::read( fd , buf , sizeof(buf) ) ; SWEAR(cnt>=0) ;
					if      (!cnt              ) { epoll.close(fd) ;                                              }
					else if (kind==Kind::Stderr) { stderr.append(buf,cnt) ;                                       }
					else                         { stdout.append(buf,cnt) ; live_out_cb(::string_view(buf,cnt)) ; }
				} break ;
				case Kind::ChildEnd : {
					struct signalfd_siginfo child_info ;
					int                     cnt        = ::read( fd , &child_info , sizeof(child_info) ) ;
					SWEAR(cnt==sizeof(child_info)) ;
					wstatus = child.wait() ;
					if (status==Status::New) {
						if (WIFEXITED(wstatus)) {
							if (WEXITSTATUS(wstatus)!=0       ) status = Status::Err     ;
							else                                status = Status::Ok      ;
						} else if (WIFSIGNALED(wstatus)) {
							if (is_sig_sync(WTERMSIG(wstatus))) status = Status::Err     ; // synchronous signal : actually an error
							else                                status = Status::Killed  ;
						} else {
							fail("unexpected wstatus : ",wstatus) ;
						}
					}
					trace("status",status) ;
					epoll.close(fd) ;
					epoll.cnt-- ;                                              // do not wait for new connections on master socket, but if one arrives before all flows are closed, process it
				} break ;
				case Kind::Master : {
					Fd slave{master_sock.accept()} ;
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
					trace("server_reply",jrr) ;
					//                                                      vvvvvvvvvvvvvvvvvvvvvvvvv
					if      ( jrr.proc==JobProc::ChkDeps && jrr.ok==Maybe ) kill_job(Status::ChkDeps) ;
					else if ( +it->second.fd                              ) sync(it->second.fd,JobExecRpcReply(jrr)) ;
					//                                                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					server_replies.erase(it) ;
					epoll.close(fd) ;
				} break ;
				case Kind::Slave : {
					JobExecRpcReq jerr  ;
					try         { if (!slaves.at(fd).receive_step(fd,jerr)) continue ; }
					catch (...) {                                                      } // server disappeared, give up
					bool            sync_      = jerr.sync ;                             // capture essential info so as to be able to move jerr
					Proc            proc       = jerr.proc ;                             // .
					JobExecRpcReply sync_reply ;
					switch (proc) {
						case Proc::None            :                                                            goto Close ;
						case Proc::Tmp             : seen_tmp   = true               ; trace("slave",fd,jerr) ; break      ;
						case Proc::CriticalBarrier : _nxt_order = DepOrder::Critical ; trace("slave",fd,jerr) ; break      ;
						case Proc::Heartbeat       :                                                            goto Close ; //           accept & read is enough to report liveness
						//                           vvvvvvvvvvvvvvvvvvvvvvvv
						case Proc::Kill            : kill_job(Status::Killed) ;                                 goto Close ; // no reply, accept & read is enough to ack
						//                   vvvvvvvv------------------------vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						case Proc::Targets : _new_targets( jerr.date , mk_key_vector(jerr.files) , false/*unlink*/ , jerr.neg_tfs , jerr.pos_tfs , jerr.comment ) ; break ; // file dates are only...
						case Proc::Unlinks : _new_targets( jerr.date , mk_key_vector(jerr.files) , true /*unlink*/ , jerr.neg_tfs , jerr.pos_tfs , jerr.comment ) ; break ; // for deps
						case Proc::Updates : _new_deps   ( jerr.date ,               jerr.files  , true /*update*/ , jerr.dfs                    , jerr.comment ) ; break ;
						case Proc::Deps    : _new_deps   ( jerr.date ,               jerr.files  , false/*update*/ , jerr.dfs                    , jerr.comment ) ; break ;
						//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						case Proc::DepInfos :
							//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
							_new_deps( jerr.date , jerr.files , false/*update*/ , jerr.dfs , jerr.comment ) ; // getting the crc is a dependence on a file
							//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							/*fall through*/
						case Proc::ChkDeps : {
							size_t sz = jerr.files.size() ;                    // capture essential info before moving to server_cb
							trace("slave",fd,jerr) ;
							Fd reply_fd = server_cb(::move(jerr)) ;
							trace("reply",reply_fd) ;
							if (!reply_fd) {
								sync_reply.proc  = proc                                                   ; // try to mimic server as much as possible when none is available
								sync_reply.ok    = Yes                                                    ; // .
								sync_reply.infos = ::vector<pair<Bool3/*ok*/,Crc>>(sz,{Yes,Crc::Unknown}) ; // .
							} else {
								epoll.add_read(reply_fd,Kind::ServerReply) ;
								server_replies[reply_fd].fd = sync_ ? fd : Fd() ;
								sync_ = false ;                                   // do sync when we have the reply from server
							}
						} break ;
						case Proc::Trace : trace("from_job",jerr.comment) ; break ;
						default : fail(proc) ;
					}
					//         vvvvvvvvvvvvvvvvvvv
					if (sync_) sync(fd,sync_reply) ;
					//         ^^^^^^^^^^^^^^^^^^^
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
	return status ;
}
