// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

#include "codec.hh"

using namespace Disk   ;
using namespace Py     ;
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
		::unique_lock lock{Req::s_reqs_mutex} ; // taking Req::s_reqs_mutex is compulsery to derefence req
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

	::string                          Backend::s_executable              ;
	Backend*                          Backend::s_tab[N<Tag>]             ;
	::mutex                           Backend::_s_mutex                  ;
	::mutex                           Backend::_s_starting_job_mutex     ;
	::atomic<JobIdx>                  Backend::_s_starting_job           ;
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
			// if dep is accessed, pretend it is now
			if (+af.accesses) { res.emplace_back( d , DepDigest( af.accesses , file_date(nfs_guard.access(d)) , af.dflags , true/*parallel*/ ) ) ; }
			else                res.emplace_back( d , DepDigest( af.accesses ,                                  af.dflags , true/*parallel*/ ) ) ;
		}
		return res ;
	}

	static inline bool _localize( Tag t , ReqIdx ri ) {
		::unique_lock lock{Req::s_reqs_mutex} ;                                 // taking Req::s_reqs_mutex is compulsery to derefence req
		return Req(ri)->options.flags[ReqFlag::Local] || !Backend::s_ready(t) ; // if asked backend is not usable, force local execution
	}

	void Backend::s_submit( Tag tag , JobIdx ji , ReqIdx ri , SubmitAttrs&& submit_attrs , ::vmap_ss&& rsrcs ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_submit",tag,ji,ri,submit_attrs,rsrcs) ;
		//
		if ( tag!=Tag::Local && _localize(tag,ri) ) {
			SWEAR(+tag<N<Tag>) ;                                                             // prevent compiler array bound warning in next statement
			if (!s_tab[+tag]) throw to_string("backend ",snake(tag)," is not implemented") ;
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
			s_tab[+t]->add_pressure(j,ri,sa) ; // ask sub-backend to raise its priority
		} else {
			it->second.reqs.push_back(ri) ;    // note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;    // and update submit_attrs in case job was not actually started
		}
	}

	void Backend::s_set_pressure( Tag t , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		if (_localize(t,ri)) t = Tag::Local ;
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_set_pressure",t,j,ri,sa) ;
		s_tab[+t]->set_pressure(j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->set_pressure(j,ri,sa) ; // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;    // and update submit_attrs in case job was not actually started
	}

	void Backend::s_launch() {
		::unique_lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_launch") ;
		for( Tag t : All<Tag> ) if (s_ready(t)) {
			try {
				s_tab[+t]->launch() ;
			} catch (::vmap<JobIdx,pair_s<vmap_ss/*rsrcs*/>>& err_list) {
				for( auto&& [ji,re] : err_list ) {
					JobExec           je     { ji                       } ;
					Rule::SimpleMatch match  = je->simple_match()         ;
					JobDigest         digest { .status=Status::EarlyErr } ;
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
		{	::unique_lock lock { _s_mutex }                       ;                   // lock _s_start_tab for minimal time to avoid dead-locks
			auto          it   = _s_start_tab.find(+de.job_exec) ;
			if (it==_s_start_tab.end()           ) return ;                           // too late, job has ended
			if (it->second.conn.seq_id!=de.seq_id) return ;                           // too late, job has ended and restarted
		}
		Trace trace(BeChnl,"_s_handle_deferred_wakeup",de) ;
		JobDigest jd { .status=Status::LateLost } ;                                   // job is still present, must be really lost
		if (+de.job_exec.start_) jd.stats.total = Pdate::s_now()-de.job_exec.start_ ;
		_s_handle_job_end( JobRpcReq( JobProc::End , de.seq_id , +de.job_exec , ::move(jd) ) ) ;
	}

	void Backend::_s_wakeup_remote( JobIdx job , StartEntry::Conn const& conn , Pdate const& start , JobServerRpcProc proc ) {
		Trace trace(BeChnl,"_s_wakeup_remote",job,conn,proc) ;
		try {
			ClientSockFd fd(conn.host,conn.port) ;
			OMsgBuf().send( fd , JobServerRpcReq(proc,conn.seq_id,job) ) ; // XXX : straighten out Fd : Fd must not detach on mv and Epoll must take an AutoCloseFd as arg to take close resp.
		} catch (::string const& e) {
			trace("no_job",job,e) ;
			// if job cannot be connected to, assume it is dead and pretend it died if it still exists after network delay
			_s_deferred_wakeup_thread->emplace_after( g_config.network_delay , DeferredEntry(conn.seq_id,JobExec(Job(job),conn.host,start,New)) ) ;
		}
	}

	void Backend::_s_handle_deferred_report(DeferredEntry&& dre) {
		::unique_lock lock { _s_mutex }                       ;    // lock _s_start_tab for minimal time to avoid dead-locks
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
			if (n_retries!=0) {                                      // keep entry to keep on going retry count
				uint8_t nr = n_retries - 1 ;                         // we just try one time, note it
				it->second = {} ;                                    // clear other entries, as if we do not exist
				n_retries = nr ;
				return status ;
			}
			status = mk_err(status) ;
		}
		trace("erase") ;
		_s_start_tab.erase(it) ;
		return status ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_start( JobRpcReq&& jrr , SlaveSockFd const& fd ) {
		switch (jrr.proc) {
			case JobProc::None  : return false ;                                                           // if connection is lost, ignore it
			case JobProc::Start : SWEAR(+fd,jrr.proc) ; break ;                                            // fd is needed to reply
		DF}
		Job                                        job               { jrr.job }           ;
		JobExec                                    job_exec          ;
		Rule                                       rule              = job->rule           ;
		Rule::SimpleMatch                          match             = job->simple_match() ;
		JobRpcReply                                reply             { JobProc::Start }    ;
		::pair<vmap<Node,FileAction>,vector<Node>> pre_actions       ;
		StartCmdAttrs                              start_cmd_attrs   ;
		::pair_ss/*script,call*/                   cmd               ;
		::vmap_s<pair_s<AccDflags>>                deps_attrs        ;
		StartRsrcsAttrs                            start_rsrcs_attrs ;
		StartNoneAttrs                             start_none_attrs  ;
		::pair_ss                                  start_msg_err     ;
		Pdate                                      eta               ;
		SubmitAttrs                                submit_attrs      ;
		::vmap_ss                                  rsrcs             ;
		Trace trace(BeChnl,"_s_handle_job_start",jrr) ;
		_s_starting_job = jrr.job ;
		::unique_lock lock { _s_starting_job_mutex } ;
		{	::unique_lock lock { _s_mutex } ;                                                              // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			trace("entry",entry) ;
			if (!entry.useful()) { trace("useless") ; return false ; }                                     // no Req found, job has been cancelled but start message still arrives, give up
			submit_attrs = ::move(entry.submit_attrs) ;
			rsrcs        =        entry.rsrcs         ;
			//                               vvvvvvvvvvvvvvvvvvvvvvv
			append_line_to_string( jrr.msg , s_start(entry.tag,+job) ) ;
			//                               ^^^^^^^^^^^^^^^^^^^^^^^
			// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
			try {
				start_none_attrs = rule->start_none_attrs.eval(match,rsrcs,&::ref(::vmap_s<Accesses>())) ; // ignore deps of start_none_attrs as this is not critical (no impact on targets)
			} catch (::pair_ss const& msg_err) {
				/**/              start_none_attrs  = rule->start_none_attrs.spec                            ;
				set_nl(jrr.msg) ; jrr.msg          += rule->start_none_attrs.s_exc_msg(true/*using_static*/) ;
				/**/              start_msg_err     = msg_err                                                ;
			}
			::vmap_s<Accesses>& deps     = submit_attrs.deps ;
			int                 step     = 0                 ;
			bool                keep_tmp = false/*garbage*/  ;
			tie(eta,keep_tmp) = entry.req_info() ;
			keep_tmp |= start_none_attrs.keep_tmp ;
			try {
				deps_attrs        = rule->deps_attrs       .eval(match            ) ; step = 1 ;
				start_cmd_attrs   = rule->start_cmd_attrs  .eval(match,rsrcs,&deps) ; step = 2 ;
				cmd               = rule->cmd              .eval(match,rsrcs,&deps) ; step = 3 ;
				start_rsrcs_attrs = rule->start_rsrcs_attrs.eval(match,rsrcs,&deps) ; step = 4 ;
				//
				try                       { start_cmd_attrs.chk(start_rsrcs_attrs.method) ; }
				catch (::string const& e) { throw ::pair_ss/*msg,err*/(e,{}) ;              }
				step = 5 ;
				//
				pre_actions = job->pre_actions( match , true/*mark_target_dirs*/ ) ; step = 6 ;
				if (+deps) {
					::umap_s<VarIdx> static_dep_idxes ; for( VarIdx i=0 ; i<deps_attrs.size() ; i++ ) static_dep_idxes[deps_attrs[i].second.first] = i ;
					for( auto const& [d,a] : deps )
						if ( auto it=static_dep_idxes.find(d) ; it!=static_dep_idxes.end() ) deps_attrs[it->second].second.second.accesses = a ;
						else                                                                 throw ::pair_ss/*msg,err*/(to_string("cannot access non static dep ",d),{}) ;
				}
			} catch (::pair_ss const& msg_err) {
				_s_small_ids.release(entry.conn.small_id) ;
				job_exec = { job , New , New } ;                                                           // job starts and ends, no host
				Tag tag = entry.tag ;                                                                      // record tag before releasing entry
				append_line_to_string(start_msg_err.first  , msg_err.first  ) ;
				append_line_to_string(start_msg_err.second , msg_err.second ) ;
				trace("release_start_tab",job_exec,entry,step,msg_err) ;
				_s_release_start_entry(it) ;
				switch (step) {
					case 0 : append_line_to_string( start_msg_err.first , rule->deps_attrs       .s_exc_msg(false/*using_static*/) ) ; break ;
					case 1 : append_line_to_string( start_msg_err.first , rule->start_cmd_attrs  .s_exc_msg(false/*using_static*/) ) ; break ;
					case 2 : append_line_to_string( start_msg_err.first , rule->cmd              .s_exc_msg(false/*using_static*/) ) ; break ;
					case 3 : append_line_to_string( start_msg_err.first , rule->start_rsrcs_attrs.s_exc_msg(false/*using_static*/) ) ; break ;
					case 4 :                                                                                                           break ;
					case 5 : append_line_to_string( start_msg_err.first , "cannot wash targets"                                    ) ; break ;
					case 6 :                                                                                                           break ;
				DF}
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				s_end( tag , +job , Status::EarlyErr ) ;                                                   // dont care about backend, job is dead for other reasons
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				JobDigest digest { .status=Status::EarlyErr , .stderr=msg_err.second } ;
				trace("early_err",digest) ;
				{	OFStream ofs { dir_guard(job->ancillary_file()) } ;
					serialize( ofs , JobInfoStart({
						.eta          = eta
					,	.submit_attrs = submit_attrs
					,	.rsrcs        = rsrcs
					,	.host         = job_exec.host
					,	.pre_start    = jrr
					,	.start        = reply
					,	.stderr       = start_msg_err.second
					}) ) ;
					serialize( ofs , JobInfoEnd( JobRpcReq(JobProc::End,jrr.job,jrr.seq_id,JobDigest(digest)) ) ) ;
				}
				if (step>0) job_exec->end_exec() ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , ::move(job_exec) , false/*report_now*/ , ::move(pre_actions.second) , ::move(start_msg_err.second) , ::move(jrr.msg            ) ) ;
				g_engine_queue.emplace( JobProc::End   , ::move(job_exec) , ::move(rsrcs) , ::move(digest)                                                  , ::move(start_msg_err.first) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				return false ;
			}
			//
			SmallId small_id = _s_small_ids.acquire() ;
			//
			::string tmp_dir = keep_tmp ?
				to_string(*g_root_dir,'/',job->ancillary_file(AncillaryTag::KeepTmp))
			:	to_string(g_config.remote_tmp_dir,'/',small_id)
			;
			//
			job_exec = { job , fd.peer_addr() , New } ;                                                    // job starts
			// simple attrs
			reply.addr                      = job_exec.host                      ;
			reply.autodep_env.auto_mkdir    = start_cmd_attrs.auto_mkdir         ;
			reply.autodep_env.ignore_stat   = start_cmd_attrs.ignore_stat        ;
			reply.autodep_env.lnk_support   = g_config.lnk_support               ;
			reply.autodep_env.reliable_dirs = g_config.reliable_dirs             ;
			reply.autodep_env.src_dirs_s    = g_src_dirs_s                       ;
			reply.autodep_env.root_dir      = *g_root_dir                        ;
			reply.autodep_env.tmp_dir       = ::move(tmp_dir               )     ;                         // tmp directory on disk
			reply.autodep_env.tmp_view      = ::move(start_cmd_attrs.tmp   )     ;                         // tmp directory as viewed by job
			reply.chroot                    = ::move(start_cmd_attrs.chroot)     ;
			reply.cmd                       = ::move(cmd                   )     ;
			reply.cwd_s                     = rule->cwd_s                        ;
			reply.hash_algo                 = g_config.hash_algo                 ;
			reply.interpreter               = rule->interpreter                  ;
			reply.keep_tmp                  = keep_tmp                           ;
			reply.kill_sigs                 = ::move(start_none_attrs.kill_sigs) ;
			reply.live_out                  = submit_attrs.live_out              ;
			reply.method                    = start_rsrcs_attrs.method           ;
			reply.network_delay             = g_config.network_delay             ;
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
			VarIdx ti = 0 ;
			for( ::string const& tn : match.static_matches() ) reply.static_matches.emplace_back( tn , rule->matches[ti++].second.flags ) ;
			for( ::string const& p  : match.star_patterns () ) reply.star_matches  .emplace_back( p  , rule->matches[ti++].second.flags ) ;
			//
			if (rule->stdin_idx !=Rule::NoVar) reply.stdin  = deps_attrs          [rule->stdin_idx ].second.first ;
			if (rule->stdout_idx!=Rule::NoVar) reply.stdout = reply.static_matches[rule->stdout_idx].first        ;
			//
			for( auto [t,a] : pre_actions.first ) reply.pre_actions.emplace_back(t->name(),a) ;
			//
			reply.static_deps = _mk_digest_deps(deps_attrs) ;
			//
			entry.start         = job_exec.start_ ;
			entry.conn.host     = job_exec.host   ;
			entry.conn.port     = jrr.port        ;
			entry.conn.small_id = small_id        ;
		}
		trace("started",reply) ;
		//vvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send(fd,reply) ; // send reply ASAP to minimize overhead, but after we have the _s_ancillary_file_mutex to ensure _s_handle_job_end is not executed too soon
		//^^^^^^^^^^^^^^^^^^^^^^
		serialize(
			OFStream(dir_guard(job->ancillary_file()))
		,	JobInfoStart({
				.rule_cmd_crc =        rule->cmd_crc
			,	.stems        = ::move(match.stems  )
			,	.eta          =        eta
			,	.submit_attrs =        submit_attrs
			,	.rsrcs        = ::move(rsrcs        )
			,	.host         =        job_exec.host
			,	.pre_start    =        jrr
			,	.start        = ::move(reply        )
			,	.stderr       =        start_msg_err.second
			})
		) ;
		bool report_now = +pre_actions.second || +start_msg_err.second || Delay(job->exec_time)>=start_none_attrs.start_delay ; // dont defer long jobs or if a message is to be delivered to user
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( JobProc::Start , ::copy(job_exec) , report_now , ::move(pre_actions.second) , ::move(start_msg_err.second) , jrr.msg+start_msg_err.first ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (!report_now) _s_deferred_report_thread->emplace_at( job_exec.start_+start_none_attrs.start_delay , jrr.seq_id , ::move(job_exec) ) ;
		return false/*keep_fd*/ ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_mngt( JobRpcReq&& jrr , SlaveSockFd const& fd ) {
		switch (jrr.proc) {
			case JobProc::None     : return false ;                // if connection is lost, ignore it
			case JobProc::ChkDeps  :
			case JobProc::DepInfos :
			case JobProc::Decode   :
			case JobProc::Encode   : SWEAR(+fd,jrr.proc) ; break ; // fd is needed to reply
			case JobProc::LiveOut  :                       break ; // no reply
		DF}
		Job job { jrr.job } ;
		Trace trace(BeChnl,"_s_handle_job_mngt",jrr) ;
		{	::unique_lock lock { _s_mutex } ;                      // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//                                                                                                                                          keep_fd
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			trace("entry",job,entry) ;
			switch (jrr.proc) { //!      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv         keep_fd
				case JobProc::ChkDeps  :
				case JobProc::DepInfos : g_engine_queue      . emplace( jrr.proc , JobExec(job,entry.conn.host,entry.start,New) , ::move(jrr.digest.deps)          , fd ) ; return true  ;
				case JobProc::LiveOut  : g_engine_queue      . emplace( jrr.proc , JobExec(job,entry.conn.host,entry.start,New) , ::move(jrr.msg)                       ) ; return false ;
				case JobProc::Decode   : Codec::g_codec_queue->emplace( jrr.proc , ::move(jrr.msg) , ::move(jrr.file) , ::move(jrr.ctx) ,               entry.reqs , fd ) ; return true  ;
				case JobProc::Encode   : Codec::g_codec_queue->emplace( jrr.proc , ::move(jrr.msg) , ::move(jrr.file) , ::move(jrr.ctx) , jrr.min_len , entry.reqs , fd ) ; return true  ;
				//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			DF}
		}
	}

	bool/*keep_fd*/ Backend::_s_handle_job_end( JobRpcReq&& jrr , SlaveSockFd const& ) {
		switch (jrr.proc) {
			case JobProc::None : return false ;                                     // if connection is lost, ignore it
			case JobProc::End  : break ;                                            // no reply
		DF}
		Job       job      { jrr.job } ;
		JobExec   job_exec ;
		::vmap_ss rsrcs    ;
		Trace trace(BeChnl,"_s_handle_job_end",jrr) ;
		if (jrr.job==_s_starting_job) ::unique_lock lock{_s_starting_job_mutex} ;   // ensure _s_handled_job_start is done for this job
		{	::unique_lock lock { _s_mutex } ;                                       // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()       ) { trace("not_in_tab"                             ) ; return false ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jrr.seq_id) { trace("bad_seq_id",entry.conn.seq_id,jrr.seq_id) ; return false ; }
			job_exec = JobExec( job , entry.conn.host , entry.start , New ) ;
			rsrcs    = ::move(entry.rsrcs)                                  ;
			_s_small_ids.release(entry.conn.small_id) ;
			trace("release_start_tab",job_exec,entry) ;
			// if we have no fd, job end was invented by heartbeat, no acknowledge
			// acknowledge job end before telling backend as backend may wait the end of the job
			//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			auto [msg,ok] = s_end( entry.tag , +job , jrr.digest.status ) ;
			//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			set_nl(jrr.msg) ; jrr.msg += msg ;
			if ( jrr.digest.status==Status::LateLost && !msg ) jrr.msg           += "vanished after start\n"                     ;
			if ( is_lost(jrr.digest.status) && !ok           ) jrr.digest.status  = Status::LateLostErr                          ;
			/**/                                               jrr.digest.status  = _s_release_start_entry(it,jrr.digest.status) ;
		}
		trace("info") ;
		for( auto& [dn,dd] : jrr.digest.deps ) {
			if (!dd.is_date) continue ;                                             // fast path
			Dep dep { Node(dn) , dd } ;
			dep.acquire_crc() ;
			dd.crc_date(dep) ;
		}
		serialize( OFStream(job->ancillary_file(),::ios::app) , JobInfoEnd(jrr) ) ; // /!\ _s_starting_job ensures ancillary file is written by _s_handle_job_start before we append to it
		job->end_exec() ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( JobProc::End , ::move(job_exec) , ::move(rsrcs) , ::move(jrr.digest) , ::move(jrr.msg) ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return false/*keep_fd*/ ;
	}

	// kill all if ri==0
	void Backend::_s_kill_req(ReqIdx ri) {
		Trace trace(BeChnl,"s_kill_req",ri) ;
		Req                                         req       { ri } ;
		::vmap<JobIdx,pair<StartEntry::Conn,Pdate>> to_wakeup ;
		{	::unique_lock lock { _s_mutex } ;                                                                       // lock for minimal time
			for( Tag t : All<Tag> ) if (s_ready(t))
				for( JobIdx j : s_tab[+t]->kill_waiting_jobs(ri) ) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::GiveUp , JobExec(j,New,New) , req , false/*report*/ ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("waiting",j) ;
				}
			for( auto jit = _s_start_tab.begin() ; jit!=_s_start_tab.end() ;) {                                     // /!\ we erase entries while iterating
				JobIdx      j = jit->first  ;
				StartEntry& e = jit->second ;
				if (ri) {
					if ( e.reqs.size()==1 && e.reqs[0]==ri ) goto Kill ;
					for( auto it = e.reqs.begin() ; it!=e.reqs.end() ; it++ ) {                                     // e.reqs is a non-sorted vector, we must search ri by hand
						if (*it!=ri) continue ;
						e.reqs.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( JobProc::GiveUp , JobExec(j,New,New) , req , +e.start/*report*/ ) ; // job is useful for some other Req
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						trace("continue",j) ;
						break ;
					}
					jit++ ;
					continue ;
				}
			Kill :
				if (+e.start) {
					trace("kill",j) ;
					to_wakeup.emplace_back(j,::pair(e.conn,e.start)) ;
					jit++ ;
				} else {
					trace("not_started",j) ;
					s_tab[+e.tag]->kill_job(j) ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( JobProc::GiveUp , JobExec(j,New,New) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					_s_start_tab.erase(jit++) ;
				}
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
		::pair_s<HeartbeatState> lost_report  = {}         /*garbage*/ ;
		Status                   status       = Status::New/*garbage*/ ;
		Pdate                    eta          ;
		::vmap_ss                rsrcs        ;
		SubmitAttrs              submit_attrs ;
		Pdate                    start        ;
		for( JobIdx job=0 ;; job++ ) {
			{	::unique_lock lock { _s_mutex }                    ;                                         // lock _s_start_tab for minimal time
				auto          it   = _s_start_tab.lower_bound(job) ;
				if (it==_s_start_tab.end()) goto WrapAround ;
				//
				job = it->first ;                                                                            // job is now the next valid entry
				StartEntry& entry = it->second ;
				//
				if (!entry    )                      continue ;                                              // not a real entry                        ==> no check, no wait
				if (!entry.old) { entry.old = true ; continue ; }                                            // entry is too new, wait until next round ==> no check, no wait
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
			{	JobExec   je { job , New , New } ;                                                           // job starts and ends, no host
				JobDigest jd { .status=status  } ;
				if (status==Status::EarlyLostErr) {                                                          // if we do not retry, record run info
					JobInfoStart jis {
						.eta          = eta
					,	.submit_attrs = submit_attrs
					,	.rsrcs        = rsrcs
					,	.host         = conn.host
					,	.pre_start    { JobProc::None , conn.seq_id , job }
					,	.start        { JobProc::None                     }
					} ;
					JobInfoEnd jie {
						.end { JobProc::End , conn.seq_id , job , ::copy(jd) , ::copy(lost_report.first) }
					} ;
					OFStream os { dir_guard(je->ancillary_file()) } ;
					serialize( os , jis ) ;
					serialize( os , jie ) ;
				}
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( JobProc::Start , ::copy(je) , false/*report_now*/                                    ) ;
				g_engine_queue.emplace( JobProc::End   , ::move(je) , ::move(rsrcs) , ::move(jd) , ::move(lost_report.first) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Next ;
			}
		Wakeup :
			_s_wakeup_remote(job,conn,start,JobServerRpcProc::Heartbeat) ;
		Next :
			if (!g_config.heartbeat_tick.sleep_for(stop)) break ;                                            // limit job checks
			continue ;
		WrapAround :
			job = 0 ;
			Delay d = g_config.heartbeat + g_config.network_delay ;                                          // ensure jobs have had a minimal time to start and signal it
			if ((last_wrap_around+d).sleep_until(stop)) { last_wrap_around = Pdate::s_now() ; continue ; }   // limit job checks
			else                                        {                                     break    ; }
		}
		trace("done") ;
	}

	void Backend::s_config( ::array<Config::Backend,N<Tag>> const& config , bool dynamic ) {
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
		for( Tag t : All<Tag> ) if (+t) {
			Backend*               be  = s_tab [+t] ;
			Config::Backend const& cfg = config[+t] ;
			if (!be            ) {                                     trace("not_implemented",t  ) ; continue ; }
			if (!cfg.configured) { be->config_err = "not configured" ; trace("not_configured" ,t  ) ; continue ; }
			if (!be->is_local()) {
				::string ifce ;
				if (+cfg.ifce) {
					Gil gil ;
					try {
						Ptr<Dict> glbs = py_run(cfg.ifce) ;
						ifce = (*glbs)["interface"].as_a<Str>() ;
					} catch (::string const& e) {
						throw to_string("bad interface for ",snake(t),'\n',indent(e,1)) ;
					}
				} else {
					ifce = host() ;
				}
				be->addr = ServerSockFd::s_addr(ifce) ;
			}
			try                       { be->config(cfg.dct,dynamic) ; be->config_err.clear() ; trace("ready",t  ) ; }
			catch (::string const& e) { SWEAR(+e)                   ; be->config_err = e     ; trace("err"  ,t,e) ; } // empty config_err means ready
		}
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
		auto        [it,fresh] = _s_start_tab.emplace(job,StartEntry()) ;        // create entry
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
		,	_s_job_start_thread->fd.service(s_tab[+tag]->addr)
		,	_s_job_mngt_thread ->fd.service(s_tab[+tag]->addr)
		,	_s_job_end_thread  ->fd.service(s_tab[+tag]->addr)
		,	::to_string(entry.conn.seq_id)
		,	::to_string(job              )
		} ;
		trace("cmd_line",cmd_line) ;
		return cmd_line ;
	}

}
