// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "config.hh"

#include "core.hh"

using namespace Disk   ;
using namespace Time   ;
using namespace Engine ;

namespace Backends {

	//
	// Backend::*
	//

	::ostream& operator<<( ::ostream& os , Backend::StartEntry const& ste ) {
		return os << "StartEntry(" << ste.conn <<','<< ste.tag <<','<< ste.reqs <<','<< ste.submit_attrs << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Backend::StartEntry::Conn const& c ) {
		return os << "Conn(" << SockFd::s_addr_str(c.host) <<':'<< c.port <<','<< c.seq_id <<','<< c.small_id << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Backend::DeferredReportEntry const& dre ) {
		return os << "DeferredReportEntry(" << dre.seq_id <<','<< dre.job_exec << ')' ;
	}

	::pair<Pdate/*eta*/,bool/*keep_tmp*/> Backend::StartEntry::req_info() const {
		Pdate eta      ;
		bool  keep_tmp = false ;
		::unique_lock lock{Req::s_reqs_mutex} ;                                // ensure Req::s_store is not reallocated while we walk
		for( ReqIdx r : reqs ) {
			Req req{r} ;
			keep_tmp |= req->options.flags[ReqFlag::KeepTmp]  ;
			eta       = +eta ? ::min(eta,req->eta) : req->eta ;
		}
		return {eta,keep_tmp} ;
	}

	//
	// Backend
	//

	ENUM( EventKind , Master , Stop , Slave )

	::string                          Backend::s_executable              ;
	Backend*                          Backend::s_tab  [+Tag::N]          ;
	::atomic<bool>                    Backend::s_ready[+Tag::N]          = {} ;
	::mutex                           Backend::_s_mutex                  ;
	::map<JobIdx,Backend::StartEntry> Backend::_s_start_tab              ;
	SmallIds<SmallId>                 Backend::_s_small_ids              ;
	Backend::JobExecThread       *    Backend::_s_job_exec_thread        = nullptr ;
	Backend::DeferredReportThread*    Backend::_s_deferred_report_thread = nullptr ;

	static ::vmap_s<DepDigest> _mk_digest_deps( ::vmap_s<pair_s<AccDflags>> const& deps_attrs ) {
		::vmap_s<DepDigest> res ; res.reserve(deps_attrs.size()) ;
		for( auto const& [k,daf] : deps_attrs ) {
			auto const& [d,af] = daf ;
			if (+af.accesses) res.emplace_back( d , DepDigest( af.accesses , af.dflags , true/*parallel*/ , file_date(d) ) ) ; // if dep is accessed, pretend it is now
			else              res.emplace_back( d , DepDigest( af.accesses , af.dflags , true/*parallel*/                ) ) ;
		}
		return res ;
	}

	static inline bool _localize( Tag t , ReqIdx ri ) {
		return Req(ri)->options.flags[ReqFlag::Local] || !Backend::s_ready[+t] ; // if asked backend is not usable, force local execution
	}

	void Backend::s_submit( Tag tag , JobIdx ji , ReqIdx ri , SubmitAttrs&& submit_attrs , ::vmap_ss&& rsrcs ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_submit",tag,ji,ri,submit_attrs,rsrcs) ;
		//
		if ( tag!=Tag::Local && _localize(tag,ri) ) {
			SWEAR(+tag<+Tag::N) ;                                                               // prevent compiler array bound warning in next statement
			if (!s_tab[+tag]) throw to_string("backend ",mk_snake(tag)," is not implemented") ;
			rsrcs = s_tab[+tag]->mk_lcl( ::move(rsrcs) , s_tab[+Tag::Local]->capacity() ) ;
			tag   = Tag::Local                                                            ;
		}
		if (!s_ready[+tag]) throw "local backend is not available"s ;
		submit_attrs.tag = tag ;
		s_tab[+tag]->submit(ji,ri,submit_attrs,::move(rsrcs)) ;
	}

