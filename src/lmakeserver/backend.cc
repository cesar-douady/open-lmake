// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "config.hh"

#include "core.hh"

using namespace Disk   ;
using namespace Time   ;
using namespace Engine ;

namespace Backends {

	ENUM( EventKind , Master , Stop , Slave )

	::latch                                   Backend::s_service_ready          { 1 } ;
	::string                                  Backend::s_executable             ;
	Backend*                                  Backend::s_tab[+Tag::N]           ;
	ServerSockFd                              Backend::s_server_fd              ;
	::mutex                                   Backend::_s_mutex                 ;
	::umap<JobIdx,Backend::StartTabEntry>     Backend::_s_start_tab             ;
	SmallIds<SmallId>                         Backend::_s_small_ids             ;
	ThreadQueue<Backend::DeferredReportEntry> Backend::_s_deferred_report_queue ;
	ThreadQueue<Backend::DeferredLostEntry  > Backend::_s_deferred_lost_queue   ;

	::ostream& operator<<( ::ostream& os , Backend::StartTabEntry const& ste ) {
		return os << "StartTabEntry(" << ste.conn <<','<< ste.tag <<','<< ste.reqs <<','<< ste.submit_attrs << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Backend::StartTabEntry::Conn const& c ) {
		return os << "Conn(" << hex<<c.job_addr<<dec <<':'<< c.job_port <<','<< c.seq_id <<','<< c.small_id << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Backend::DeferredReportEntry const& dre ) {
		return os << "DeferredReportEntry(" << dre.date <<':'<< dre.seq_id <<','<< dre.job_exec << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Backend::DeferredLostEntry const& dle ) {
		return os << "DeferredReportEntry(" << dle.date <<','<< dle.seq_id <<','<< dle.job << ')' ;
	}

	void Backend::s_submit( Tag tag , JobIdx ji , ReqIdx ri , SubmitAttrs&& submit_attrs , ::vmap_ss&& rsrcs ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_submit",tag,ji,ri,submit_attrs,rsrcs) ;
		//
		if ( Req(ri)->options.flags[ReqFlag::Local] && tag!=Tag::Local ) {
			SWEAR(+tag<+Tag::N) ;                                                           // prevent compiler array bound warning in next statement
			rsrcs = s_tab[+tag]->mk_lcl( ::move(rsrcs) , s_tab[+Tag::Local]->capacity() ) ;
			tag   = Tag::Local                                                            ;
		}
		//
		submit_attrs.tag = tag ;
		s_tab[+tag]->submit(ji,ri,submit_attrs,::move(rsrcs)) ;
	}

