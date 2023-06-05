// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
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

	::latch                               Backend::s_service_ready   { 1 } ;
	::string                              Backend::s_executable      ;
	Backend*                              Backend::s_tab[+Tag::N]    = {}  ;
	ServerSockFd                          Backend::s_server_fd       ;
	::mutex                               Backend::_s_mutex          ;
	::umap<JobIdx,Backend::StartTabEntry> Backend::_s_start_tab      ;
	SmallIds<SmallId>                     Backend::_s_small_ids      ;
	ThreadQueue<Backend::DeferredEntry>   Backend::_s_deferred_queue ;

	::ostream& operator<<( ::ostream& os , Backend::StartTabEntry const& ste ) {
		return os << "StartTabEntry(" << ste.conn <<','<< ste.reqs << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Backend::StartTabEntry::Conn const& c ) {
		return os << "Conn(" << hex<<c.job_addr<<dec <<':'<< c.job_port <<','<< c.seq_id <<','<< c.small_id << ')' ;
	}



	void Backend::_s_wakeup_remote( JobIdx job , StartTabEntry::Conn const& conn , JobExecRpcProc proc ) {
		Trace trace("_s_wakeup_remote",job,proc) ;
		try {
			// as job_exec is not waiting for this message, pretend we are the job, so use JobExecRpcReq instead of JobRpcReply
			OMsgBuf().send( ClientSockFd(conn.job_addr,conn.job_port) , JobExecRpcReq(proc) ) ;
		} catch (...) {
			trace("no_job") ;
			// if job cannot be connected to, assume it is dead and pretend it died
			_s_handle_job_req( Fd() , JobRpcReq( JobProc::End , conn.seq_id , job , Status::Lost ) ) ;
		}
	}

	void Backend::_s_deferred_thread_func(::stop_token stop) {
		Trace::t_key = 'S' ;
		Trace trace("_s_deferred_thread_func") ;
		for(;;) {
			auto [popped,info] = _s_deferred_queue.pop(stop) ;
			if (!popped                     ) break ;
			if (!info.date.sleep_until(stop)) break ;
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			//
			{	::unique_lock lock{_s_mutex} ;                                 // lock _s_start_tab for minimal time to avoid dead-locks
				auto it = _s_start_tab.find(info.job) ;
				if (it==_s_start_tab.end()             ) continue ;
				if (it->second.conn.seq_id!=info.seq_id) continue ;
			}
			g_engine_queue.emplace( JobProc::ReportStart , Job(info.job) ) ;
		}
		trace("done") ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_req( Fd fd , JobRpcReq && jrr ) {
		Date start ;
		switch (jrr.proc) {
			case JobProc::None    : return false ;                             // if connection is lost, ignore it
			case JobProc::Start   :
			case JobProc::LiveOut :
			case JobProc::End     :
			case JobProc::ChkDeps :
			case JobProc::DepCrcs : break        ;
			default : FAIL(jrr.proc) ;
		}
		Job            job           { jrr.job        } ;
		Rule           rule          = job->rule        ;
		JobRpcReply    reply         { JobProc::Start } ;
		::vector<Node> report_unlink ;
		Trace trace("jrr",jrr) ;
		{	::unique_lock  lock  { _s_mutex } ;                                // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			auto           it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab") ; return false ; }
			StartTabEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id") ; return false ; }
			trace("entry",entry) ;
			switch (jrr.proc) {
				case JobProc::Start :
					start = ProcessDate::s_now() ;
					//            vvvvvvvvvvvvvvvvvvvvvvvvvvv
					entry.reqs  = s_start(rule->backend,+job) ;
					entry.start = start                       ;
					//            ^^^^^
					try {
						Rule::SimpleMatch match    = job.simple_match()      ;
						in_addr_t         job_addr = fd.peer_addr()          ;
						SmallId           small_id = _s_small_ids.acquire()  ;
						bool              live_out = false                   ;
						bool              keep_tmp = rule->keep_tmp          ;
						for( ReqIdx r : entry.reqs ) {
							Req req{r} ;
							if (job.c_req_info(req).live_out        ) live_out = true ;
							if (req->options.flags[ReqFlag::KeepTmp]) keep_tmp = true ;
						}
						job.fill_rpc_reply( reply , match , entry.rsrcs ) ;
						//
						reply.addr             = job_addr                       ;
						reply.ancillary_file   = job.ancillary_file("job_data") ;
						reply.autodep_method   = rule->autodep_method           ;
						reply.auto_mkdir       = rule->auto_mkdir               ;
						reply.chroot           = rule->chroot                   ;
						reply.cwd              = rule->cwd                      ;
					//	reply.env                                                 // from job.fill_rpc_reply above
					//	reply.force_deps                                          // from job.fill_rpc_reply above
						reply.hash_algo        = g_config.hash_algo             ;
					//	reply.host                                                // directly filled in job_exec
						reply.ignore_stat      = rule->ignore_stat              ;
						reply.interpreter      = rule->interpreter              ;
						reply.is_python        = rule->is_python                ;
					//	reply.job_id                                              // directly filled in job_exec
						reply.keep_tmp         = keep_tmp                       ;
						reply.kill_sigs        = rule->kill_sigs                ;
						reply.live_out         = live_out                       ;
						reply.lnk_support      = g_config.lnk_support           ;
						reply.reason           = entry.reason                   ;
						reply.root_dir         = *g_root_dir                    ;
						reply.rsrcs            = ::move(entry.rsrcs)            ;
					//	reply.script                                              // from job.fill_rpc_reply above
					//	reply.seq_id                                              // directly filled in job_exec
						reply.small_id         = small_id                       ;
					//	reply.stdin                                               // from job.fill_rpc_reply above
					//	reply.stdout                                              // from job.fill_rpc_reply above
					//	reply.targets                                             // from job.fill_rpc_reply above
						reply.timeout          = rule->timeout                  ;
						reply.remote_admin_dir = *g_remote_admin_dir            ;
						//
						reply.job_tmp_dir      = keep_tmp ? to_string(*g_root_dir,'/',AdminDir,job.ancillary_file("/job_keep_tmp")) : to_string(*g_remote_admin_dir,"/job_tmp/",small_id) ;
						//
						report_unlink = job.wash(match) ;
						entry.conn.job_addr = job_addr ;
						entry.conn.job_port = jrr.port ;
						entry.conn.small_id = small_id ;
					} catch (::string const& e) {
						_s_small_ids.release(entry.conn.small_id) ;
						trace("erase_start_tab",job,it->second,e) ;
						_s_start_tab.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Start , job , report_unlink , false/*report*/                  ) ;
						s_end(rule->backend,+job)                                                                         ;
						g_engine_queue.emplace( JobProc::End   , job , start , JobDigest{.status=Status::Err,.stderr=e} ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						return false ;
					}
				break ;
				case JobProc::End :
					start = entry.start ;
					_s_small_ids.release(entry.conn.small_id) ;
					trace("erase_start_tab",job,it->second) ;
					_s_start_tab.erase(it) ;
					//vvvvvvvvvvvvvvvvvvvvvvv
					s_end(rule->backend,+job) ;
					//^^^^^^^^^^^^^^^^^^^^^^^
				break ;
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
				bool deferred_start_report = Delay(job->exec_time)<rule->start_delay && report_unlink.empty() ; // if we report activity at start, deferring is meaningless
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , job , report_unlink , !deferred_start_report ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (deferred_start_report) _s_deferred_queue.emplace( start+rule->start_delay , +job , jrr.seq_id ) ;
				trace("started",reply) ;
			} break ;
			//                                                                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobProc::LiveOut :                                                 g_engine_queue.emplace( jrr.proc , job , ::move(jrr.txt)              ) ;                  break ;
			case JobProc::End     : job.end_exec() ;                                g_engine_queue.emplace( jrr.proc , job , start , ::move(jrr.digest)   ) ;                  break ;
			case JobProc::ChkDeps :
			case JobProc::DepCrcs : trace("deps",jrr.proc,jrr.digest.deps.size()) ; g_engine_queue.emplace( jrr.proc , job , ::move(jrr.digest.deps) , fd ) ; keep_fd = true ; break ;
			//                                                                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
						if (!_s_handle_job_req(fd,::move(jrr))) { fd.close() ; } // close fd if not requested to keep it
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
			trace("sleep") ;
			if (!g_config.heartbeat.sleep_for(stop)) {
				trace("done") ;
				return ;
			}
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			trace("slept") ;
			::umap<JobIdx,StartTabEntry::Conn> to_wakeup ;
			::vector<JobIdx>                   missings  = s_heartbeat() ;     // check jobs that have been submitted but have not started yet
			{	::unique_lock lock{_s_mutex} ;                                 // lock _s_start_tab for minimal time to avoid dead-locks
				for( JobIdx j : missings ) {
					auto it = _s_start_tab.find(j) ;
					if (it==_s_start_tab.end()) continue ;
					trace("erase_start_tab",j,it->second) ;
					_s_start_tab.erase(it) ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::Start , j , ::vector<Node>() , false ) ;
					g_engine_queue.emplace( JobProc::End   , j , Status::Lost             ) ; // signal jobs that have disappeared so they can be relaunched
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				}
				for( auto& [j,e] : _s_start_tab )
					if (e.old) to_wakeup[j] = e.conn ;                         // make a shadow to avoid too long a lock
					else       e.old        = true   ;                         // no reason to check newer jobs, so save resources
			}
			// check jobs that have already started
			//                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			for( auto const& [j,c] : to_wakeup ) _s_wakeup_remote(j,c,JobExecRpcProc::Heartbeat) ;
			//                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}

	void Backend::s_config(ServerConfig::Backend const config[]) {
		s_executable = *g_lmake_dir+"/_bin/job_exec" ;
		static ::jthread job_exec_thread {_s_job_exec_thread_func } ;
		static ::jthread heartbeat_thread{_s_heartbeat_thread_func} ;
		static ::jthread deferred_thread {_s_deferred_thread_func } ;
		//
		::unique_lock lock{_s_mutex} ;
		for( Tag t : Tag::N ) if (s_tab[+t]) s_tab[+t]->config(config[+t]) ;
		s_service_ready.wait() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , JobIdx job , ::vmap_ss&& rsrcs , JobReason reason ) {
		Trace trace("acquire_cmd_line",job,reason) ;
		SWEAR(!_s_mutex.try_lock()       ) ;
		SWEAR(!_s_start_tab.contains(job)) ;
		StartTabEntry& entry = _s_start_tab[job] ;
		entry.open() ;
		entry.rsrcs  = ::move(rsrcs) ;
		entry.reason = reason        ;
		trace("create_start_tab",job,entry) ;
		return { s_executable , s_server_fd.service(g_config.backends[+tag].addr) , ::to_string(entry.conn.seq_id) , ::to_string(job) } ;
	}

	// kill all if req==0
	void Backend::_s_kill_req(ReqIdx req) {
		Trace trace("s_kill_req",req) ;
		::vmap<JobIdx,StartTabEntry::Conn> to_kill ;
		{	::unique_lock lock{_s_mutex} ;                                     // lock for minimal time
			for( Tag t : Tag::N )
				for( JobIdx j : s_tab[+t]->kill_req(req) ) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::NotStarted , Job(j) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					_s_start_tab.erase(j) ;
				}
			for( auto& [j,e] : _s_start_tab ) {
				if (req) {
					auto it = e.reqs.find(req) ;
					if (it==e.reqs.end()) continue ;
					if (e.reqs.size()>1) {
						e.reqs.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Continue , Job(j) , Req(req) ) ; // job is necessary for some other req
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
