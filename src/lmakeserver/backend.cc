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
	Backend::BackendDescr                     Backend::s_tab[+Tag::N]           ;
	ServerSockFd                              Backend::s_server_fd              ;
	::mutex                                   Backend::_s_mutex                 ;
	::umap<JobIdx,Backend::StartTabEntry>     Backend::_s_start_tab             ;
	SmallIds<SmallId>                         Backend::_s_small_ids             ;
	ThreadQueue<Backend::DeferredReportEntry> Backend::_s_deferred_report_queue ;
	ThreadQueue<Backend::DeferredLostEntry  > Backend::_s_deferred_lost_queue   ;

	::ostream& operator<<( ::ostream& os , Backend::StartTabEntry const& ste ) {
		return os << "StartTabEntry(" << ste.conn <<','<< ste.tag <<','<< ste.reqs << ')' ;
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
			// if job cannot be connected to, assume it is dead and pretend it died after network_delay to give a chance to report if is already completed
			_s_deferred_lost_queue.emplace( Date::s_now()+g_config.network_delay , conn.seq_id , job ) ;
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
			DiskDate::s_refresh_now() ;                                                              // we have waited, refresh now
			_s_handle_job_req( JobRpcReq( JobProc::End , info.seq_id , info.job , Status::Lost ) ) ;
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
		Job             job               { jrr.job                } ;
		JobExec         job_exec          { job , ::move(jrr.host) } ;
		Rule            rule              = job->rule                ;
		JobRpcReply     reply             { JobProc::Start         } ;
		::vector<Node>  report_unlink     ;
		StartCmdAttrs   start_cmd_attrs   ;
		StartRsrcsAttrs start_rsrcs_attrs ;
		StartNoneAttrs  start_none_attrs  ;
		Trace trace("_s_handle_job_req",jrr) ;
		{	::unique_lock  lock  { _s_mutex } ;                                // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			auto           it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab") ; return false ; }
			StartTabEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id") ; return false ; }
			trace("entry",entry) ;
			switch (jrr.proc) {
				case JobProc::Start :
					job_exec.start = Date::s_now() ;
					//            vvvvvvvvvvvvvvvvvvvvvvv
					entry.reqs  = s_start(entry.tag,+job) ;
					entry.start = job_exec.start          ;
					//            ^^^^^^^^^^^^^^
					try {
						start_cmd_attrs   = rule->start_cmd_attrs  .eval(job) ;
						start_rsrcs_attrs = rule->start_rsrcs_attrs.eval(job) ;
						start_none_attrs  = rule->start_none_attrs .eval(job) ;
						//
						Rule::SimpleMatch match    = job.simple_match()     ;
						in_addr_t         job_addr = fd.peer_addr()         ;
						SmallId           small_id = _s_small_ids.acquire() ;
						bool              keep_tmp = start_none_attrs.keep_tmp ; for( ReqIdx r : entry.reqs ) if (Req(r)->options.flags[ReqFlag::KeepTmp]) keep_tmp = true ;
						::string          tmp_dir  = keep_tmp ? to_string(*g_root_dir,'/',job.ancillary_file(AncillaryTag::KeepTmp)) : to_string(*g_remote_admin_dir,"/job_tmp/",small_id) ;
						//
						job.fill_rpc_reply( reply , match , entry.rsrcs ) ;
						for( ::pair_ss const& kv : start_cmd_attrs  .env ) reply.env.push_back(kv) ;
						for( ::pair_ss const& kv : start_rsrcs_attrs.env ) reply.env.push_back(kv) ;
						for( ::pair_ss const& kv : start_none_attrs .env ) reply.env.push_back(kv) ;
						//
						reply.addr             = job_addr                    ;
						reply.ancillary_file   = job.ancillary_file()        ;
						reply.method           = start_cmd_attrs.method      ;
						reply.auto_mkdir       = start_cmd_attrs.auto_mkdir  ;
						reply.chroot           = start_cmd_attrs.chroot      ;
						reply.cwd_s            = rule->cwd_s                 ;
					//	reply.env                                               // done above
						reply.hash_algo        = g_config.hash_algo          ;
					//	reply.host                                              // directly filled in job_exec
						reply.ignore_stat      = start_cmd_attrs.ignore_stat ;
						reply.interpreter      = start_cmd_attrs.interpreter ;
						reply.is_python        = rule->is_python             ;
					//	reply.job_id                                            // directly filled in job_exec
						reply.keep_tmp         = keep_tmp                    ;
						reply.kill_sigs        = start_none_attrs.kill_sigs  ;
						reply.live_out         = entry.live_out              ;
						reply.lnk_support      = g_config.lnk_support        ;
						reply.local_mrkr       = start_cmd_attrs.local_mrkr  ;
						reply.reason           = entry.reason                ;
						reply.root_dir         = *g_root_dir                 ;
						reply.rsrcs            = ::move(entry.rsrcs)         ;
					//	reply.script                                            // from job.fill_rpc_reply above
					//	reply.seq_id                                            // directly filled in job_exec
						reply.small_id         = small_id                    ;
					//	reply.static_deps                                       // from job.fill_rpc_reply above
					//	reply.stdin                                             // from job.fill_rpc_reply above
					//	reply.stdout                                            // from job.fill_rpc_reply above
					//	reply.targets                                           // from job.fill_rpc_reply above
						reply.timeout          = start_rsrcs_attrs.timeout   ;
						reply.remote_admin_dir = *g_remote_admin_dir         ;
						reply.job_tmp_dir      = ::move(tmp_dir)             ;
						//
						report_unlink = job.wash(match) ;
						entry.conn.job_addr = job_addr ;
						entry.conn.job_port = jrr.port ;
						entry.conn.small_id = small_id ;
					} catch (::string const& e) {
						_s_small_ids.release(entry.conn.small_id) ;
						trace("erase_start_tab",job,it->second,e) ;
						Tag tag = entry.tag ;
						_s_start_tab.erase(it) ;
						job_exec.host.clear() ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Start , ::move(job_exec) , report_unlink , false/*report*/          ) ;
						s_end(tag,+job)                                                                                        ;
						g_engine_queue.emplace( JobProc::End   , ::move(job_exec) , JobDigest{.status=Status::Err,.stderr=e} ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						return false ;
					}
				break ;
				case JobProc::End : {
					job_exec.start = entry.start ;
					_s_small_ids.release(entry.conn.small_id) ;
					trace("erase_start_tab",job,it->second) ;
					Tag tag = entry.tag ;
					_s_start_tab.erase(it) ;
					//vvvvvvvvvvvvv
					s_end(tag,+job) ;
					//^^^^^^^^^^^^^
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
				serialize( OFStream(dir_guard(job.ancillary_file())) , ::pair(jrr,reply) ) ;
				bool deferred_start_report = Delay(job->exec_time)<start_none_attrs.start_delay && report_unlink.empty() ; // if we report activity at start, deferring is meaningless
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , JobExec(job_exec) , report_unlink , !deferred_start_report ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (deferred_start_report) _s_deferred_report_queue.emplace( job_exec.start+start_none_attrs.start_delay , jrr.seq_id , ::move(job_exec) ) ;
				trace("started",reply) ;
			} break ;
			case JobProc::ChkDeps  : //                                              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobProc::DepInfos : trace("deps",jrr.proc,jrr.digest.deps.size()) ; g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.digest.deps) , fd ) ; keep_fd = true ; break ;
			case JobProc::LiveOut  :                                                 g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.txt)              ) ;                  break ;
			//                                                                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			case JobProc::End :
				serialize( OFStream(job.ancillary_file(),::ios::app) , jrr ) ;
				job.end_exec() ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.digest) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
			trace("sleep") ;
			if (!g_config.heartbeat.sleep_for(stop)) {
				trace("done") ;
				return ;
			}
			DiskDate::s_refresh_now() ;                                        // we have waited, refresh now
			trace("slept") ;
			::umap<JobIdx,StartTabEntry::Conn> to_wakeup ;
			::vector<JobIdx>                   missings  = s_heartbeat() ;     // check jobs that have been submitted but have not started yet
			{	::unique_lock lock { _s_mutex }    ;                           // lock _s_start_tab for minimal time to avoid dead-locks
				Date          now  = Date::s_now() ;
				for( JobIdx j : missings ) {
					auto it = _s_start_tab.find(j) ;
					if (it==_s_start_tab.end()) continue ;
					trace("erase_start_tab",j,it->second) ;
					_s_start_tab.erase(it) ;
					JobExec je{j,now} ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::Start , ::move(je) , ::vector<Node>() , false ) ;
					g_engine_queue.emplace( JobProc::End   , ::move(je) , Status::Lost             ) ; // signal jobs that have disappeared so they can be relaunched
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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

	void Backend::s_config(Config::Backend const config[]) {
		s_executable = *g_lmake_dir+"/_bin/job_exec" ;
		static ::jthread job_exec_thread        {_s_job_exec_thread_func       } ;
		static ::jthread heartbeat_thread       {_s_heartbeat_thread_func      } ;
		static ::jthread deferred_report_thread {_s_deferred_report_thread_func} ;
		static ::jthread deferred_lost_thread   {_s_deferred_lost_thread_func  } ;
		//
		::unique_lock lock{_s_mutex} ;
		for( Tag t : Tag::N ) if (s_tab[+t].be) s_tab[+t].be->config(config[+t]) ;
		s_service_ready.wait() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , JobIdx job , bool live_out , ::vector_s&& rsrcs , JobReason reason ) {
		::vmap_ss const& rsrcs_spec = Job(job)->rule->submit_rsrcs_attrs.spec.rsrcs ;
		SWEAR( rsrcs_spec.empty() || rsrcs_spec.size()==rsrcs.size() ) ;
		//
		Trace trace("acquire_cmd_line",tag,job,reason) ;
		SWEAR(!_s_mutex.try_lock()       ) ;
		SWEAR(!_s_start_tab.contains(job)) ;
		StartTabEntry& entry = _s_start_tab[job] ;
		entry.open() ;
		entry.tag      = tag           ;
		entry.rsrcs    = ::move(rsrcs) ;
		entry.live_out = live_out      ;
		entry.reason   = reason        ;
		trace("create_start_tab",job,entry) ;
		return { s_executable , s_server_fd.service(g_config.backends[+tag].addr) , ::to_string(entry.conn.seq_id) , ::to_string(job) , ::to_string(s_tab[+tag].is_remote) } ;
	}

	// kill all if req==0
	void Backend::_s_kill_req(ReqIdx req) {
		Trace trace("s_kill_req",req) ;
		::vmap<JobIdx,StartTabEntry::Conn> to_kill ;
		{	::unique_lock lock { _s_mutex }    ;                               // lock for minimal time
			Date          now  = Date::s_now() ;
			for( Tag t : Tag::N )
				for( JobIdx j : s_tab[+t].be->kill_req(req) ) {
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
						g_engine_queue.emplace( JobProc::Continue , JobExec(j,now) , Req(req) ) ; // job is necessary for some other req
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