	void Backend::s_add_pressure( Tag t , JobIdx j , ReqIdx r , SubmitAttrs const& sa ) {
		if (Req(r)->options.flags[ReqFlag::Local]) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_add_pressure",t,j,r,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) {
			s_tab[+t]->add_pressure(j,r,sa) ;                               // if job is not started, ask sub-backend to raise its priority
		} else {
			it->second.reqs.insert(r) ;                                        // else, job is already started, note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;                                    // and update submit_attrs in case job was not actually started
		}
	}

	void Backend::s_set_pressure( Tag t , JobIdx j , ReqIdx r , SubmitAttrs const& sa ) {
		if (Req(r)->options.flags[ReqFlag::Local]) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_set_pressure",t,j,r,sa) ;
		s_tab[+t]->set_pressure(j,r,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->set_pressure(j,r,sa) ;       // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;            // and update submit_attrs in case job was not actually started
	}

	void Backend::s_launch() {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_launch") ;
		for( Tag t : Tag::N )
			try {
				s_tab[+t]->launch() ;
			} catch (::vmap<JobIdx,pair_s<vmap_ss/*rsrcs*/>>& err_list) {
				for( auto& [ji,re] : err_list ) {
					JobExec je{ji,ProcessDate::s_now()} ;
					g_engine_queue.emplace( JobProc::Start , JobExec(je) , false/*report*/                                                                  ) ;
					g_engine_queue.emplace( JobProc::End   , ::move (je) , ::move(re.second) , JobDigest{.status=Status::EarlyErr,.stderr=::move(re.first)} ) ;
				}
			}
	}

	void Backend::_s_wakeup_remote( JobIdx job , StartTabEntry::Conn const& conn , JobExecRpcProc proc ) {
		Trace trace("_s_wakeup_remote",job,conn,proc) ;
		try {
			// as job_exec is not waiting for this message, pretend we are the job, so use JobExecRpcReq instead of JobRpcReply
			OMsgBuf().send( ClientSockFd(conn.job_addr,conn.job_port) , JobExecRpcReq(proc) ) ;
		} catch (...) {
			trace("no_job") ;
			// if job cannot be connected to, assume it is dead and pretend it died after network_delay to give a chance to report if is already completed
			{	::unique_lock lock { _s_mutex } ;                                                            // lock _s_start_tab for minimal time to avoid dead-locks
				auto it = _s_start_tab.find(job) ;                                                           // get job entry
				if (it==_s_start_tab.end()             ) return ;                                            // too late, job has already been reported
				if (conn.seq_id!=it->second.conn.seq_id) return ;                                            // .
				_s_deferred_lost_queue.emplace( Date::s_now()+g_config.network_delay , conn.seq_id , job ) ;
				it->second.state = ConnState::Lost ;                                                         // mark entry so it is not reported several times
			}
		}
	}

	void Backend::_s_deferred_report_thread_func(::stop_token stop) {
		Trace::t_key = 'S' ;
		Trace trace("_s_deferred_report_thread_func") ;
		for(;;) {
			auto [popped,info] = _s_deferred_report_queue.pop(stop) ;
			if (!popped                     ) break ;
			if (!info.date.sleep_until(stop)) break ;
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			//
			{	::unique_lock lock { _s_mutex }                        ;       // lock _s_start_tab for minimal time to avoid dead-locks
				auto          it   = _s_start_tab.find(+info.job_exec) ;
				if (it==_s_start_tab.end()             ) continue ;
				if (it->second.conn.seq_id!=info.seq_id) continue ;
			}
			g_engine_queue.emplace( JobProc::ReportStart , ::move(info.job_exec) ) ;
		}
		trace("done") ;
	}

	void Backend::_s_deferred_lost_thread_func(::stop_token stop) {
		Trace::t_key = 'L' ;
		Trace trace("_s_deferred_lost_thread_func") ;
		for(;;) {
			auto [popped,info] = _s_deferred_lost_queue.pop(stop) ;
			if (!popped                     ) break ;
			if (!info.date.sleep_until(stop)) break ;
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			Status s = Status::Lost ;
			{	::unique_lock lock { _s_mutex }                  ;
				auto          it   = _s_start_tab.find(info.job) ;
				if (it==_s_start_tab.end()) { trace("completed",info) ; continue ; } // since we decided that job is lost, it finally completed, ignore
				trace("lost",info,it->second.submit_attrs.n_retries) ;
				s = it->second.lost() ;
			}
			::string host = deserialize<JobInfoStart>(IFStream(Job(info.job).ancillary_file())).pre_start.host ;
			_s_handle_job_req( JobRpcReq( JobProc::End , info.seq_id , info.job , host , JobDigest{.status=s,.stderr="vanished after start"} ) ) ;
		}
		trace("done") ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_req( JobRpcReq && jrr , Fd fd ) {
		switch (jrr.proc) {
			case JobProc::None     :              return false ;               // if connection is lost, ignore it
			case JobProc::Start    : SWEAR(+fd) ; break        ;               // fd is needed to reply
			case JobProc::LiveOut  :
			case JobProc::End      :
			case JobProc::ChkDeps  :
			case JobProc::DepInfos :              break        ;
			default : FAIL(jrr.proc) ;
		}
		Job                         job                { jrr.job                  } ;
		JobExec                     job_exec           { job , ::string(jrr.host) } ; // keep jrr intact for recording
		Rule                        rule               = job->rule                  ;
		JobRpcReply                 reply              { JobProc::Start           } ;
		::vector<Node>              report_unlink      ;
		StartCmdAttrs               start_cmd_attrs    ;
		::string                    cmd                ;
		::vmap_s<pair_s<AccDflags>> create_match_attrs ;
		StartRsrcsAttrs             start_rsrcs_attrs  ;
		StartNoneAttrs              start_none_attrs   ;
		::string                    start_exc_txt      ;
		ProcessDate                 eta                ;
		SubmitAttrs                 submit_attrs       ;
		::vmap_ss                   rsrcs              ;
		::string                    backend_msg        ;
		Trace trace("_s_handle_job_req",jrr,job_exec) ;
		{	::unique_lock  lock  { _s_mutex } ;                                // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			auto           it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartTabEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			trace("entry",entry) ;
			switch (jrr.proc) {
				case JobProc::Start : {
					job_exec.start = Date::s_now()      ;
					submit_attrs   = entry.submit_attrs ;
					//                            vvvvvvvvvvvvvvvvvvvvvvv
					tie(backend_msg,entry.reqs) = s_start(entry.tag,+job) ;
					entry.start                 = job_exec.start          ;
					//                            ^^^^^^^^^^^^^^
					// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
					Rule::SimpleMatch match_ = job.simple_match() ;
					try {
						start_none_attrs = rule->start_none_attrs.eval(match_,entry.rsrcs) ;
					} catch (::string const& e) {
						start_none_attrs = rule->start_none_attrs.spec ;
						start_exc_txt    = e                           ;
					}
					bool create_match_attrs_passed = false                     ;
					bool cmd_attrs_passed          = false                     ;
					bool cmd_passed                = false                     ;
					bool keep_tmp                  = start_none_attrs.keep_tmp ;
					{	::unique_lock lock{Req::s_reqs_mutex} ;                  // ensure Req::s_store is not reallocated while we walk
						for( ReqIdx r : entry.reqs ) {
							Req req{r} ;
							keep_tmp |= req->options.flags[ReqFlag::KeepTmp]              ;
							eta       = +eta ? ::min(eta,req->stats.eta) : req->stats.eta ;
						}
					}
					try {
						create_match_attrs = rule->create_match_attrs.eval(match_            ) ; create_match_attrs_passed = true ;
						start_cmd_attrs    = rule->start_cmd_attrs   .eval(match_,entry.rsrcs) ; cmd_attrs_passed          = true ;
						cmd                = rule->cmd               .eval(match_,entry.rsrcs) ; cmd_passed                = true ;
						start_rsrcs_attrs  = rule->start_rsrcs_attrs .eval(match_,entry.rsrcs) ;
					} catch (::string const& e) {
						_s_small_ids.release(entry.conn.small_id) ;
						trace("erase_start_tab",job,it->second,STR(cmd_attrs_passed),STR(cmd_passed),e) ;
						Tag       tag   = entry.tag           ;
						::vmap_ss rsrcs = ::move(entry.rsrcs) ;
						_s_start_tab.erase(it) ;
						job_exec.host.clear() ;
						::string err_str = to_string(
								cmd_passed                ? rule->start_rsrcs_attrs .s_exc_msg(false/*using_static*/)
							:	cmd_attrs_passed          ? rule->cmd               .s_exc_msg(false/*using_static*/)
							:	create_match_attrs_passed ? rule->start_cmd_attrs   .s_exc_msg(false/*using_static*/)
							:	                            rule->create_match_attrs.s_exc_msg(false/*using_static*/)
						,	'\n'
						,	e
						) ;
						s_end(tag,+job) ;
						JobDigest digest { .status=Status::EarlyErr , .stderr=::move(err_str) }  ;
						for( auto const& [k,daf] : create_match_attrs ) {
							auto const& [d,af] = daf ;
							if (+af.accesses) digest.deps.emplace_back( d , DepDigest( af.accesses , af.dflags , true/*parallel*/ , file_date(d) ) ) ; // if dep is accessed, pretend it is now
							else              digest.deps.emplace_back( d , DepDigest( af.accesses , af.dflags , true/*parallel*/                ) ) ;
						}
						trace("early_err",digest) ;
						{	OFStream ofs { dir_guard(job.ancillary_file()) } ;
							serialize( ofs , JobInfoStart({ .eta=eta , .submit_attrs=submit_attrs , .pre_start=jrr , .start=reply , .backend_msg=backend_msg }) ) ;
							serialize( ofs , JobInfoEnd  ( JobRpcReq( JobProc::End , {} , jrr.job , {} , digest )                                             ) ) ;
						}
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Start , ::move(job_exec) , false/*report_now*/ , report_unlink , start_exc_txt ) ;
						g_engine_queue.emplace( JobProc::End   , ::move(job_exec) , ::move(rsrcs) , ::move(digest)                      ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						return false ;
					}
					//
					::vector_s targets  = match_.targets()       ;
					in_addr_t  job_addr = fd.peer_addr()         ;
					SmallId    small_id = _s_small_ids.acquire() ;
					::string   tmp_dir  = keep_tmp ? to_string(*g_root_dir,'/',job.ancillary_file(AncillaryTag::KeepTmp)) : to_string(g_config.remote_admin_dir,"/job_tmp/",small_id) ;
					//
					for( ::pair_ss const& kv : start_cmd_attrs  .env ) reply.env.push_back(kv) ;
					for( ::pair_ss const& kv : start_rsrcs_attrs.env ) reply.env.push_back(kv) ;
					for( ::pair_ss const& kv : start_none_attrs .env ) reply.env.push_back(kv) ;
					// simple attrs
					reply.addr                    = job_addr                    ;
					reply.autodep_env.auto_mkdir  = start_cmd_attrs.auto_mkdir  ;
					reply.autodep_env.ignore_stat = start_cmd_attrs.ignore_stat ;
					reply.autodep_env.lnk_support = g_config.lnk_support        ;
					reply.autodep_env.src_dirs_s  = g_config.src_dirs_s         ;
					reply.autodep_env.root_dir    = *g_root_dir                 ;
					reply.chroot                  = start_cmd_attrs.chroot      ;
					reply.cmd                     = ::move(cmd)                 ;
					reply.cwd_s                   = rule->cwd_s                 ;
				//	reply.env                                                      // done above
					reply.hash_algo               = g_config.hash_algo          ;
				//	reply.host                                                     // directly filled in job_exec
					reply.interpreter             = start_cmd_attrs.interpreter ;
				//	reply.job_id                                                   // directly filled in job_exec
					reply.keep_tmp                = keep_tmp                    ;
					reply.kill_sigs               = start_none_attrs.kill_sigs  ;
					reply.live_out                = entry.submit_attrs.live_out ;
					reply.local_mrkr              = start_cmd_attrs.local_mrkr  ;
					reply.method                  = start_cmd_attrs.method      ;
				//	reply.seq_id                                                   // directly filled in job_exec
					reply.small_id                = small_id                    ;
					reply.timeout                 = start_rsrcs_attrs.timeout   ;
					reply.remote_admin_dir        = g_config.remote_admin_dir   ;
					reply.job_tmp_dir             = ::move(tmp_dir)             ;
					// fancy attrs
					if ( rule->stdin_idx !=Rule::NoVar && +job->deps[rule->stdin_idx] ) reply.stdin  = create_match_attrs[rule->stdin_idx ].second.first ;
					if ( rule->stdout_idx!=Rule::NoVar                                ) reply.stdout = targets           [rule->stdout_idx]              ;
					//
					reply.targets.reserve(targets.size()) ;
					for( VarIdx t=0 ; t<targets.size() ; t++ ) if (!targets[t].empty()) reply.targets.emplace_back( targets[t] , false/*is_native_star:garbage*/ , rule->tflags(t) ) ;
					//
					reply.static_deps.reserve(create_match_attrs.size()) ;
					for( auto const& [k,daf] : create_match_attrs ) {
						auto const& [d,af] = daf ;
						if (+af.accesses) reply.static_deps.emplace_back( d , DepDigest(af.accesses,af.dflags,true/*parallel*/,Node(d)->date) ) ; // job_exec only handle dates, not crc
						else              reply.static_deps.emplace_back( d , DepDigest(af.accesses,af.dflags,true/*parallel*/              ) ) ;
					}
					//
					report_unlink       = job.wash(match_) ;
					entry.conn.job_addr = job_addr         ;
					entry.conn.job_port = jrr.port         ;
					entry.conn.small_id = small_id         ;
				} break ;
				case JobProc::End : {
					rsrcs = ::move(entry.rsrcs) ;
					job_exec.start = entry.start ;
					_s_small_ids.release(entry.conn.small_id) ;
					trace("erase_start_tab",job,it->second) ;
					Tag tag = entry.tag ;
					if (jrr.digest.status>Status::Garbage) _s_start_tab.erase(it) ;
					else                                   it->second.clear()     ; // retain entry so counting down number of retries goes on
					//            vvvvvvvvvvvvvvv
					backend_msg = s_end(tag,+job) ;
					//            ^^^^^^^^^^^^^^^
				} break ;
				default : ;
			}
		}
		trace("info") ;
		bool keep_fd = false ;
		switch (jrr.proc) {
			case JobProc::Start : {
				//vvvvvvvvvvvvvvvvvvvvvv
				OMsgBuf().send(fd,reply) ;
				//^^^^^^^^^^^^^^^^^^^^^^
				serialize( OFStream(dir_guard(job.ancillary_file())) , JobInfoStart({.eta=eta,.submit_attrs=submit_attrs,.pre_start=jrr,.start=reply,.backend_msg=backend_msg}) ) ;
				//
				bool deferred_start_report = Delay(job->exec_time)<start_none_attrs.start_delay && report_unlink.empty() && start_exc_txt.empty() ; // dont defer if we must report info at start time
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , JobExec(job_exec) , !deferred_start_report , report_unlink , start_exc_txt ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (deferred_start_report) _s_deferred_report_queue.emplace( job_exec.start+start_none_attrs.start_delay , jrr.seq_id , ::move(job_exec) ) ;
				trace("started",reply) ;
			} break ;
			case JobProc::ChkDeps  : //                                              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobProc::DepInfos : trace("deps",jrr.proc,jrr.digest.deps.size()) ; g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.digest.deps) , fd ) ; keep_fd = true ; break ;
			case JobProc::LiveOut  :                                                 g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.txt)              ) ;                  break ;
			//                                                                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			case JobProc::End :
				serialize( OFStream(job.ancillary_file(),::ios::app) , JobInfoEnd(jrr,::move(backend_msg)) ) ;
				job.end_exec() ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(rsrcs) , ::move(jrr.digest) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			break ;
			default : FAIL(jrr.proc) ;
		}
		return keep_fd ;
	}

	void Backend::_s_job_exec_thread_func(::stop_token stop) {
		static constexpr uint64_t One = 1 ;
		Trace::t_key = 'J' ;
		AutoCloseFd        stop_fd = ::eventfd(0,O_CLOEXEC) ;
		Epoll              epoll   { New }                  ;
		::stop_callback    stop_cb { stop , [&](){ SWEAR(::write(stop_fd,&One,sizeof(One))==sizeof(One)) ; } } ; // transform request_stop into an event Epoll can wait for
		::umap<Fd,IMsgBuf> slaves  ;
		//
		s_server_fd.listen() ;
		Trace trace("_s_job_exec_func",s_server_fd.port()) ;
		s_service_ready.count_down() ;
		//
		epoll.add_read(s_server_fd,EventKind::Master) ;
		epoll.add_read(stop_fd    ,EventKind::Stop  ) ;
		for(;;) {
			trace("wait") ;
			::vector<Epoll::Event> events = epoll.wait() ;                     // wait for 1 event, no timeout
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			for( Epoll::Event event : events ) {
				EventKind    kind = event.data<EventKind>() ;
				Fd           fd   = event.fd()              ;
				trace("waited",fd,kind) ;
				switch (kind) {
					case EventKind::Master : {
						SlaveSockFd slave_fd{s_server_fd.accept()} ;
						trace("new_req",slave_fd) ;
						epoll.add_read(slave_fd,EventKind::Slave) ;
						slaves.try_emplace(::move(slave_fd)) ;
					} break ;
					case EventKind::Stop : {
						uint64_t _ ;
						SWEAR(::read(fd,&_,sizeof(_))==sizeof(_)) ;
						for( auto const& [sfd,_] : slaves ) epoll.close(sfd) ;
						trace("done") ;
						return ;
					} break ;
					case EventKind::Slave : {
						JobRpcReq   jrr  ;
						try         { if (!slaves.at(fd).receive_step(fd,jrr)) { trace("partial") ; continue ; } }
						catch (...) {                                            trace("bad_msg") ; continue ;   } // ignore malformed messages, maybe job_exec died for some reasons
						//
						epoll.del(fd) ;                                        // _s_handle_job_req may trigger fd being closed by main thread, hence epoll.del must be done before
						slaves.erase(fd) ;
						if (!_s_handle_job_req(::move(jrr),fd)) { fd.close() ; } // close fd if not requested to keep it
					} break ;
					default : FAIL(kind) ;
				}
			}
		}
	}

	void Backend::_s_heartbeat_thread_func(::stop_token stop) {
		if (!g_config.heartbeat) return ;
		Trace::t_key = 'H' ;
		Trace trace("_heartbeat_thread_func") ;
		for(;;) {
			trace("sleep",g_config.heartbeat,ProcessDate::s_now()) ;
			if (!g_config.heartbeat.sleep_for(stop)) {
				trace("done") ;
				return ;
			}
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			trace("slept",ProcessDate::s_now()) ;
			::umap<JobIdx,StartTabEntry::Conn> to_wakeup ;
			::vmap<JobIdx,pair_s<bool/*err*/>> missings  = s_heartbeat() ;     // check jobs that have been submitted but have not started yet
			Date                               now       = Date::s_now() ;
			{	::unique_lock lock { _s_mutex } ;                              // lock _s_start_tab for minimal time to avoid dead-locks
				for( auto& [j,he] : missings ) {
					auto it = _s_start_tab.find(j) ;
					if (it==_s_start_tab.end()) continue ;
					trace("erase_start_tab",j,it->second) ;
					Status    s     = he.second ? Status::EarlyErr : it->second.lost() ;
					::vmap_ss rsrcs = it->second.rsrcs                                 ;
					if (s>Status::Garbage) _s_start_tab.erase(it) ;
					JobExec je{j,now} ;
					// signal jobs that have disappeared so they can be relaunched or reported in error
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::Start , ::move(je) , false/*report_now*/ , ::vector<Node>()                        ) ;
					g_engine_queue.emplace( JobProc::End   , ::move(je) , ::move(rsrcs) , JobDigest{.status=s,.stderr=::move(he.first)} ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				}
				for( auto& [j,e] : _s_start_tab ) {
					if (!e.start                ) continue ;                   // if job has not started yet, it is the responsibility of the sub-backend to monitor it
					switch (e.state) {
						case ConnState::New  : e.state = ConnState::Old ; break ; // dont check new jobs to save resources
						case ConnState::Old  : to_wakeup[j] = e.conn    ; break ; // make a shadow to avoid too long a lock
						case ConnState::Lost :                            break ; // already reported
						default : FAIL(e.state) ;
					}
				}
			}
			// check jobs that have already started
			//                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			for( auto const& [j,c] : to_wakeup ) _s_wakeup_remote(j,c,JobExecRpcProc::Heartbeat) ;
			//                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}

	::vmap<JobIdx,pair_s<bool/*err*/>> Backend::s_heartbeat() {
		::vmap<JobIdx,pair_s<bool/*err*/>> res  ;
		::unique_lock                      lock { _s_mutex } ;
		//
		Trace trace("s_heartbeat") ;
		for( Tag t : Tag::N ) {
			if (!s_tab[+t]) continue ;                                                                  // if s_tab is not initialized yet (we are called from an async thread), no harm, just skip
			if (res.empty()) res =                 s_tab[+t]->heartbeat() ;                             // fast path
			else             for( auto const& he : s_tab[+t]->heartbeat() ) res.push_back(::move(he)) ;
		}
		trace("jobs",res) ;
		return res ;
	}

	void Backend::s_config(Config::Backend const config[]) {
		s_executable = *g_lmake_dir+"/_bin/job_exec" ;
		static ::jthread job_exec_thread        {_s_job_exec_thread_func       } ;
		static ::jthread heartbeat_thread       {_s_heartbeat_thread_func      } ;
		static ::jthread deferred_report_thread {_s_deferred_report_thread_func} ;
		static ::jthread deferred_lost_thread   {_s_deferred_lost_thread_func  } ;
		//
		::unique_lock lock{_s_mutex} ;
		for( Tag t : Tag::N ) if (s_tab[+t]) s_tab[+t]->config(config[+t]) ;
		s_service_ready.wait() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , JobIdx job , ::vmap_ss&& rsrcs , SubmitAttrs const& submit_attrs ) {
		Trace trace("acquire_cmd_line",tag,job,submit_attrs) ;
		SWEAR(!_s_mutex.try_lock()) ;
		auto           [it,fresh] = _s_start_tab.emplace(job,StartTabEntry()) ; // create entry
		StartTabEntry& entry      = it->second                                ;
		entry.open() ;
		entry.tag   = tag           ;
		entry.rsrcs = ::move(rsrcs) ;
		if (fresh) {                                                    entry.submit_attrs = submit_attrs ;                                            }
		else       { uint8_t n_retries = entry.submit_attrs.n_retries ; entry.submit_attrs = submit_attrs ; entry.submit_attrs.n_retries = n_retries ; } // keep retry count if it was counting
		trace("create_start_tab",job,entry) ;
		::vector_s cmd_line {
			s_executable
		,	s_server_fd.service(g_config.backends[+tag].addr)
		,	::to_string(entry.conn.seq_id)
		,	::to_string(job              )
		,	g_config.backends[+tag].is_local?"local":"remote"
		} ;
		trace("cmd_line",cmd_line) ;
		return cmd_line ;
	}

	// kill all if req==0
	void Backend::_s_kill_req(ReqIdx req) {
		Trace trace("s_kill_req",req) ;
		::vmap<JobIdx,StartTabEntry::Conn> to_kill ;
		{	::unique_lock lock { _s_mutex }    ;                               // lock for minimal time
			Date          now  = Date::s_now() ;
			for( Tag t : Tag::N )
				for( JobIdx j : s_tab[+t]->kill_req(req) ) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::NotStarted , JobExec(j,now) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					_s_start_tab.erase(j) ;
				}
			for( auto& [j,e] : _s_start_tab ) {
				if (req) {
					auto it = e.reqs.find(req) ;
					if (it==e.reqs.end()) continue ;
					if (e.reqs.size()>1) {
						e.reqs.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Continue , JobExec(j,now) , Req(req) ) ; // job is necessarly for some other req
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						continue ;
					}
				}
				to_kill.emplace_back(j,e.conn) ;
			}
		}
		//                                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( auto const& [j,c] : to_kill ) _s_wakeup_remote(j,c,JobExecRpcProc::Kill) ;
		//                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

}