	void Backend::s_add_pressure( Tag t , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		if (_localize(t,ri)) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_add_pressure",t,j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) {
			s_tab[+t]->add_pressure(j,ri,sa) ;                                 // if job is not started, ask sub-backend to raise its priority
		} else {
			it->second.reqs.insert(ri) ;                                       // else, job is already started, note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;                                    // and update submit_attrs in case job was not actually started
		}
	}

	void Backend::s_set_pressure( Tag t , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		if (_localize(t,ri)) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_set_pressure",t,j,ri,sa) ;
		s_tab[+t]->set_pressure(j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->set_pressure(j,ri,sa) ;         // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;            // and update submit_attrs in case job was not actually started
	}

	void Backend::s_launch() {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_launch") ;
		for( Tag t : Tag::N ) {
			if (!s_ready[+t]) continue ;
			try {
				s_tab[+t]->launch() ;
			} catch (::vmap<JobIdx,pair_s<vmap_ss/*rsrcs*/>>& err_list) {
				for( auto& [ji,re] : err_list ) {
					JobExec           je     { ji , Pdate::s_now()                                                                                      } ;
					Rule::SimpleMatch match  = je->simple_match()                                                                                         ;
					JobDigest         digest { .status=Status::Err , .deps=_mk_digest_deps(je->rule->deps_attrs.eval(match)) , .stderr=::move(re.first) } ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::Start , JobExec(je) , false/*report*/ , je->wash(match)  ) ;
					g_engine_queue.emplace( JobProc::End   , ::move (je) , ::move(re.second) , ::move(digest) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				}
			}
		}
	}

	void Backend::_s_wakeup_remote( Tag tag , JobIdx job , StartEntry::Conn const& conn , JobServerRpcProc proc ) {
		Trace trace("_s_wakeup_remote",job,conn,proc) ;
		try {
			ClientSockFd fd(conn.host,conn.port) ;
			OMsgBuf().send( fd , JobServerRpcReq(proc,conn.seq_id,job) ) ;     // XXX : straighten out Fd : Fd must not detach on mv and Epoll must take an AutoCloseFd as arg to take close resp.
		} catch (::string const& e) {
			trace("no_job",job,e) ;
			// if job cannot be connected to, assume it is dead and pretend it died
			::pair_s<bool/*retry*/> lost_info = s_lost_info(tag,job)                                                                  ;
			JobDigest               jd        { .status=lost_info.second?Status::Lost:Status::Err , .stderr=::move(lost_info.first) } ;
			_s_handle_job_req( JobRpcReq( JobProc::End , conn.seq_id , job , ::move(jd) ) ) ;
		}
	}

	void Backend::_s_handle_deferred_report(DeferredReportEntry&& dre) {
		::unique_lock lock { _s_mutex }                       ;                // lock _s_start_tab for minimal time to avoid dead-locks
		auto          it   = _s_start_tab.find(+dre.job_exec) ;
		if (it==_s_start_tab.end()            ) return ;
		if (it->second.conn.seq_id!=dre.seq_id) return ;
		g_engine_queue.emplace( JobProc::ReportStart , ::move(dre.job_exec) ) ;
	}

	Status Backend::_s_release_start_entry( ::map<JobIdx,StartEntry>::iterator it , Status status=Status::Ok ) {
		if (status==Status::Lost) {
			uint8_t& n_retries = it->second.submit_attrs.n_retries ;
			if (n_retries!=0) {                                                // keep entry to keep on going retry count
				uint8_t nr = n_retries - 1 ;                                   // we just try one time, note it
				it->second = {} ;                                              // clear other entries, as if we do not exist
				n_retries = nr ;
				return status ;
			}
			status = Status::Err ;
		}
		_s_start_tab.erase(it) ;
		return status ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_req( JobRpcReq&& jrr , Fd fd ) {
		switch (jrr.proc) {
			case JobProc::None     : return false ;                            // if connection is lost, ignore it
			case JobProc::Start    :
			case JobProc::ChkDeps  :
			case JobProc::DepInfos : SWEAR(+fd,jrr.proc) ; break ;             // fd is needed to reply
			case JobProc::End      :                                           // dont reply if we have no fd
			case JobProc::LiveOut  :                       break ;             // no reply
			default : FAIL(jrr.proc) ;
		}
		Pdate                       now               = Pdate::s_now()   ;
		Job                         job               { jrr.job        } ;
		JobExec                     job_exec          { job , now      } ;     // keep jrr intact for recording
		Rule                        rule              = job->rule        ;
		JobRpcReply                 reply             { JobProc::Start } ;
		::vector<Node>              report_unlink     ;
		StartCmdAttrs               start_cmd_attrs   ;
		::pair_ss/*script,call*/    cmd               ;
		::vmap_s<pair_s<AccDflags>> deps_attrs        ;
		StartRsrcsAttrs             start_rsrcs_attrs ;
		StartNoneAttrs              start_none_attrs  ;
		::string                    start_exc_txt     ;
		Pdate                       eta               ;
		SubmitAttrs                 submit_attrs      ;
		::vmap_ss                   rsrcs             ;
		::string                    backend_msg       ;
		Trace trace("_s_handle_job_req",jrr,job_exec) ;
		{	::unique_lock lock  { _s_mutex } ;                                // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			auto          it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry&   entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			trace("entry",entry) ;
			switch (jrr.proc) {
				case JobProc::Start : {
					submit_attrs   = entry.submit_attrs ;
					rsrcs          = entry.rsrcs        ;
					//                            vvvvvvvvvvvvvvvvvvvvvvv
					tie(backend_msg,entry.reqs) = s_start(entry.tag,+job) ;
					entry.start                 = now                     ;
					//                            ^^^
					// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
					Rule::SimpleMatch match = job->simple_match() ;
					try {
						start_none_attrs = rule->start_none_attrs.eval(match,rsrcs) ;
					} catch (::string const& e) {
						start_none_attrs = rule->start_none_attrs.spec ;
						start_exc_txt    = e                           ;
					}
					report_unlink = job->wash(match) ;
					bool deps_attrs_passed = false            ;
					bool cmd_attrs_passed  = false            ;
					bool cmd_passed        = false            ;
					bool keep_tmp          = false/*garbage*/ ;
					tie(eta,keep_tmp) = entry.req_info() ;
					keep_tmp |= start_none_attrs.keep_tmp ;
					try {
						deps_attrs        = rule->deps_attrs       .eval(match      ) ; deps_attrs_passed = true ;
						start_cmd_attrs   = rule->start_cmd_attrs  .eval(match,rsrcs) ; cmd_attrs_passed  = true ;
						cmd               = rule->cmd              .eval(match,rsrcs) ; cmd_passed        = true ;
						start_rsrcs_attrs = rule->start_rsrcs_attrs.eval(match,rsrcs) ;
					} catch (::string const& e) {
						reply.proc = JobProc::None ;                           // instruct job_exec to give up
						//vvvvvvvvvvvvvvvvvvvvvv
						OMsgBuf().send(fd,reply) ;
						//^^^^^^^^^^^^^^^^^^^^^^
						_s_small_ids.release(entry.conn.small_id) ;
						trace("erase_start_tab",job,it->second,STR(cmd_attrs_passed),STR(cmd_passed),e) ;
						Tag tag = entry.tag ;
						_s_release_start_entry(it) ;
						job_exec.host = NoSockAddr ;                           // if job does not start, do not report a host
						::string err_str = to_string(
								cmd_passed        ? rule->start_rsrcs_attrs.s_exc_msg(false/*using_static*/)
							:	cmd_attrs_passed  ? rule->cmd              .s_exc_msg(false/*using_static*/)
							:	deps_attrs_passed ? rule->start_cmd_attrs  .s_exc_msg(false/*using_static*/)
							:	                    rule->deps_attrs       .s_exc_msg(false/*using_static*/)
						,	'\n'
						,	e
						) ;
						s_end( tag , +job , Status::Lost ) ;
						JobDigest digest { .status=Status::Err , .deps=_mk_digest_deps(deps_attrs) , .stderr=::move(err_str) }  ;
						trace("early_err",digest) ;
						{	OFStream ofs { dir_guard(job->ancillary_file()) } ;
							serialize( ofs , JobInfoStart({
								.eta          = eta
							,	.submit_attrs = submit_attrs
							,	.rsrcs        = rsrcs
							,	.host         = job_exec.host
							,	.pre_start    = jrr
							,	.start        = reply
							,	.backend_msg  = backend_msg
							}) ) ;
							serialize( ofs , JobInfoEnd( JobRpcReq(JobProc::End,jrr.job,jrr.seq_id,digest) ) ) ;
						}
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Start , ::move(job_exec) , false/*report_now*/ , report_unlink , start_exc_txt ) ;
						g_engine_queue.emplace( JobProc::End   , ::move(job_exec) , ::move(rsrcs) , ::move(digest)                      ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						return false ;
					}
					//
					::vector_s targets  = match.targets()        ;
					SmallId    small_id = _s_small_ids.acquire() ;
					//
					::string tmp_dir = keep_tmp ?
						to_string(*g_root_dir,'/',job->ancillary_file(AncillaryTag::KeepTmp))
					:	to_string(g_config.remote_tmp_dir,'/',small_id)
					;
					//
					job_exec.host = fd.peer_addr() ;
					// simple attrs
					reply.addr                    = job_exec.host                       ;
					reply.autodep_env.auto_mkdir  = start_cmd_attrs.auto_mkdir          ;
					reply.autodep_env.ignore_stat = start_cmd_attrs.ignore_stat         ;
					reply.autodep_env.lnk_support = g_config.lnk_support                ;
					reply.autodep_env.src_dirs_s  = g_config.src_dirs_s                 ;
					reply.autodep_env.root_dir    = *g_root_dir                         ;
					reply.autodep_env.tmp_dir     = ::move(tmp_dir               )      ; // tmp directory on disk
					reply.autodep_env.tmp_view    = ::move(start_cmd_attrs.tmp   )      ; // tmp directory as viewed by job
					reply.chroot                  = ::move(start_cmd_attrs.chroot)      ;
					reply.cmd                     = ::move(cmd                   )      ;
					reply.cwd_s                   = rule->cwd_s                         ;
					reply.hash_algo               = g_config.hash_algo                  ;
					reply.interpreter             = ::move(start_cmd_attrs.interpreter) ;
					reply.keep_tmp                = keep_tmp                            ;
					reply.kill_sigs               = ::move(start_none_attrs.kill_sigs)  ;
					reply.live_out                = submit_attrs.live_out               ;
					reply.method                  = start_cmd_attrs.method              ;
					reply.small_id                = small_id                            ;
					reply.timeout                 = start_rsrcs_attrs.timeout           ;
					reply.remote_admin_dir        = g_config.remote_admin_dir           ;
					// fancy attrs
					for( ::pair_ss& kv : start_cmd_attrs  .env ) reply.env.push_back(::move(kv)) ;
					for( ::pair_ss& kv : start_rsrcs_attrs.env ) reply.env.push_back(::move(kv)) ;
					for( ::pair_ss& kv : start_none_attrs .env ) reply.env.push_back(::move(kv)) ;
					//
					if ( rule->stdin_idx !=Rule::NoVar && +job->deps[rule->stdin_idx] ) reply.stdin  = deps_attrs[rule->stdin_idx ].second.first ;
					if ( rule->stdout_idx!=Rule::NoVar                                ) reply.stdout = targets   [rule->stdout_idx]              ;
					//
					reply.targets.reserve(targets.size()) ;
					for( VarIdx t=0 ; t<targets.size() ; t++ ) if (!targets[t].empty()) reply.targets.emplace_back( targets[t] , false/*is_native_star:garbage*/ , rule->tflags(t) ) ;
					//
					reply.static_deps = _mk_digest_deps(deps_attrs) ;
					//
					entry.conn.host     = job_exec.host ;
					entry.conn.port     = jrr.port      ;
					entry.conn.small_id = small_id      ;
				} break ;
				case JobProc::End : {
					rsrcs = ::move(entry.rsrcs) ;
					job_exec.host       = entry.conn.host ;
					job_exec.start_date = entry.start     ;
					_s_small_ids.release(entry.conn.small_id) ;
					trace("erase_start_tab",job,it->second) ;
					// if we have no fd, job end was invented by heartbeat, no acknowledge
					// acknowledge job end before telling backend as backend may wait the end of the job
					//             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					if (+fd) try { OMsgBuf().send(fd,JobRpcReply(JobProc::End)) ; } catch (::string const&) {} // if job is dead, we dont care, we have our digest
					backend_msg = s_end( entry.tag , +job,jrr.digest.status ) ;
					//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					jrr.digest.status = _s_release_start_entry(it,jrr.digest.status) ;
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
				serialize(
					OFStream(dir_guard(job->ancillary_file()))
				,	JobInfoStart({
						.eta          = eta
					,	.submit_attrs = submit_attrs
					,	.rsrcs        = ::move(rsrcs)
					,	.host         = job_exec.host
					,	.pre_start    = jrr
					,	.start        = reply
					,	.backend_msg  = backend_msg
					})
				) ;
				//
				bool deferred_start_report =
					Delay(job->exec_time)<start_none_attrs.start_delay         // dont defer long jobs
				&&	report_unlink.empty()                                      // dont defer if we must report info at start time
				&&	start_exc_txt.empty()                                      // .
				&&	!submit_attrs.live_out                                     // always show a start line if we must report live output to identify job for which output is shown
				;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , JobExec(job_exec) , !deferred_start_report , report_unlink , start_exc_txt ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (deferred_start_report) _s_deferred_report_thread->emplace_at( job_exec.start_date+start_none_attrs.start_delay , jrr.seq_id , ::move(job_exec) ) ;
				trace("started",reply) ;
			} break ;
			case JobProc::ChkDeps  : //                                              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobProc::DepInfos : trace("deps",jrr.proc,jrr.digest.deps.size()) ; g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.digest.deps) , fd ) ; keep_fd = true ; break ;
			case JobProc::LiveOut  :                                                 g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.txt)              ) ;                  break ;
			//                                                                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			case JobProc::End :
				serialize( OFStream(job->ancillary_file(),::ios::app) , JobInfoEnd(jrr,::move(backend_msg)) ) ;
				job->end_exec() ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(rsrcs) , ::move(jrr.digest) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			break ;
			default : FAIL(jrr.proc) ;
		}
		return keep_fd ;
	}

	void Backend::_s_heartbeat_thread_func(::stop_token stop) {
		Trace::t_key = 'H' ;
		Trace trace("_heartbeat_thread_func") ;
		Pdate  last_wrap_around = Pdate::s_now() ;
		//
		for( JobIdx job=0 ;; job++ ) {
			StartEntry::Conn      conn         ;
			::pair_s<Bool3/*ok*/> lost_report  = {{},Yes/*garbage*/} ;
			Status                status       = Status::Unknown     ;
			Pdate                 eta          ;
			::vmap_ss             rsrcs        ;
			SubmitAttrs           submit_attrs ;
			Tag                   tag          = Tag::Unknown    ;
			{	::unique_lock lock { _s_mutex }                    ;           // lock _s_start_tab for minimal time
				auto          it   = _s_start_tab.lower_bound(job) ;
				if (it==_s_start_tab.end()) goto WrapAround ;
				//
				/**/        job   = it->first      ;
				StartEntry& entry = it->second     ;
				//
				if (!entry    ) {                    continue ; }              // not a real entry                        ==> no check, no wait
				if (!entry.old) { entry.old = true ; continue ; }              // entry is too new, wait until next round ==> no check, no wait
				tag  = entry.tag  ;
				conn = entry.conn ;
				if (+entry.start             ) goto Wakeup ;
				lost_report = s_heartbeat(tag,job) ;
				if (lost_report.second==Yes  ) goto Next   ;                    // job is still alive
				if (lost_report.first.empty()) lost_report.first = "vanished" ;
				//
				trace("handle_job",job,entry,status) ;
				rsrcs        = ::move(entry.rsrcs       )                                                     ;
				submit_attrs = ::move(entry.submit_attrs)                                                     ;
				eta          = entry.req_info().first                                                         ;
				status       = _s_release_start_entry( it , lost_report.second==No?Status::Err:Status::Lost ) ;
			}
			{	JobExec je { job , Pdate::s_now() } ;
				if (status>Status::Garbage) {
					JobInfoStart jis { .eta=eta , .submit_attrs=submit_attrs , .rsrcs=rsrcs , .host=conn.host , .backend_msg=lost_report.first } ;
					serialize( OFStream(dir_guard(je->ancillary_file())) , jis ) ;
				}
				// signal jobs that have disappeared so they can be relaunched or reported in error
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , JobExec(je) , false/*report_now*/ , je->wash(je->simple_match())                          ) ;
				g_engine_queue.emplace( JobProc::End   , ::move (je) , ::move(rsrcs) , JobDigest{.status=status,.stderr=::move(lost_report.first)} ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Next ;
			}
		Wakeup :
			_s_wakeup_remote(tag,job,conn,JobServerRpcProc::Heartbeat) ;
		Next :
			if (!Delay(0.1).sleep_for(stop)) break ;                           // limit job checks to 10/s overall
			continue ;
		WrapAround :
			job = 0 ;
			Delay d = Delay(10.) + g_config.network_delay ;                                                // ensure jobs have had a minimal time to start and signal it
			if ((last_wrap_around+d).sleep_until(stop)) { last_wrap_around = Pdate::s_now() ; continue ; } // limit job checks 1/10s per job
			else                                        {                                     break    ; }
		}
		trace("done") ;
	}

	void Backend::s_config(Config::Backend const config[]) {
		s_executable = *g_lmake_dir+"/_bin/job_exec" ;
		static ::jthread            heartbeat_thread      {    _s_heartbeat_thread_func                 } ;
		static JobExecThread        job_exec_thread       {'J',_s_handle_job_req        ,1000/*backlog*/} ; _s_job_exec_thread        = &job_exec_thread        ;
		static DeferredReportThread deferred_report_thread{'S',_s_handle_deferred_report                } ; _s_deferred_report_thread = &deferred_report_thread ;
		//
		::unique_lock lock{_s_mutex} ;
		for( Tag t : Tag::N )
			if ( s_tab[+t] && config[+t].configured )                          // if implemented and configured
				s_ready[+t] = s_tab[+t]->config(config[+t]) ;
		job_exec_thread.wait_started() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , JobIdx job , ::vmap_ss&& rsrcs , SubmitAttrs const& submit_attrs ) {
		Trace trace("acquire_cmd_line",tag,job,submit_attrs) ;
		SWEAR(!_s_mutex.try_lock()) ;                                          // check we have the lock to access _s_start_tab
		//
		SubmitRsrcsAttrs::s_canon(rsrcs) ;
		//
		auto        [it,fresh] = _s_start_tab.emplace(job,StartEntry()) ;      // create entry
		StartEntry& entry      = it->second                             ;
		entry.open() ;
		entry.tag   = tag           ;
		entry.rsrcs = ::move(rsrcs) ;
		if (fresh) {                                                    entry.submit_attrs = submit_attrs ;                                            }
		else       { uint8_t n_retries = entry.submit_attrs.n_retries ; entry.submit_attrs = submit_attrs ; entry.submit_attrs.n_retries = n_retries ; } // keep retry count if it was counting
		trace("create_start_tab",job,entry) ;
		::vector_s cmd_line {
			s_executable
		,	_s_job_exec_thread->fd.service(g_config.backends[+tag].addr)
		,	::to_string(entry.conn.seq_id)
		,	::to_string(job              )
		} ;
		trace("cmd_line",cmd_line) ;
		return cmd_line ;
	}

	// kill all if req==0
	void Backend::_s_kill_req(ReqIdx req) {
		Trace trace("s_kill_req",req) ;
		::vmap<JobIdx,pair<Tag,StartEntry::Conn>> to_kill ;
		{	::unique_lock lock { _s_mutex }     ;                              // lock for minimal time
			Pdate         now  = Pdate::s_now() ;
			for( Tag t : Tag::N ) {
				if (!s_ready[+t]) continue ;
				for( JobIdx j : s_tab[+t]->kill_req(req) ) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::NotStarted , JobExec(j,now) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					_s_start_tab.erase(j) ;
				}
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
				to_kill.emplace_back(j,::pair(e.tag,e.conn)) ;
			}
		}
		//                                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( auto const& [j,c] : to_kill ) _s_wakeup_remote(c.first,j,c.second,JobServerRpcProc::Kill) ;
		//                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

}
