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

	void send_reply( JobIdx job , JobMngtRpcReply&& jmrr ) {
		Lock lock { Backend::_s_mutex }             ;
		auto it   = Backend::_s_start_tab.find(job) ;
		if (it==Backend::_s_start_tab.end()) return ;         // job is dead without waiting for reply, curious but possible
		Backend::StartEntry const& e = it->second ;
		try {
			jmrr.seq_id = e.conn.seq_id ;
			ClientSockFd fd( e.conn.host , e.conn.port , 3/*n_trials*/ ) ;
			OMsgBuf().send( fd , jmrr ) ;
		} catch (...) {                                       // if we cannot connect to job, assume it is dead while we processed the request
			Backend::_s_deferred_wakeup_thread.emplace_after(
				g_config->network_delay
			,	Backend::DeferredEntry { e.conn.seq_id , JobExec(Job(job),e.conn.host,e.start_date) }
			) ;
		}
	}

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
		bool  keep_tmp = false               ;
		Lock  lock     { Req::s_reqs_mutex } ; // taking Req::s_reqs_mutex is compulsery to derefence req
		for( ReqIdx r : reqs ) {
			Req req{r} ;
			keep_tmp |= req->options.flags[ReqFlag::KeepTmp]  ;
			eta       = +eta ? ::min(eta,req->eta) : req->eta ;
		}
		return {eta,keep_tmp} ;
	}

	Status Backend::StartTab::release( ::map<JobIdx,StartEntry>::iterator it , Status status ) {
		Trace trace(BeChnl,"release",it->first,status) ;
		if ( is_lost(status) && is_ok(status)==Maybe ) {
			uint8_t& n_retries = it->second.submit_attrs.n_retries ;
			if (n_retries!=0) {                                      // keep entry to keep on going retry count
				uint8_t nr = n_retries - 1 ;                         // record trial and save value
				it->second = {} ;                                    // clear other entries, as if we do not exist
				n_retries = nr ;                                     // restore value
				return status ;
			}
			status = mk_err(status) ;
		}
		trace("erase") ;
		erase(it) ;
		return status ;
	}

	//
	// Backend
	//

	::string                  Backend::s_executable              ;
	Backend*                  Backend::s_tab[N<Tag>]             = {} ;
	Mutex<MutexLvl::Backend > Backend::_s_mutex                  ;
	Mutex<MutexLvl::StartJob> Backend::_s_starting_job_mutex     ;
	::atomic<JobIdx>          Backend::_s_starting_job           ;
	Backend::StartTab         Backend::_s_start_tab              ;
	SmallIds<SmallId>         Backend::_s_small_ids              ;
	Backend::JobThread        Backend::_s_job_start_thread       ;
	Backend::JobMngtThread    Backend::_s_job_mngt_thread        ;
	Backend::JobThread        Backend::_s_job_end_thread         ;
	Backend::DeferredThread   Backend::_s_deferred_report_thread ;
	Backend::DeferredThread   Backend::_s_deferred_wakeup_thread ;

	static ::vmap_s<DepDigest> _mk_digest_deps( ::vmap_s<DepSpec>&& deps_attrs ) {
		::vmap_s<DepDigest> res ; res.reserve(deps_attrs.size()) ;
		for( auto& [_,d] : deps_attrs ) res.emplace_back( ::move(d.txt) , DepDigest( {} , d.dflags , true/*parallel*/ ) ) ;
		return res ;
	}

	static bool _localize( Tag t , ReqIdx ri ) {
		Lock lock{Req::s_reqs_mutex} ;                                          // taking Req::s_reqs_mutex is compulsery to derefence req
		return Req(ri)->options.flags[ReqFlag::Local] || !Backend::s_ready(t) ; // if asked backend is not usable, force local execution
	}

	void Backend::s_submit( Tag tag , JobIdx ji , ReqIdx ri , SubmitAttrs&& submit_attrs , ::vmap_ss&& rsrcs ) {
		SWEAR(+tag) ;
		Lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_submit",tag,ji,ri,submit_attrs,rsrcs) ;
		//
		if ( tag!=Tag::Local && _localize(tag,ri) ) {
			SWEAR(+tag<N<Tag>) ;                                                   // prevent compiler array bound warning in next statement
			if (!s_tab[+tag]) throw "backend "s+snake(tag)+" is not implemented" ;
			rsrcs = s_tab[+tag]->mk_lcl( ::move(rsrcs) , s_tab[+Tag::Local]->capacity() ) ;
			tag   = Tag::Local                                                            ;
			trace("local",rsrcs) ;
		}
		if (!s_ready(tag)) throw "local backend is not available"s ;
		submit_attrs.tag = tag ;
		s_tab[+tag]->submit(ji,ri,submit_attrs,::move(rsrcs)) ;
	}

	void Backend::s_add_pressure( Tag tag , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		SWEAR(+tag) ;
		if (_localize(tag,ri)) tag = Tag::Local ;
		Lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_add_pressure",tag,j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) {
			s_tab[+tag]->add_pressure(j,ri,sa) ; // ask sub-backend to raise its priority
		} else {
			it->second.reqs.push_back(ri) ;      // note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;      // and update submit_attrs in case job was not actually started
		}
	}

	void Backend::s_set_pressure( Tag tag , JobIdx j , ReqIdx ri , SubmitAttrs const& sa ) {
		SWEAR(+tag) ;
		if (_localize(tag,ri)) tag = Tag::Local ;
		Lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_set_pressure",tag,j,ri,sa) ;
		s_tab[+tag]->set_pressure(j,ri,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+tag]->set_pressure(j,ri,sa) ; // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;      // and update submit_attrs in case job was not actually started
	}

	void Backend::_s_handle_deferred_wakeup(DeferredEntry&& de) {
		Trace trace(BeChnl,"_s_handle_deferred_wakeup",de) ;
		{	Lock lock { _s_mutex } ;                                                      // lock _s_start_tab for minimal time to avoid dead-locks
			if (_s_start_tab.find(+de.job_exec,de.seq_id)==_s_start_tab.end()) return ;   // too late, job has ended
		}
		JobDigest jd { .status=Status::LateLost } ;                                       // job is still present, must be really lost
		if (+de.job_exec.start_date) jd.stats.total = Pdate(New)-de.job_exec.start_date ;
		trace("lost",jd) ;
		_s_handle_job_end( JobRpcReq( Proc::End , de.seq_id , +de.job_exec , ::move(jd) ) ) ;
	}

	void Backend::_s_wakeup_remote( JobIdx job , StartEntry::Conn const& conn , Pdate start_date , JobMngtProc proc ) {
		Trace trace(BeChnl,"_s_wakeup_remote",job,conn,proc) ;
		SWEAR(conn.seq_id,job,conn) ;
		try {
			ClientSockFd fd(conn.host,conn.port) ;
			OMsgBuf().send( fd , JobMngtRpcReply(proc,conn.seq_id) ) ;
		} catch (::string const& e) {
			trace("no_job",job,e) ;
			// if job cannot be connected to, assume it is dead and pretend it died if it still exists after network delay
			_s_deferred_wakeup_thread.emplace_after( g_config->network_delay , DeferredEntry{conn.seq_id,JobExec(Job(job),conn.host,start_date)} ) ;
		}
	}

	void Backend::_s_handle_deferred_report(DeferredEntry&& dre) {
		Lock lock { _s_mutex }                       ;                                // lock _s_start_tab for minimal time to avoid dead-locks
		if (_s_start_tab.find(+dre.job_exec,dre.seq_id)==_s_start_tab.end()) return ;
		Trace trace(BeChnl,"_s_handle_deferred_report",dre) ;
		g_engine_queue.emplace( Proc::ReportStart , ::move(dre.job_exec) ) ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_start( JobRpcReq&& jrr , SlaveSockFd const& fd ) {
		switch (jrr.proc) {
			case Proc::None  : return false ;                    // if connection is lost, ignore it
			case Proc::Start : SWEAR(+fd,jrr.proc) ; break ;     // fd is needed to reply
		DF}
		Job                      job                 { jrr.job }           ;
		JobExec                  job_exec            ;
		Rule                     rule                = job->rule           ;
		Rule::SimpleMatch        match               = job->simple_match() ;
		JobRpcReply              reply               { Proc::Start }       ;
		vmap<Node,FileAction>    pre_actions         ;
		vmap<Node,FileActionTag> pre_action_warnings ;
		StartCmdAttrs            start_cmd_attrs     ;
		::pair_ss/*script,call*/ cmd                 ;
		::vmap_s<DepSpec>        deps_attrs          ;
		StartRsrcsAttrs          start_rsrcs_attrs   ;
		StartNoneAttrs           start_none_attrs    ;
		::pair_ss                start_msg_err       ;
		SubmitAttrs              submit_attrs        ;
		::vmap_ss                rsrcs               ;
		Pdate                    eta                 ;
		bool                     keep_tmp            = false/*garbage*/    ;
		::vector<ReqIdx>         reqs                ;
		Trace trace(BeChnl,"_s_handle_job_start",jrr) ;
		_s_starting_job = jrr.job ;
		Lock lock { _s_starting_job_mutex } ;
		// to lock for minimal time, we lock twice
		// 1st time, we only gather info, real decisions will be taken when we lock the 2nd time
		// because the only thing that can happend between the 2 locks is that entry disappears, we can move info from entry during 1st lock
		{	Lock lock { _s_mutex } ;                             // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job,jrr.seq_id) ; if (it==_s_start_tab.end()) { trace("not_in_tab") ; return false ; }
			StartEntry& entry = it->second                         ;
			trace("entry1",entry) ;
			submit_attrs      = ::move(entry.submit_attrs) ;
			rsrcs             =        entry.rsrcs         ;
			reqs              =        entry.reqs          ;
			tie(eta,keep_tmp) = entry.req_info()           ;
		}
		trace("submit_attrs",submit_attrs) ;
		::vmap_s<DepDigest>& deps          = submit_attrs.deps ;
		size_t               n_submit_deps = deps.size()       ;
		int                  step          = 0                 ;
		deps_attrs = rule->deps_attrs.eval(match) ;              // this cannot fail as it was already run to construct job
		try {
			cmd               = rule->cmd              .eval(match,rsrcs,&deps) ; step = 1 ;
			start_cmd_attrs   = rule->start_cmd_attrs  .eval(match,rsrcs,&deps) ; step = 2 ;
			start_rsrcs_attrs = rule->start_rsrcs_attrs.eval(match,rsrcs,&deps) ; step = 3 ;
			//
			pre_actions = job->pre_actions( match , true/*mark_target_dirs*/ ) ; step = 5 ;
			for( auto const& [t,a] : pre_actions )
				switch (a.tag) {
					case FileActionTag::UnlinkWarning  :
					case FileActionTag::UnlinkPolluted : pre_action_warnings.emplace_back(t,a.tag) ; ; break ;
					default : ;
				}
		} catch (::pair_ss const& msg_err) {
			start_msg_err.first  <<set_nl<< msg_err.first  ;
			start_msg_err.second <<set_nl<< msg_err.second ;
			switch (step) {
				case 0 : start_msg_err.first <<set_nl<< rule->cmd              .s_exc_msg(false/*using_static*/) ; break ;
				case 1 : start_msg_err.first <<set_nl<< rule->start_cmd_attrs  .s_exc_msg(false/*using_static*/) ; break ;
				case 2 : start_msg_err.first <<set_nl<< rule->start_rsrcs_attrs.s_exc_msg(false/*using_static*/) ; break ;
				case 3 : start_msg_err.first <<set_nl<< "cannot wash targets"                                    ; break ;
			DF}
		}
		trace("deps",step,deps) ;
		// record as much info as possible in reply
		::uset_s env_keys ;
		switch (step) {
			case 5 :
				// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
				try {
					start_none_attrs = rule->start_none_attrs.eval(match,rsrcs,&deps) ;
				} catch (::pair_ss const& msg_err) {
					start_none_attrs  = rule->start_none_attrs.spec ;
					start_msg_err     = msg_err                     ;
					jrr.msg <<set_nl<< rule->start_none_attrs.s_exc_msg(true/*using_static*/) ;
				}
				keep_tmp |= start_none_attrs.keep_tmp ;
				//
				for( auto [t,a] : pre_actions ) reply.pre_actions.emplace_back(t->name(),a) ;
			[[fallthrough]] ;
			case 3 :
				reply.method  = start_rsrcs_attrs.method  ;
				reply.timeout = start_rsrcs_attrs.timeout ;
				//
				for( ::pair_ss& kv : start_rsrcs_attrs.env ) { env_keys.insert(kv.first) ; reply.env.push_back(::move(kv)) ; }
			[[fallthrough]] ;
			case 2 :
				reply.interpreter             = ::move(start_cmd_attrs.interpreter) ;
				reply.autodep_env.auto_mkdir  =        start_cmd_attrs.auto_mkdir   ;
				reply.autodep_env.ignore_stat =        start_cmd_attrs.ignore_stat  ;
				reply.job_space               = ::move(start_cmd_attrs.job_space  ) ;
				reply.use_script              =        start_cmd_attrs.use_script   ;
				//
				for( ::pair_ss& kv : start_cmd_attrs.env )
					if (env_keys.insert(kv.first).second) {
						reply.env.push_back(::move(kv)) ;
					} else if (step==5) {
						step = 4 ;
						start_msg_err.first <<set_nl<< "env variable "<<kv.first<<" is defined both in environ_cmd and environ_resources" ;
					}
			[[fallthrough]] ;
			case 1 :
				reply.cmd = ::move(cmd) ;
			[[fallthrough]] ;
			case 0 : {
				VarIdx ti = 0 ;
				for( ::string const& tn : match.static_matches() ) reply.static_matches.emplace_back( tn , rule->matches[ti++].second.flags ) ;
				for( ::string const& p  : match.star_patterns () ) reply.star_matches  .emplace_back( p  , rule->matches[ti++].second.flags ) ;
				//
				if (rule->stdin_idx !=Rule::NoVar) reply.stdin                     = deps_attrs          [rule->stdin_idx ].second.txt ;
				if (rule->stdout_idx!=Rule::NoVar) reply.stdout                    = reply.static_matches[rule->stdout_idx].first      ;
				/**/                               reply.addr                      = fd.peer_addr()                                    ;
				/**/                               reply.autodep_env.lnk_support   = g_config->lnk_support                             ;
				/**/                               reply.autodep_env.reliable_dirs = g_config->reliable_dirs                           ;
				/**/                               reply.autodep_env.src_dirs_s    = *g_src_dirs_s                                     ;
				/**/                               reply.cwd_s                     = rule->cwd_s                                       ;
				/**/                               reply.date_prec                 = g_config->date_prec                               ;
				/**/                               reply.keep_tmp                  = keep_tmp                                          ;
				/**/                               reply.key                       = g_config->key                                     ;
				/**/                               reply.kill_sigs                 = ::move(start_none_attrs.kill_sigs)                ;
				/**/                               reply.live_out                  = submit_attrs.live_out                             ;
				/**/                               reply.network_delay             = g_config->network_delay                           ;
				//
				for( ::pair_ss& kv : start_none_attrs.env ) if (env_keys.insert(kv.first).second) reply.env.push_back(::move(kv)) ; // in case of key conflict, ignore environ_ancillary
				//
				for( auto const& [k,v] : rsrcs ) if (k=="tmp") { reply.tmp_sz_mb = from_string_with_units<'M'>(v) ; break ; }
			} break ;
		DF}
		//
		reply.deps = _mk_digest_deps(::move(deps_attrs)) ;
		if (+deps) {
			::umap_s<VarIdx> dep_idxes ; for( VarIdx i=0 ; i<reply.deps.size() ; i++ ) dep_idxes[reply.deps[i].first] = i ;
			for( auto const& [dn,dd] : deps )
				if ( auto it=dep_idxes.find(dn) ; it!=dep_idxes.end() )                                       reply.deps[it->second].second |= dd ;   // update existing dep
				else                                                    { dep_idxes[dn] = reply.deps.size() ; reply.deps.emplace_back(dn,dd) ;      } // create new dep
		}
		bool deps_done = false ;                             // true if all deps are done for at least a non-zombie req
		for( Req r : reqs ) if (!r.zombie()) {
			for( auto const& [dn,dd] : ::vector_view(deps.data()+n_submit_deps,deps.size()-n_submit_deps) )
				if (!Node(dn)->done(r,NodeGoal::Status)) goto NextReq ;
			deps_done = true ;
			break ;
		NextReq : ;
		}
		//
		{	Lock lock { _s_mutex } ;                         // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job,jrr.seq_id) ; if (it==_s_start_tab.end()) { trace("not_in_tab") ; return false ; }
			StartEntry& entry = it->second                         ;
			trace("entry2",entry) ;
			for( Req r : entry.reqs ) if (!r.zombie()) goto Useful ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send(fd,JobRpcReply(Proc::None)) ;     // silently tell job_exec to give up
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return false/*keep_fd*/ ;
		Useful :
			//                 vvvvvvvvvvvvvvvvvvvvvvv
			jrr.msg <<set_nl<< s_start(entry.tag,+job) ;
			//                 ^^^^^^^^^^^^^^^^^^^^^^^
			if ( step<5 || !deps_done ) {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				OMsgBuf().send(fd,JobRpcReply(Proc::None)) ; // silently tell job_exec to give up
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				Status status = Status::EarlyErr ;
				if (!deps_done) {
					status        = Status::EarlyChkDeps ;
					start_msg_err = {}                   ;
				}
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				s_end( entry.tag , +job , status ) ;         // dont care about backend, job is dead for other reasons
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				trace("early",start_msg_err) ;
				job_exec = { job , reply.addr , New } ;                                                                                                                          // job starts and ends
				JobRpcReq end_jrr { Proc::End , jrr.seq_id , jrr.job , JobDigest{.status=status,.deps=reply.deps,.stderr=start_msg_err.second} , ::move(start_msg_err.first) } ; // before move(reply)
				JobInfoStart jis {
					.eta          =        eta
				,	.submit_attrs = ::move(submit_attrs        )
				,	.rsrcs        =        rsrcs
				,	.host         =        reply.addr
				,	.pre_start    =        jrr
				,	.start        = ::move(reply               )
				,	.stderr       = ::move(start_msg_err.second)
				} ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( Proc::Start , ::copy(job_exec) , ::move(jis    ) , false/*report_now*/ , ::move(pre_action_warnings) , ""s , ::move(jrr.msg ) ) ;
				g_engine_queue.emplace( Proc::End   , ::move(job_exec) , ::move(end_jrr) , ::move(rsrcs)                                                              ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				trace("release_start_tab",entry,step) ;
				_s_start_tab.release(it) ;
				return false/*keep_fd*/ ;
			}
			//
			reply.small_id = _s_small_ids.acquire() ;
			//vvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send(fd,reply) ;                                                                                          // send reply ASAP to minimize overhead
			//^^^^^^^^^^^^^^^^^^^^^^
			job_exec            = { job , reply.addr , New , {} } ; SWEAR(+job_exec.start_date) ;                               // job starts
			entry.start_date    = job_exec.start_date             ;
			entry.conn.host     = job_exec.host                   ;
			entry.conn.port     = jrr.port                        ;
			entry.conn.small_id = reply.small_id                  ;
		}
		in_addr_t reply_addr = reply.addr ;                                                                                     // save before move
		JobInfoStart jis {
			.rule_cmd_crc =        rule->cmd_crc
		,	.stems        = ::move(match.stems         )
		,	.eta          =        eta
		,	.submit_attrs =        submit_attrs
		,	.rsrcs        = ::move(rsrcs               )
		,	.host         =        reply_addr
		,	.pre_start    =        jrr
		,	.start        = ::move(reply               )
		,	.stderr       =        start_msg_err.second
		} ;
		trace("started",job_exec,reply) ;
		bool report_now = +pre_action_warnings || +start_msg_err.second || Delay(job->exec_time)>=start_none_attrs.start_delay ; // dont defer long jobs or if a message is to be delivered to user
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( Proc::Start , ::copy(job_exec) , ::move(jis) , report_now , ::move(pre_action_warnings) , ::move(start_msg_err.second) , jrr.msg+start_msg_err.first ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (!report_now) {
			Pdate start_report = job_exec.start_date+start_none_attrs.start_delay ;                                             // record before moving job_exec
			_s_deferred_report_thread.emplace_at( start_report , jrr.seq_id , ::move(job_exec) ) ;
		}
		return false/*keep_fd*/ ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_mngt( JobMngtRpcReq&& jmrr , SlaveSockFd const& fd ) {
		switch (jmrr.proc) {
			case JobMngtProc::None       : return false/*keep_fd*/ ;      // if connection is lost, ignore it
			case JobMngtProc::ChkDeps    :
			case JobMngtProc::DepVerbose :
			case JobMngtProc::Decode     :
			case JobMngtProc::Encode     : SWEAR(+fd,jmrr.proc) ; break ; // fd is needed to reply
			case JobMngtProc::LiveOut    :                        break ; // no reply
		DF}
		Job job { jmrr.job } ;
		Trace trace(BeChnl,"_s_handle_job_mngt",jmrr) ;
		{	Lock lock { _s_mutex } ;                                      // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//                                                                                                                                            keep_fd
			auto        it    = _s_start_tab.find(+job,jmrr.seq_id) ; if (it==_s_start_tab.end()) { trace("not_in_tab") ; return false ; }
			StartEntry& entry = it->second                          ;
			trace("entry",job,entry) ;
			switch (jmrr.proc) { //!           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				case JobMngtProc::ChkDeps    :
				case JobMngtProc::DepVerbose : g_engine_queue.emplace( jmrr.proc , JobExec(job,entry.conn.host,entry.start_date,New) , jmrr.fd , ::move(jmrr.deps) ) ; break ;
				case JobMngtProc::LiveOut    : g_engine_queue.emplace( jmrr.proc , JobExec(job,entry.conn.host,entry.start_date,New) ,           ::move(jmrr.txt)  ) ; break ;
				//
				case JobMngtProc::Decode : Codec::g_codec_queue->emplace( jmrr.proc , +job , jmrr.fd , ::move(jmrr.txt) , ::move(jmrr.file) , ::move(jmrr.ctx) ,                entry.reqs ) ; break ;
				case JobMngtProc::Encode : Codec::g_codec_queue->emplace( jmrr.proc , +job , jmrr.fd , ::move(jmrr.txt) , ::move(jmrr.file) , ::move(jmrr.ctx) , jmrr.min_len , entry.reqs ) ; break ;
				//                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			DF}
		}
		return false/*keep_fd*/ ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_end( JobRpcReq&& jrr , SlaveSockFd const& ) {
		switch (jrr.proc) {
			case Proc::None : return false ;                                             // if connection is lost, ignore it
			case Proc::End  : break ;                                                    // no reply
		DF}
		Job       job   { jrr.job } ;
		JobExec   je    ;
		::vmap_ss rsrcs ;
		Trace trace(BeChnl,"_s_handle_job_end",jrr) ;
		if (jrr.job==_s_starting_job) Lock lock{_s_starting_job_mutex} ;                 // ensure _s_handled_job_start is done for this job
		{	Lock lock { _s_mutex } ;                                                     // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job,jrr.seq_id) ; if (it==_s_start_tab.end()) { trace("not_in_tab") ; return false ; }
			StartEntry& entry = it->second                         ;
			je    = { job , entry.conn.host , entry.start_date , New } ;
			rsrcs = ::move(entry.rsrcs)                                ;
			_s_small_ids.release(entry.conn.small_id) ;
			trace("release_start_tab",job,entry) ;
			// if we have no fd, job end was invented by heartbeat, no acknowledge
			// acknowledge job end before telling backend as backend may wait the end of the job
			//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			auto [msg,ok] = s_end( entry.tag , +job , jrr.digest.status ) ;
			//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			set_nl(jrr.msg) ; jrr.msg += msg ;
			if ( jrr.digest.status==Status::LateLost && !msg ) jrr.msg           += "vanished after start\n"                   ;
			if ( is_lost(jrr.digest.status) && !ok           ) jrr.digest.status  = Status::LateLostErr                        ;
			/**/                                               jrr.digest.status  = _s_start_tab.release(it,jrr.digest.status) ;
		}
		trace("info") ;
		job->end_exec() ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( Proc::End , ::move(je) , ::move(jrr) , ::move(rsrcs) ) ; // /!\ _s_starting_job ensures Start has been queued before we enqueue End
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return false/*keep_fd*/ ;
	}

	// kill all if ri==0
	void Backend::_s_kill_req(ReqIdx ri) {
		Trace trace(BeChnl,"s_kill_req",ri) ;
		Req                                         req       { ri } ;
		::vmap<JobIdx,pair<StartEntry::Conn,Pdate>> to_wakeup ;
		{	Lock lock { _s_mutex } ;                                                                                       // lock for minimal time
			for( Tag t : All<Tag> ) if (s_ready(t))
				for( JobIdx j : s_tab[+t]->kill_waiting_jobs(ri) ) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( Proc::GiveUp , JobExec(j,New) , req , false/*report*/ ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("queued_in_backend",j) ;
				}
			for( auto jit = _s_start_tab.begin() ; jit!=_s_start_tab.end() ;) {                                            // /!\ we erase entries while iterating
				JobIdx      j = jit->first  ;
				StartEntry& e = jit->second ;
				if (!e) { jit++ ; continue ; }
				if (ri) {
					if ( e.reqs.size()==1 && e.reqs[0]==ri ) goto Kill ;
					for( auto it = e.reqs.begin() ; it!=e.reqs.end() ; it++ ) {                                            // e.reqs is a non-sorted vector, we must search ri by hand
						if (*it!=ri) continue ;
						e.reqs.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( Proc::GiveUp , JobExec(j,e.start_date) , req , +e.start_date/*report*/ ) ; // job is useful for some other Req
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						trace("give_up",j) ;
						break ;
					}
					jit++ ;
					trace("keep",j) ;
					continue ;
				}
			Kill :
				if (+e.start_date) {
					trace("kill",j) ;
					to_wakeup.emplace_back(j,::pair(e.conn,e.start_date)) ;
					jit++ ;
				} else {
					trace("queued",j) ;
					s_tab[+e.tag]->kill_job(j) ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( Proc::GiveUp , JobExec(j,e.start_date) ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					_s_start_tab.erase(jit++) ;
				}
			}
		}
		//                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		for( auto const& [j,c] : to_wakeup ) _s_wakeup_remote( j , c.first , c.second , JobMngtProc::Kill ) ;
		//                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	void Backend::_s_heartbeat_thread_func(::stop_token stop) {
		t_thread_key = 'H' ;
		Trace trace(BeChnl,"_heartbeat_thread_func") ;
		Pdate  last_wrap_around{New} ;
		//
		StartEntry::Conn         conn         ;
		::pair_s<HeartbeatState> lost_report  = {}/*garbage*/ ;
		Status                   status       = {}/*garbage*/ ;
		Pdate                    eta          ;
		::vmap_ss                rsrcs        ;
		SubmitAttrs              submit_attrs ;
		Pdate                    start_date   ;
		for( JobIdx job=0 ;; job++ ) {
			if (stop.stop_requested()) break ;                                                                        // exit even if sleep_until does not wait
			{	Lock lock { _s_mutex }                    ;                                                           // lock _s_start_tab for minimal time
				auto it   = _s_start_tab.lower_bound(job) ;
				if (it==_s_start_tab.end()) goto WrapAround ;
				StartEntry& entry = it->second ;
				job = it->first ;                                                                                     // job is now the next valid entry
				//
				if (!entry    )                      continue ;                                                       // not a real entry                        ==> no check, no wait
				if (!entry.old) { entry.old = true ; continue ; }                                                     // entry is too new, wait until next round ==> no check, no wait
				start_date = entry.start_date ;
				conn       = entry.conn       ;
				if (+start_date) goto Wakeup ;
				lost_report = s_heartbeat(entry.tag,job) ;
				if (lost_report.second==HeartbeatState::Alive) goto Next ;                                            // job is still alive
				if (!lost_report.first                       ) lost_report.first = "vanished before start" ;
				//
				Status hbs = lost_report.second==HeartbeatState::Err ? Status::EarlyLostErr : Status::LateLost ;
				//
				rsrcs        = ::move(entry.rsrcs       )   ;
				submit_attrs = ::move(entry.submit_attrs)   ;
				eta          = entry.req_info().first       ;
				status       = _s_start_tab.release(it,hbs) ;
				trace("handle_job",job,entry,status) ;
			}
			{	JobExec      je  { job , New } ;                                                                      // job starts and ends, no host
				JobInfoStart jis {
					.eta          = eta
				,	.submit_attrs = submit_attrs
				,	.rsrcs        = rsrcs
				,	.host         = conn.host
				,	.pre_start    { Proc::None , conn.seq_id , job }
				,	.start        { Proc::None                     }
				} ;
				JobRpcReq jrr { Proc::End , conn.seq_id , job , JobDigest{.status=status} , ::move(lost_report.first) } ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( Proc::Start , ::copy(je) , ::move(jis) , false/*report_now*/ ) ;
				g_engine_queue.emplace( Proc::End   , ::move(je) , ::move(jrr) , ::move(rsrcs)       ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Next ;
			}
		Wakeup :
			_s_wakeup_remote(job,conn,start_date,JobMngtProc::Heartbeat) ;
		Next :
			if (!g_config->heartbeat_tick.sleep_for(stop)) break ;                                                    // limit job checks
			continue ;
		WrapAround :
			job = 0 ;
			Delay d = g_config->heartbeat + g_config->network_delay ;                                                 // ensure jobs have had a minimal time to start and signal it
			if ((last_wrap_around+d).sleep_until(stop,false/*flush*/)) { last_wrap_around = Pdate(New) ; continue ; } // limit job checks
			else                                                       {                                 break    ; }
		}
		trace("done") ;
	}

	void Backend::s_config( ::array<Config::Backend,N<Tag>> const& config , bool dynamic ) {
		static ::jthread heartbeat_thread { _s_heartbeat_thread_func } ;
		if (!dynamic) {
			_s_job_start_thread      .open( 'S' , _s_handle_job_start       , JobExecBacklog ) ;
			_s_job_mngt_thread       .open( 'M' , _s_handle_job_mngt        , JobExecBacklog ) ;
			_s_job_end_thread        .open( 'E' , _s_handle_job_end         , JobExecBacklog ) ;
			_s_deferred_report_thread.open( 'R' , _s_handle_deferred_report                  ) ;
			_s_deferred_wakeup_thread.open( 'W' , _s_handle_deferred_wakeup                  ) ;
		}
		Trace trace(BeChnl,"s_config",STR(dynamic)) ;
		if (!dynamic) s_executable = *g_lmake_dir_s+"_bin/job_exec" ;
		//
		Lock lock{_s_mutex} ;
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
						throw "bad interface for "s+snake(t)+'\n'+indent(e,1) ;
					}
				} else {
					ifce = host() ;
				}
				be->addr = ServerSockFd::s_addr(ifce) ;
			}
			try                       { be->config(cfg.dct,dynamic) ; be->config_err.clear() ; trace("ready",t  ) ; }
			catch (::string const& e) { SWEAR(+e)                   ; be->config_err = e     ; trace("err"  ,t,e) ; } // empty config_err means ready
		}
		_s_job_start_thread.wait_started() ;
		_s_job_mngt_thread .wait_started() ;
		_s_job_end_thread  .wait_started() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , JobIdx job , ::vector<ReqIdx> const& reqs , ::vmap_ss&& rsrcs , SubmitAttrs const& submit_attrs ) {
		Trace trace(BeChnl,"acquire_cmd_line",tag,job,reqs,rsrcs,submit_attrs) ;
		_s_mutex.swear_locked() ;
		//
		SubmitRsrcsAttrs::s_canon(rsrcs) ;
		//
		auto        [it,fresh] = _s_start_tab.emplace(job,StartEntry()) ;                                                                  // create entry
		StartEntry& entry      = it->second                             ;
		entry.open() ;
		entry.tag   = tag           ;
		entry.reqs  = reqs          ;
		entry.rsrcs = ::move(rsrcs) ;
		if (fresh) {                                             entry.submit_attrs = submit_attrs ;                                     }
		else       { uint8_t nr = entry.submit_attrs.n_retries ; entry.submit_attrs = submit_attrs ; entry.submit_attrs.n_retries = nr ; } // keep retry count if it was counting
		trace("create_start_tab",job,entry) ;
		::vector_s cmd_line {
			s_executable
		,	_s_job_start_thread.fd.service(s_tab[+tag]->addr)
		,	_s_job_mngt_thread .fd.service(s_tab[+tag]->addr)
		,	_s_job_end_thread  .fd.service(s_tab[+tag]->addr)
		,	::to_string(entry.conn.seq_id                       )
		,	::to_string(job                                     )
		,	no_slash(*g_root_dir_s)
		,	::to_string(entry.conn.seq_id%g_config->trace.n_jobs)
		} ;
		trace("cmd_line",cmd_line) ;
		return cmd_line ;
	}

}
