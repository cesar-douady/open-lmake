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

	::ostream& operator<<( ::ostream& os , Backend::DeferredEntry const& de ) {
		return os << "DeferredEntry(" << de.seq_id <<','<< de.job_exec << ')' ;
	}

	::pair<Pdate/*eta*/,bool/*keep_tmp*/> Backend::StartEntry::req_info() const {
		Pdate eta      ;
		bool  keep_tmp = false ;
		::unique_lock lock{Req::s_reqs_mutex} ;                                // taking Req::s_reqs_mutex is compulsery to derefence req
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
	::mutex                           Backend::_s_mutex                  ;
	::map<JobIdx,Backend::StartEntry> Backend::_s_start_tab              ;
	SmallIds<SmallId>                 Backend::_s_small_ids              ;
	Backend::JobExecThread *          Backend::_s_job_start_thread       = nullptr ;
	Backend::JobExecThread *          Backend::_s_job_mngt_thread        = nullptr ;
	Backend::JobExecThread *          Backend::_s_job_end_thread         = nullptr ;
	Backend::DeferredThread*          Backend::_s_deferred_report_thread = nullptr ;
	Backend::DeferredThread*          Backend::_s_deferred_wakeup_thread = nullptr ;

	static ::vmap_s<DepDigest> _mk_digest_deps( ::vmap_s<pair_s<AccDflags>> const& deps_attrs ) {
		NfsGuard            nfs_guard { g_config.reliable_dirs } ;
		::vmap_s<DepDigest> res       ; res.reserve(deps_attrs.size()) ;
		for( auto const& [k,daf] : deps_attrs ) {
			auto const& [d,af] = daf ;
			if (+af.accesses) res.emplace_back( d , DepDigest( af.accesses , af.dflags , true/*parallel*/ , file_date(nfs_guard.access(d)) ) ) ; // if dep is accessed, pretend it is now
			else              res.emplace_back( d , DepDigest( af.accesses , af.dflags , true/*parallel*/                                  ) ) ;
		}
		return res ;
	}

	static inline bool _localize( Tag t , ReqIdx ri ) {
		::unique_lock lock{Req::s_reqs_mutex} ;                                 // taking Req::s_reqs_mutex is compulsery to derefence req
		return Req(ri)->options.flags[ReqFlag::Local] || !Backend::s_ready(t) ; // if asked backend is not usable, force local execution
	}

	void Backend::s_submit( Tag tag , JobIdx ji , ReqIdx ri , SubmitAttrs&&submit_attrs , ::vmap_ss&& rsrcs ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_submit",tag,ji,ri,submit_attrs,rsrcs) ;
		//
		if ( tag!=Tag::Local && _localize(tag,ri) ) {
			SWEAR(+tag<+Tag::N) ;                                                               // prevent compiler array bound warning in next statement
			if (!s_tab[+tag]) throw to_string("backend ",mk_snake(tag)," is not implemented") ;
			rsrcs = s_tab[+tag]->mk_lcl( ::move(rsrcs) , s_tab[+Tag::Local]->capacity() ) ;
			tag   = Tag::Local                                                            ;
			trace("local",rsrcs) ;
		}
		if (!s_ready(tag)) throw "local backend is not available"s ;
		submit_attrs.tag = tag ;
		s_tab[+tag]->submit(ji,ri,submit_attrs,::move(rsrcs)) ;
	}

	void Backend::s_add_pressure( Tag t , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		if (_localize(t,ri)) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_add_pressure",t,j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) {
			s_tab[+t]->add_pressure(j,ri,sa) ;                                 // ask sub-backend to raise its priority
		} else {
			it->second.reqs.push_back(ri) ;                                    // note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;                                    // and update submit_attrs in case job was not actually started
		}
	}

	void Backend::s_set_pressure( Tag t , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		if (_localize(t,ri)) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_set_pressure",t,j,ri,sa) ;
		s_tab[+t]->set_pressure(j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->set_pressure(j,ri,sa) ;         // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;            // and update submit_attrs in case job was not actually started
	}

	void Backend::s_launch() {
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_launch") ;
		for( Tag t : Tag::N ) {
			if (!s_ready(t)) continue ;
			try {
				s_tab[+t]->launch() ;
			} catch (::vmap<JobIdx,pair_s<vmap_ss/*rsrcs*/>>& err_list) {
				for( auto&& [ji,re] : err_list ) {
					JobExec           je     { ji                                                                                 } ;
					Rule::SimpleMatch match  = je->simple_match()                                                                   ;
					JobDigest         digest { .status=Status::EarlyErr , .deps=_mk_digest_deps(je->rule->deps_attrs.eval(match)) } ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::Start , JobExec(je) , false/*report*/                                       ) ;
					g_engine_queue.emplace( JobProc::End   , ::move (je) , ::move(re.second) , ::move(digest) , ::move(re.first) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("cannot_launch",ji) ;
					_s_start_tab.erase(ji) ;
				}
			}
		}
	}

	void Backend::_s_handle_deferred_wakeup(DeferredEntry&& de) {
		{	::unique_lock lock { _s_mutex }                       ;                // lock _s_start_tab for minimal time to avoid dead-locks
			auto          it   = _s_start_tab.find(+de.job_exec) ;
			if (it==_s_start_tab.end()           ) return ;                                         // too late, job has ended
			if (it->second.conn.seq_id!=de.seq_id) return ;                                         // too late, job has ended and restarted
		}
		Trace trace(BeChnl,"_s_handle_deferred_wakeup",de) ;
		JobDigest jd { .status=Status::LateLost } ;                                             // job is still present, must be really lost
		if (+de.job_exec.start_) jd.stats.total = Pdate::s_now()-de.job_exec.start_ ;
		_s_handle_job_end( JobRpcReq( JobProc::End , de.seq_id , +de.job_exec , ::move(jd) ) ) ;
	}

	void Backend::_s_wakeup_remote( JobIdx job , StartEntry::Conn const& conn , Pdate const& start , JobServerRpcProc proc ) {
		Trace trace(BeChnl,"_s_wakeup_remote",job,conn,proc) ;
		try {
			ClientSockFd fd(conn.host,conn.port) ;
			OMsgBuf().send( fd , JobServerRpcReq(proc,conn.seq_id,job) ) ;     // XXX : straighten out Fd : Fd must not detach on mv and Epoll must take an AutoCloseFd as arg to take close resp.
		} catch (::string const& e) {
			trace("no_job",job,e) ;
			// if job cannot be connected to, assume it is dead and pretend it died if it still exists after network delay
			_s_deferred_wakeup_thread->emplace_after( g_config.network_delay , DeferredEntry(conn.seq_id,JobExec(Job(job),conn.host,start,New)) ) ;
		}
	}

	void Backend::_s_handle_deferred_report(DeferredEntry&& dre) {
		::unique_lock lock { _s_mutex }                       ;                // lock _s_start_tab for minimal time to avoid dead-locks
		auto          it   = _s_start_tab.find(+dre.job_exec) ;
		if (it==_s_start_tab.end()            ) return ;
		if (it->second.conn.seq_id!=dre.seq_id) return ;
		Trace trace(BeChnl,"_s_handle_deferred_report",dre) ;
		g_engine_queue.emplace( JobProc::ReportStart , ::move(dre.job_exec) ) ;
	}

	Status Backend::_s_release_start_entry( ::map<JobIdx,StartEntry>::iterator it , Status status=Status::Ok ) {
		Trace trace(BeChnl,"_s_release_start_entry",it->first,status) ;
		if ( is_lost(status) && is_ok(status)==Maybe ) {
			uint8_t& n_retries = it->second.submit_attrs.n_retries ;
			if (n_retries!=0) {                                                // keep entry to keep on going retry count
				uint8_t nr = n_retries - 1 ;                                   // we just try one time, note it
				it->second = {} ;                                              // clear other entries, as if we do not exist
				n_retries = nr ;
				return status ;
			}
			status = mk_err(status) ;
		}
		trace("erase") ;
		_s_start_tab.erase(it) ;
		return status ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_start( JobRpcReq&& jrr , Fd fd ) {
		switch (jrr.proc) {
			case JobProc::None  : return false ;                               // if connection is lost, ignore it
			case JobProc::Start : SWEAR(+fd,jrr.proc) ; break ;                // fd is needed to reply
			default : FAIL(jrr.proc) ;
		}
		Job                                                       job               { jrr.job        } ;
		JobExec                                                   job_exec          ;
		Rule                                                      rule              = job->rule        ;
		JobRpcReply                                               reply             { JobProc::Start } ;
		::pair<vmap<Node,FileAction>,vmap<Node,bool/*uniquify*/>> pre_actions       ;
		StartCmdAttrs                                             start_cmd_attrs   ;
		::pair_ss/*script,call*/                                  cmd               ;
		::vmap_s<pair_s<AccDflags>>                               deps_attrs        ;
		StartRsrcsAttrs                                           start_rsrcs_attrs ;
		StartNoneAttrs                                            start_none_attrs  ;
		::string                                                  start_stderr      ;
		Pdate                                                     eta               ;
		SubmitAttrs                                               submit_attrs      ;
		::vmap_ss                                                 rsrcs             ;
		Trace trace(BeChnl,"_s_handle_job_start",jrr) ;
		{	::unique_lock lock { _s_mutex } ;                                  // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			trace("entry",entry) ;
			submit_attrs = entry.submit_attrs ;
			rsrcs        = entry.rsrcs        ;
			//                              vvvvvvvvvvvvvvvvvvvvvvv
			ensure_nl(jrr.msg) ; jrr.msg += s_start(entry.tag,+job) ;
			//                              ^^^^^^^^^^^^^^^^^^^^^^^
			for( Req r : entry.reqs ) if (!r->zombie) goto Start ;
			trace("no_req") ;
			return false ;                                                     // no Req found, job has been cancelled but start message still arrives, give up
		Start :
			// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
			Rule::SimpleMatch match = job->simple_match() ;
			try {
				start_none_attrs = rule->start_none_attrs.eval(match,rsrcs) ;
			} catch (::string const& e) {
				/**/                 start_none_attrs  = rule->start_none_attrs.spec                            ;
				ensure_nl(jrr.msg) ; jrr.msg          += rule->start_none_attrs.s_exc_msg(true/*using_static*/) ;
				/**/                 start_stderr      = e                                                      ;
			}
			int  step     = 0                ;
			bool keep_tmp = false/*garbage*/ ;
			tie(eta,keep_tmp) = entry.req_info() ;
			keep_tmp |= start_none_attrs.keep_tmp ;
			try {
				deps_attrs        = rule->deps_attrs       .eval(match      )                                     ; step = 1 ;
				start_cmd_attrs   = rule->start_cmd_attrs  .eval(match,rsrcs)                                     ; step = 2 ;
				cmd               = rule->cmd              .eval(match,rsrcs)                                     ; step = 3 ;
				start_rsrcs_attrs = rule->start_rsrcs_attrs.eval(match,rsrcs)                                     ; step = 4 ;
				pre_actions       = job->pre_actions( match , submit_attrs.manual_ok , true/*mark_target_dirs*/ ) ; step = 5 ;
			} catch (::string const& e) {
				_s_small_ids.release(entry.conn.small_id) ;
				job_exec = { job , New , New } ;                               //  job starts and ends, no host
				Tag      tag             = entry.tag ;                         // record tag before releasing entry
				::string end_backend_msg ;
				trace("release_start_tab",job_exec,entry,step,e) ;
				_s_release_start_entry(it) ;
				switch (step) {
					case 0 : end_backend_msg = rule->deps_attrs       .s_exc_msg(false/*using_static*/) ; break ;
					case 1 : end_backend_msg = rule->start_cmd_attrs  .s_exc_msg(false/*using_static*/) ; break ;
					case 2 : end_backend_msg = rule->cmd              .s_exc_msg(false/*using_static*/) ; break ;
					case 3 : end_backend_msg = rule->start_rsrcs_attrs.s_exc_msg(false/*using_static*/) ; break ;
					case 4 : end_backend_msg = "cannot wash targets"                                    ; break ;
					default : FAIL(step) ;
				}
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				s_end( tag , +job , Status::EarlyErr ) ;                       // dont care about backend, job is dead for other reasons
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				JobDigest digest { .status=Status::EarlyErr , .deps=_mk_digest_deps(deps_attrs) , .stderr=e } ;
				trace("early_err",digest) ;
				{	OFStream ofs { dir_guard(job->ancillary_file()) } ;
					serialize( ofs , JobInfoStart({
						.eta          = eta
					,	.submit_attrs = submit_attrs
					,	.rsrcs        = rsrcs
					,	.host         = job_exec.host
					,	.pre_start    = jrr
					,	.start        = reply
					,	.stderr       = start_stderr
					}) ) ;
					serialize( ofs , JobInfoEnd( JobRpcReq(JobProc::End,jrr.job,jrr.seq_id,JobDigest(digest)) ) ) ;
				}
				if (step>0) job_exec->end_exec() ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , ::move(job_exec) , false/*report_now*/ , ::move(pre_actions.second) , ::move(start_stderr) , ::move(jrr.msg        ) ) ;
				g_engine_queue.emplace( JobProc::End   , ::move(job_exec) , ::move(rsrcs) , ::move(digest)                                          , ::move(end_backend_msg) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
			job_exec = { job , fd.peer_addr() , New } ;                        // job starts
			// simple attrs
			reply.addr                      = job_exec.host                      ;
			reply.autodep_env.auto_mkdir    = start_cmd_attrs.auto_mkdir         ;
			reply.autodep_env.ignore_stat   = start_cmd_attrs.ignore_stat        ;
			reply.autodep_env.lnk_support   = g_config.lnk_support               ;
			reply.autodep_env.reliable_dirs = g_config.reliable_dirs             ;
			reply.autodep_env.src_dirs_s    = g_src_dirs_s                       ;
			reply.autodep_env.root_dir      = *g_root_dir                        ;
			reply.autodep_env.tmp_dir       = ::move(tmp_dir               )     ; // tmp directory on disk
			reply.autodep_env.tmp_view      = ::move(start_cmd_attrs.tmp   )     ; // tmp directory as viewed by job
			reply.chroot                    = ::move(start_cmd_attrs.chroot)     ;
			reply.cmd                       = ::move(cmd                   )     ;
			reply.cwd_s                     = rule->cwd_s                        ;
			reply.hash_algo                 = g_config.hash_algo                 ;
			reply.interpreter               = rule->interpreter                  ;
			reply.keep_tmp                  = keep_tmp                           ;
			reply.kill_sigs                 = ::move(start_none_attrs.kill_sigs) ;
			reply.live_out                  = submit_attrs.live_out              ;
			reply.method                    = start_cmd_attrs.method             ;
			reply.remote_admin_dir          = g_config.remote_admin_dir          ;
			reply.small_id                  = small_id                           ;
			reply.timeout                   = start_rsrcs_attrs.timeout          ;
			reply.trace_n_jobs              = g_config.trace.n_jobs              ;
			reply.use_script                = start_cmd_attrs.use_script         ;
			// fancy attrs
			for( ::pair_ss& kv : start_cmd_attrs  .env ) reply.env.push_back(::move(kv)) ;
			for( ::pair_ss& kv : start_rsrcs_attrs.env ) reply.env.push_back(::move(kv)) ;
			for( ::pair_ss& kv : start_none_attrs .env ) reply.env.push_back(::move(kv)) ;
			//
			if ( rule->stdin_idx !=Rule::NoVar && +job->deps[rule->stdin_idx] ) reply.stdin  = deps_attrs[rule->stdin_idx ].second.first ;
			if ( rule->stdout_idx!=Rule::NoVar                                ) reply.stdout = targets   [rule->stdout_idx]              ;
			//
			for( auto [t,a] : pre_actions.first ) reply.pre_actions.emplace_back(t->name(),a) ;
			//
			reply.targets.reserve(targets.size()) ;
			for( VarIdx ti=0 ; ti<targets.size() ; ti++ ) reply.targets.emplace_back( targets[ti] , rule->tflags(ti) ) ;
			//
			reply.static_deps = _mk_digest_deps(deps_attrs) ;
			//
			entry.start         = job_exec.start_ ;
			entry.conn.host     = job_exec.host   ;
			entry.conn.port     = jrr.port        ;
			entry.conn.small_id = small_id        ;
		}
		//vvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send(fd,reply) ;
		//^^^^^^^^^^^^^^^^^^^^^^
		trace("started",reply) ;
		serialize(
			OFStream(dir_guard(job->ancillary_file()))
		,	JobInfoStart({
				.eta          = eta
			,	.submit_attrs = submit_attrs
			,	.rsrcs        = ::move(rsrcs)
			,	.host         = job_exec.host
			,	.pre_start    = jrr
			,	.start        = ::move(reply)
			,	.stderr       = start_stderr
			})
		) ;
		bool report_now = Delay(job->exec_time)>=start_none_attrs.start_delay ;                                                                   // dont defer long jobs
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( jrr.proc , JobExec(job_exec) , report_now , ::move(pre_actions.second) , ::move(start_stderr) , ::move(jrr.msg) ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (!report_now) _s_deferred_report_thread->emplace_at( job_exec.start_+start_none_attrs.start_delay , jrr.seq_id , ::move(job_exec) ) ;
		return false/*keep_fd*/ ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_mngt( JobRpcReq&& jrr , Fd fd ) {
		switch (jrr.proc) {
			case JobProc::None     : return false ;                            // if connection is lost, ignore it
			case JobProc::ChkDeps  :
			case JobProc::DepInfos : SWEAR(+fd,jrr.proc) ; break ;             // fd is needed to reply
			case JobProc::LiveOut  :                       break ;             // no reply
			default : FAIL(jrr.proc) ;
		}
		Job     job      { jrr.job } ;
		JobExec job_exec ;
		Trace trace(BeChnl,"_s_handle_job_mngt",jrr) ;
		{	::unique_lock lock { _s_mutex } ;                                  // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			job_exec = JobExec( job , entry.conn.host , entry.start , New ) ;
			trace("entry",job_exec,entry) ;
		}
		switch (jrr.proc) {
			case JobProc::ChkDeps  : //vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobProc::DepInfos : g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.digest.deps) ,                                fd ) ; return true /*keep_fd*/ ;
			case JobProc::LiveOut  : g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(jrr.msg)                                             ) ; return false/*keep_fd*/ ;
			case JobProc::Decode   : g_codec_queue .emplace( false/*encode*/ , ::move(jrr.msg) , ::move(jrr.file) , ::move(jrr.ctx)               , fd ) ; return true /*keep_fd*/ ;
			case JobProc::Encode   : g_codec_queue .emplace( true /*encode*/ , ::move(jrr.msg) , ::move(jrr.file) , ::move(jrr.ctx) , jrr.min_len , fd ) ; return true /*keep_fd*/ ;
			default : FAIL(jrr.proc) ; //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}

	bool/*keep_fd*/ Backend::_s_handle_job_end( JobRpcReq&& jrr , Fd ) {
		switch (jrr.proc) {
			case JobProc::None : return false ;                                // if connection is lost, ignore it
			case JobProc::End  : break ;                                       // no reply
			default : FAIL(jrr.proc) ;
		}
		Job       job         { jrr.job } ;
		JobExec   job_exec    ;
		::vmap_ss rsrcs       ;
		Trace trace(BeChnl,"_s_handle_job_end",jrr) ;
		{	::unique_lock lock { _s_mutex } ;                                  // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			job_exec = JobExec( job , entry.conn.host , entry.start , New ) ;
			rsrcs    = ::move(entry.rsrcs)                                  ;
			_s_small_ids.release(entry.conn.small_id) ;
			trace("release_start_tab",job_exec,entry) ;
			// if we have no fd, job end was invented by heartbeat, no acknowledge
			// acknowledge job end before telling backend as backend may wait the end of the job
			::string backend_msg ;
			bool     backend_ok  = true/*garbage*/ ;
			//                              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			::tie(backend_msg,backend_ok) = s_end( entry.tag , +job , jrr.digest.status ) ;
			//                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			ensure_nl(jrr.msg) ; jrr.msg += backend_msg ;
			if ( jrr.digest.status==Status::LateLost && !backend_msg ) { ensure_nl(jrr.msg) ; jrr.msg           += "vanished after start\n"                     ; }
			if ( is_lost(jrr.digest.status) && !backend_ok           )                        jrr.digest.status  = Status::Err                                  ;
			/**/                                                                              jrr.digest.status  = _s_release_start_entry(it,jrr.digest.status) ;
		}
		trace("info") ;
		serialize( OFStream(job->ancillary_file(),::ios::app) , JobInfoEnd(jrr) ) ;
		job->end_exec() ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( jrr.proc , ::move(job_exec) , ::move(rsrcs) , ::move(jrr.digest) , ::move(jrr.msg) ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return false/*keep_fd*/ ;
	}

	// kill all if req==0
	void Backend::_s_kill_req(ReqIdx req) {
		Trace trace(BeChnl,"s_kill_req",req) ;
		::vmap  <JobIdx,pair<StartEntry::Conn,Pdate>> to_wakeup ;
		{	::unique_lock lock { _s_mutex } ;                                  // lock for minimal time
			for( Tag t : Tag::N )
				if (s_ready(t))
					for( JobIdx j : s_tab[+t]->kill_req(req) )
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::NotStarted , JobExec(j,New,New) ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			for( auto jit = _s_start_tab.begin() ; jit!=_s_start_tab.end() ;) {            // /!\ we erase entries while iterating
				JobIdx      j = jit->first  ;
				StartEntry& e = jit->second ;
				if (req) {
					if ( e.reqs.size()==1 && e.reqs[0]==req ) goto Kill ;
					for( auto it = e.reqs.begin() ; it!=e.reqs.end() ; it++ ) { // e.reqs is a non-sorted vector, we must search req by hand
						if (*it!=req) continue ;
						e.reqs.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::Continue , JobExec(j,New,New) , Req(req) ) ; // job is for some other Req
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						trace("continue",j) ;
						break ;
					}
					jit++ ;
					continue ;
				}
			Kill :
				if (+e.start) { trace("wakeup"     ,j) ; to_wakeup.emplace_back(j,::pair(e.conn,e.start))                   ;                    jit++  ; }
				//                                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				else          { trace("not_started",j) ; g_engine_queue.emplace( JobProc::NotStarted , JobExec(j,New,New) ) ; _s_start_tab.erase(jit++) ; }
				//                                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
		}
		//                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( auto const& [j,c] : to_wakeup ) _s_wakeup_remote( j , c.first , c.second , JobServerRpcProc::Kill ) ;
		//                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	void Backend::_s_heartbeat_thread_func(::stop_token stop) {
		t_thread_key = 'H' ;
		Trace trace(BeChnl,"_heartbeat_thread_func") ;
		Pdate  last_wrap_around = Pdate::s_now() ;
		//
		StartEntry::Conn         conn         ;
		::pair_s<HeartbeatState> lost_report  = {{},HeartbeatState::Unknown} /*garbage*/ ;
		Status                   status       = Status::Unknown              /*garbage*/ ;
		Pdate                    eta          ;
		::vmap_ss                rsrcs        ;
		SubmitAttrs              submit_attrs ;
		Pdate                    start        ;
		for( JobIdx job=0 ;; job++ ) {
			{	::unique_lock lock { _s_mutex }                    ;           // lock _s_start_tab for minimal time
				auto          it   = _s_start_tab.lower_bound(job) ;
				if (it==_s_start_tab.end()) goto WrapAround ;
				//
				job = it->first ;                                              // job is now the next valid entry
				StartEntry& entry = it->second ;
				//
				if (!entry    )                      continue ;                // not a real entry                        ==> no check, no wait
				if (!entry.old) { entry.old = true ; continue ; }              // entry is too new, wait until next round ==> no check, no wait
				conn  = entry.conn  ;
				start = entry.start ;
				if (+start) goto Wakeup ;
				lost_report = s_heartbeat(entry.tag,job) ;
				if (lost_report.second==HeartbeatState::Alive) goto Next ;                                   // job is still alive
				if (!lost_report.first                       ) lost_report.first = "vanished before start" ;
				//
				Status hbs = lost_report.second==HeartbeatState::Err ? Status::EarlyLostErr : Status::LateLost ;
				//
				rsrcs        = ::move(entry.rsrcs       )     ;
				submit_attrs = ::move(entry.submit_attrs)     ;
				eta          = entry.req_info().first         ;
				status       = _s_release_start_entry(it,hbs) ;
				trace("handle_job",job,entry,status) ;
			}
			{	JobExec   je { job , New , New } ;                             // job starts and ends, no host
				JobDigest jd { .status=status  } ;
				if (status==Status::EarlyLostErr) {                                                                     // if we do not retry, record run info
					JobInfoStart jis { .eta=eta , .submit_attrs=submit_attrs , .rsrcs=rsrcs , .host=conn.host       } ;
					JobInfoEnd   jie { .end=JobRpcReq(JobProc::End,0,job,JobDigest(jd),::string(lost_report.first)) } ;
					OFStream     os  { dir_guard(je->ancillary_file())                                              } ;
					serialize( os , jis ) ;
					serialize( os , jie ) ;
				}
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , JobExec(je) , false/*report_now*/                                    ) ;
				g_engine_queue.emplace( JobProc::End   , ::move (je) , ::move(rsrcs) , ::move(jd) , ::move(lost_report.first) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Next ;
			}
		Wakeup :
			_s_wakeup_remote(job,conn,start,JobServerRpcProc::Heartbeat) ;
		Next :
			if (!g_config.heartbeat_tick.sleep_for(stop)) break ;                           // limit job checks
			continue ;
		WrapAround :
			job = 0 ;
			Delay d = g_config.heartbeat + g_config.network_delay ;                                        // ensure jobs have had a minimal time to start and signal it
			if ((last_wrap_around+d).sleep_until(stop)) { last_wrap_around = Pdate::s_now() ; continue ; } // limit job checks
			else                                        {                                     break    ; }
		}
		trace("done") ;
	}

	void Backend::s_config( ::array<Config::Backend,+Tag::N> const& config , bool dynamic ) {
		static ::jthread      heartbeat_thread      {    _s_heartbeat_thread_func                 } ;
		static JobExecThread  job_start_thread      {'S',_s_handle_job_start      ,1000/*backlog*/} ; _s_job_start_thread       = &job_start_thread       ;
		static JobExecThread  job_mngt_thread       {'M',_s_handle_job_mngt       ,1000/*backlog*/} ; _s_job_mngt_thread        = &job_mngt_thread        ;
		static JobExecThread  job_end_thread        {'E',_s_handle_job_end        ,1000/*backlog*/} ; _s_job_end_thread         = &job_end_thread         ;
		static DeferredThread deferred_report_thread{'R',_s_handle_deferred_report                } ; _s_deferred_report_thread = &deferred_report_thread ;
		static DeferredThread deferred_wakeup_thread{'W',_s_handle_deferred_wakeup                } ; _s_deferred_wakeup_thread = &deferred_wakeup_thread ;
		Trace trace(BeChnl,"s_config",STR(dynamic)) ;
		if (!dynamic) s_executable = *g_lmake_dir+"/_bin/job_exec" ;
		//
		::unique_lock lock{_s_mutex} ;
		for( Tag t : Tag::N ) {
			if (!s_tab[+t]            ) {                                                                                        trace("not_implemented",t  ) ; continue ; }
			if (!config[+t].configured) {                                             s_tab[+t]->config_err = "not configured" ; trace("not_configured" ,t  ) ; continue ; }
			try                         { s_tab[+t]->config(config[+t].dct,dynamic) ; s_tab[+t]->config_err.clear()            ; trace("ready"          ,t  ) ;            }
			catch (::string const& e)   { SWEAR(+e)                                 ; s_tab[+t]->config_err = e                ; trace("err"            ,t,e) ;            } // empty config_err ...
		}                                                                                                                                                                    // ... means ready
		job_start_thread.wait_started() ;
		job_mngt_thread .wait_started() ;
		job_end_thread  .wait_started() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , JobIdx job , ::vector<ReqIdx> const& reqs , ::vmap_ss&& rsrcs , SubmitAttrs const& submit_attrs ) {
		Trace trace(BeChnl,"acquire_cmd_line",tag,job,reqs,rsrcs,submit_attrs) ;
		SWEAR(!_s_mutex.try_lock()) ;                                            // check we have the lock to access _s_start_tab
		//
		SubmitRsrcsAttrs::s_canon(rsrcs) ;
		//
		auto        [it,fresh] = _s_start_tab.emplace(job,StartEntry()) ;      // create entry
		StartEntry& entry      = it->second                             ;
		entry.open() ;
		entry.tag   = tag           ;
		entry.reqs  = reqs          ;
		entry.rsrcs = ::move(rsrcs) ;
		if (fresh) {                                                    entry.submit_attrs = submit_attrs ;                                            }
		else       { uint8_t n_retries = entry.submit_attrs.n_retries ; entry.submit_attrs = submit_attrs ; entry.submit_attrs.n_retries = n_retries ; } // keep retry count if it was counting
		trace("create_start_tab",job,entry) ;
		::vector_s cmd_line {
			s_executable
		,	_s_job_start_thread->fd.service(g_config.backends[+tag].addr)
		,	_s_job_mngt_thread ->fd.service(g_config.backends[+tag].addr)
		,	_s_job_end_thread  ->fd.service(g_config.backends[+tag].addr)
		,	::to_string(entry.conn.seq_id)
		,	::to_string(job              )
		} ;
		trace("cmd_line",cmd_line) ;
		return cmd_line ;
	}

}
