// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

#include "codec.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Py     ;
using namespace Time   ;
using namespace Engine ;

namespace Backends {

	void send_reply( Job job , JobMngtRpcReply&& jmrr ) {
		Trace trace("send_reply",job) ;
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
			,	Backend::DeferredEntry { e.conn.seq_id , JobExec(job,e.conn.host,e.start_date) }
			) ;
		}
		trace("done") ;
	}

	//
	// Backend::*
	//

	::string& operator+=( ::string& os , Backend::Workload const& wl ) {
		os << "Workload("                                                      ;
		os <<      wl._ref_workload       /1000. <<'@'<< wl._ref_date          ;
		os <<','<< wl._reasonable_workload/1000. <<'/'<< wl._reasonable_tokens ;
		os <<','<< wl._running_tokens                                          ;
		//
		os <<",[" ;
		First first ;
		for( Req r : Req::s_reqs_by_start ) os <<first("",",")<< r <<':'<< wl._queued_cost[+r] ;
		os <<']' ;
		//
		return os <<')' ;
	}

	::string& operator+=( ::string& os , Backend::StartEntry const& ste ) {
		return os << "StartEntry(" << ste.conn <<','<< ste.tag <<','<< ste.reqs <<','<< ste.submit_attrs << ')' ;
	}

	::string& operator+=( ::string& os , Backend::StartEntry::Conn const& c ) {
		return os << "Conn(" << SockFd::s_addr_str(c.host) <<':'<< c.port <<','<< c.seq_id <<','<< c.small_id << ')' ;
	}

	::string& operator+=( ::string& os , Backend::DeferredEntry const& de ) {
		return os << "DeferredEntry(" << de.seq_id <<','<< de.job_exec << ')' ;
	}

	void Backend::Workload::_refresh() {
		Pdate now = Pdate(New).round_msec() ;                                                   // avoid rounding errors
		Trace trace(BeChnl,"Workload::_refresh",self,now) ;
		//
		for( auto it=_eta_set.begin() ; it!=_eta_set.end() && it->first<=now ;) {               // eta is passed, job is no more reasonable
			auto   [eta,job]     = *it                           ;
			Tokens tokens        = job->tokens1+1                ;
			Val    left_workload = tokens*(eta-_ref_date).msec() ;
			SWEAR(_reasonable_tokens  >=tokens       ,_reasonable_tokens  ,tokens       ) ;
			SWEAR(_reasonable_workload>=left_workload,_reasonable_workload,left_workload) ;
			_reasonable_tokens   -= tokens        ;
			_reasonable_workload -= left_workload ;
			_eta_tab.erase(it->second) ;                                                        // erase _eta_tab while it is still valid
			_eta_set.erase(it++      ) ;
		}
		//
		Val delta_date     = (now-_ref_date).msec()        ;
		Val delta_workload = delta_date*_reasonable_tokens ;
		_ref_workload += delta_date*_running_tokens ;                                           // this is where there is a rounding error if we do not round now
		_ref_date      = now                        ;                                           // _ref_date is always rounded on ms
		SWEAR( _reasonable_workload>=delta_workload , _reasonable_workload , delta_workload ) ;
		_reasonable_workload -= delta_workload ;
		if (!_reasonable_tokens) SWEAR(!_reasonable_workload,_reasonable_workload) ;            // check no reasonable workload if no reasonable jobs
		trace("done",self) ;
	}

	Backend::Workload::Val Backend::Workload::start( ::vector<ReqIdx> const& reqs , Job j ) {
		Lock        lock { _mutex }             ;
		Delay::Tick dly  = Delay(j->cost).val() ;
		Trace trace(BeChnl,"Workload::start",self,reqs,j,j->tokens1,j->cost,j->exec_time,dly) ;
		for( Req r : reqs ) {
			SWEAR( _queued_cost[+r]>=dly , _queued_cost[+r] , r , dly , j ) ;
			_queued_cost[+r] -= dly ;
		}
		_refresh() ;
		Tokens tokens = j->tokens1+1 ;
		if ( Delay jet=Delay(j->exec_time).round_msec() ; +jet ) { // schedule job based on best estimate
			Pdate jed = _ref_date + jet ;
			_eta_tab.try_emplace(j  ,jed) ;
			_eta_set.emplace    (jed,j  ) ;
			_reasonable_tokens   += tokens            ;
			_reasonable_workload += tokens*jet.msec() ;
		}
		_running_tokens += tokens ;
		trace("done",self) ;
		return _ref_workload ;
	}

	Backend::Workload::Val Backend::Workload::end( ::vector<ReqIdx> const& , Job j ) {
		Lock lock { _mutex } ;
		Trace trace(BeChnl,"Workload::end",self,j,j->tokens1) ;
		_refresh() ;
		Tokens tokens = j->tokens1+1 ;
		if ( auto it=_eta_tab.find(j) ; it!=_eta_tab.end() ) {        // cancel scheduled time left to run
			SWEAR( it->second>=_ref_date , it->second , _ref_date ) ;
			Val left_workload = tokens*((it->second-_ref_date).msec()) ;
			SWEAR(_reasonable_tokens  >=tokens       ,_reasonable_tokens  ,tokens       ) ;
			SWEAR(_reasonable_workload>=left_workload,_reasonable_workload,left_workload) ;
			_reasonable_tokens   -= tokens        ;
			_reasonable_workload -= left_workload ;
			_eta_set.erase({it->second,j}) ;                          // erase _eta_set while it is still valid
			_eta_tab.erase(it            ) ;
		}
		SWEAR( _running_tokens>=tokens , _running_tokens , tokens ) ;
		_running_tokens -= tokens ;
		trace("done",self) ;
		return _ref_workload ;
	}

	// cost is the share of exec_time that can be accumulated, i.e. multiplied by the fraction of what was running in parallel
	Delay Backend::Workload::cost( Job job , Val start_workload , Pdate start_date ) const {
		Lock lock { _mutex } ;
		start_date = start_date.round_msec() ;
		SWEAR( _ref_date>=start_date , _ref_date , start_date ) ;
		uint64_t dly_ms   = (_ref_date-start_date).msec()                  ;
		Val      workload = ::max( _ref_workload-start_workload , Val(1) ) ;
		Tokens  tokens    = job->tokens1+1                                 ;
		return Delay((dly_ms/1000.)*dly_ms*tokens/workload) ;                // divide by 1000. to convert to s
	}

	::pair<Pdate/*eta*/,bool/*keep_tmp*/> Backend::StartEntry::req_info() const {
		Pdate eta      = Pdate::Future       ;
		bool  keep_tmp = false               ;
		Lock  lock     { Req::s_reqs_mutex } ; // taking Req::s_reqs_mutex is compulsery to derefence req
		for( Req r : reqs ) {
			keep_tmp |= r->options.flags[ReqFlag::KeepTmp] ;
			eta       = ::min(eta,r->eta)                  ;
		}
		return {eta,keep_tmp} ;
	}

	//
	// Backend
	//

	Backend*                             Backend::s_tab[N<Tag>]             = {}  ;
	::string                             Backend::_s_job_exec               ;
	Mutex<MutexLvl::Backend >            Backend::_s_mutex                  ;
	Backend::DeferredThread              Backend::_s_deferred_report_thread ;
	Backend::DeferredThread              Backend::_s_deferred_wakeup_thread ;
	Backend::JobStartThread              Backend::_s_job_start_thread       ;
	Backend::JobMngtThread               Backend::_s_job_mngt_thread        ;
	Backend::JobEndThread                Backend::_s_job_end_thread         ;
	SmallIds<SmallId,true/*ThreadSafe*/> Backend::_s_small_ids              ;
	Mutex<MutexLvl::StartJob>            Backend::_s_starting_job_mutex     ;
	::atomic<JobIdx>                     Backend::_s_starting_job           ;
	::map<Job,Backend::StartEntry>       Backend::_s_start_tab              ;
	Backend::Workload                    Backend::_s_workload               ;

	static ::vmap_s<DepDigest> _mk_digest_deps( ::vmap_s<DepSpec>&& deps_attrs ) {
		::vmap_s<DepDigest> res ; res.reserve(deps_attrs.size()) ;
		for( auto& [_,d] : deps_attrs ) res.emplace_back( ::move(d.txt) , DepDigest( {} , d.dflags , true/*parallel*/ ) ) ;
		return res ;
	}

	static bool _localize( Tag t , Req r ) {
		Lock lock{Req::s_reqs_mutex} ;                                    // taking Req::s_reqs_mutex is compulsery to derefence req
		return r->options.flags[ReqFlag::Local] || !Backend::s_ready(t) ; // if asked backend is not usable, force local execution
	}

	void Backend::s_submit( Tag tag , Job j , Req r , SubmitAttrs&& submit_attrs , ::vmap_ss&& rsrcs ) {
		SWEAR(+tag) ;
		Lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_submit",tag,j,r,submit_attrs,rsrcs) ;
		//
		if ( tag!=Tag::Local && _localize(tag,r) ) {
			SWEAR(+tag<N<Tag>) ;                                                              // prevent compiler array bound warning in next statement
			throw_unless( s_tab[+tag] , "open-lmake was compiled without ",tag," support" ) ;
			rsrcs = s_tab[+tag]->mk_lcl( ::move(rsrcs) , s_tab[+Tag::Local]->capacity() , +j ) ;
			tag   = Tag::Local                                                                 ;
			trace("local",rsrcs) ;
		}
		throw_unless( s_ready(tag) , "local backend is not available" ) ;
		submit_attrs.tag = tag ;
		_s_workload.submit(r,j) ;
		s_tab[+tag]->submit(j,r,submit_attrs,::move(rsrcs)) ;
	}

	void Backend::s_add_pressure( Tag tag , Job j , Req r , SubmitAttrs const& sa ) {
		SWEAR(+tag) ;
		if (_localize(tag,r)) tag = Tag::Local ;
		Lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_add_pressure",tag,j,r,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) {
			s_tab[+tag]->add_pressure(j,r,sa) ; // ask sub-backend to raise its priority
		} else {
			it->second.reqs.push_back(+r) ;     // note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;     // and update submit_attrs in case job was not actually started
		}
		_s_workload.add(r,j) ;                  // if not started, we must account for job being queued for this new req
	}

	void Backend::s_set_pressure( Tag tag , Job j , Req r , SubmitAttrs const& sa ) {
		SWEAR(+tag) ;
		if (_localize(tag,r)) tag = Tag::Local ;
		Lock lock{_s_mutex} ;
		Trace trace(BeChnl,"s_set_pressure",tag,j,r,sa) ;
		s_tab[+tag]->set_pressure(j,r,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+tag]->set_pressure(j,r,sa) ; // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;     // and update submit_attrs in case job was not actually started
	}

	void Backend::_s_handle_deferred_wakeup(DeferredEntry&& de) {
		Trace trace(BeChnl,"_s_handle_deferred_wakeup",de) ;
		{	Lock lock { _s_mutex } ;                                                       // lock _s_start_tab for minimal time to avoid dead-locks
			auto it = _s_start_tab.find(+de.job_exec) ;
			if (!( it!=_s_start_tab.end() && it->second.conn.seq_id==de.seq_id )) return ; // too late, job has ended
		}
		JobDigest jd { .status=Status::LateLost } ;                                        // job is still present, must be really lost
		if (+de.job_exec.start_date) jd.stats.total = Pdate(New)-de.job_exec.start_date ;
		trace("lost",jd) ;
		_s_handle_job_end( JobEndRpcReq( {de.seq_id,+de.job_exec} , ::move(jd) ) ) ;
	}

	void Backend::_s_wakeup_remote( Job job , StartEntry::Conn const& conn , Pdate start_date , JobMngtProc proc ) {
		Trace trace(BeChnl,"_s_wakeup_remote",job,conn,proc) ;
		SWEAR(conn.seq_id,job,conn) ;
		try {
			ClientSockFd fd(conn.host,conn.port) ;
			OMsgBuf().send( fd , JobMngtRpcReply(proc,conn.seq_id) ) ;
		} catch (::string const& e) {
			trace("no_job",job,e) ;
			// if job cannot be connected to, assume it is dead and pretend it died if it still exists after network delay
			_s_deferred_wakeup_thread.emplace_after( g_config->network_delay , DeferredEntry{conn.seq_id,JobExec(job,conn.host,start_date)} ) ;
		}
	}

	void Backend::_s_handle_deferred_report(DeferredEntry&& de) {
		Lock lock { _s_mutex }                      ;             // lock _s_start_tab for minimal time to avoid dead-locks
		auto it   = _s_start_tab.find(+de.job_exec) ;
		if (!( it!=_s_start_tab.end() && it->second.conn.seq_id==de.seq_id )) return ;
		Trace trace(BeChnl,"_s_handle_deferred_report",de) ;
		g_engine_queue.emplace( Proc::ReportStart , ::move(de.job_exec) ) ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_start( JobStartRpcReq&& jsrr , SlaveSockFd const& fd ) {
		if (!jsrr) return false ;                                                                   // if connection is lost, ignore it
		SWEAR(+fd,jsrr) ;                                                                           // fd is needed to reply
		Job                      job                 { jsrr.job }          ;
		JobExec                  job_exec            ;
		Rule                     rule                = job->rule()         ;
		Rule::SimpleMatch        match               = job->simple_match() ;
		JobStartRpcReply         reply               ;
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
		::vector<ReqIdx>         reqs                ;
		Trace trace(BeChnl,"_s_handle_job_start",jsrr) ;
		_s_starting_job = jsrr.job ;
		Lock lock { _s_starting_job_mutex } ;
		// to lock for minimal time, we lock twice
		// 1st time, we only gather info, real decisions will be taken when we lock the 2nd time
		// because the only thing that can happend between the 2 locks is that entry disappears, we can move info from entry during 1st lock
		{	Lock lock { _s_mutex } ;                                                                // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()        ) { trace("not_in_tab1",job                              ) ; return false/*keep_fd*/ ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jsrr.seq_id) { trace("bad seq_id1",job,entry.conn.seq_id,jsrr.seq_id) ; return false/*keep_fd*/ ; }
			trace("entry1",entry) ;
			submit_attrs            = ::move(entry.submit_attrs) ;
			rsrcs                   = ::move(entry.rsrcs       ) ;
			reqs                    =        entry.reqs          ;
			tie(eta,reply.keep_tmp) = entry.req_info()           ;
		}
		trace("submit_attrs",submit_attrs) ;
		::vmap_s<DepDigest>& deps          = submit_attrs.deps ;
		size_t               n_submit_deps = deps.size()       ;
		int                  step          = 0                 ;
		deps_attrs = rule->deps_attrs.eval(match) ;                                                 // this cannot fail as it was already run to construct job
		try {
			try {
				cmd               = rule->cmd              .eval(match,rsrcs,&deps) ; step = 1 ;
				start_cmd_attrs   = rule->start_cmd_attrs  .eval(match,rsrcs,&deps) ; step = 2 ;
				start_rsrcs_attrs = rule->start_rsrcs_attrs.eval(match,rsrcs,&deps) ; step = 3 ;
				//
				pre_actions = job->pre_actions( match , true/*mark_target_dirs*/ ) ; step = 4 ;
				for( auto const& [t,a] : pre_actions )
					switch (a.tag) {
						case FileActionTag::UnlinkWarning  :
						case FileActionTag::UnlinkPolluted : pre_action_warnings.emplace_back(t,a.tag) ; ; break ;
					DN}
			} catch (::string const& e) { throw ::pair_ss(e,{}) ; }
		} catch (::pair_ss const& e) {
			start_msg_err.first  <<set_nl<< e.first  ;
			start_msg_err.second <<set_nl<< e.second ;
			switch (step) {
				case 0 : start_msg_err.first <<set_nl<< rule->cmd              .s_exc_msg(false/*using_static*/) ; break ;
				case 1 : start_msg_err.first <<set_nl<< rule->start_cmd_attrs  .s_exc_msg(false/*using_static*/) ; break ;
				case 2 : start_msg_err.first <<set_nl<< rule->start_rsrcs_attrs.s_exc_msg(false/*using_static*/) ; break ;
				case 3 : start_msg_err.first <<set_nl<< "cannot wash targets"                                    ; break ;
			DF}
		}
		trace("deps",step,deps) ;
		// record as much info as possible in reply
		switch (step) {
			case 4 :
				// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
				try {
					try                       { start_none_attrs = rule->start_none_attrs.eval(match,rsrcs,&deps) ; }
					catch (::string const& e) { throw ::pair_ss(e,{}) ;                                             }
				} catch (::pair_ss const& e) {
					start_msg_err    = e                           ;
					start_none_attrs = rule->start_none_attrs.spec ;
					jsrr.msg <<set_nl<< rule->start_none_attrs.s_exc_msg(true/*using_static*/) ;
				}
				reply.keep_tmp                 |= start_none_attrs.keep_tmp       ;
				reply.end_attrs.max_stderr_len  = start_none_attrs.max_stderr_len ;
				#if HAS_ZLIB
					reply.z_lvl = start_none_attrs.z_lvl ;                                                                                                   // if zlib is not available, dont compress
				#endif
				//
				for( auto [t,a] : pre_actions ) reply.pre_actions.emplace_back(t->name(),a) ;
			[[fallthrough]] ;
			case 3 :
				reply.method     = start_rsrcs_attrs.method     ;
				reply.timeout    = start_rsrcs_attrs.timeout    ;
				reply.use_script = start_rsrcs_attrs.use_script ;
				//
				for( ::pair_ss& kv : start_rsrcs_attrs.env ) reply.env.push_back(::move(kv)) ;
			[[fallthrough]] ;
			case 2 :
				reply.interpreter             = ::move(start_cmd_attrs.interpreter ) ;
				reply.allow_stderr            =        start_cmd_attrs.allow_stderr  ;
				reply.autodep_env.auto_mkdir  =        start_cmd_attrs.auto_mkdir    ;
				reply.autodep_env.ignore_stat =        start_cmd_attrs.ignore_stat   ;
				reply.job_space               = ::move(start_cmd_attrs.job_space   ) ;
				//
				for( ::pair_ss& kv : start_cmd_attrs.env ) reply.env.push_back(::move(kv)) ;
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
				/**/                               reply.addr                      = fd.peer_addr()                                    ; SWEAR(reply.addr) ; // 0 is reserved to mean no addr
				/**/                               reply.autodep_env.lnk_support   = g_config->lnk_support                             ;
				/**/                               reply.autodep_env.reliable_dirs = g_config->reliable_dirs                           ;
				/**/                               reply.autodep_env.src_dirs_s    = *g_src_dirs_s                                     ;
				/**/                               reply.end_attrs.cache           = submit_attrs.cache                                ;
				if (+submit_attrs.cache          ) reply.cache                     = Cache::s_tab.at(submit_attrs.cache)               ;
				/**/                               reply.cwd_s                     = rule->cwd_s                                       ;
				/**/                               reply.ddate_prec                = g_config->ddate_prec                              ;
				/**/                               reply.key                       = g_config->key                                     ;
				/**/                               reply.kill_sigs                 = ::move(start_none_attrs.kill_sigs)                ;
				/**/                               reply.live_out                  = submit_attrs.live_out                             ;
				/**/                               reply.network_delay             = g_config->network_delay                           ;
				//
				for( ::pair_ss& kv : start_none_attrs.env ) reply.env.push_back(::move(kv)) ;
			} break ;
		DF}
		//
		reply.deps = _mk_digest_deps(::move(deps_attrs)) ;
		if (+deps) {
			::umap_s<VarIdx> dep_idxes ; for( VarIdx i : iota<VarIdx>(reply.deps.size()) ) dep_idxes[reply.deps[i].first] = i ;
			for( auto const& [dn,dd] : deps )
				if ( auto it=dep_idxes.find(dn) ; it!=dep_idxes.end() )                                       reply.deps[it->second].second |= dd ;          // update existing dep
				else                                                    { dep_idxes[dn] = reply.deps.size() ; reply.deps.emplace_back(dn,dd) ;      }        // create new dep
		}
		bool deps_done = false ;                    // true if all deps are done for at least a non-zombie req
		for( Req r : reqs ) if (!r.zombie()) {
			for( auto const& [dn,dd] : ::span(deps.data()+n_submit_deps,deps.size()-n_submit_deps) )
				if (!Node(dn)->done(r,NodeGoal::Status)) goto NextReq ;
			deps_done = true ;
			break ;
		NextReq : ;
		}
		//
		{	Lock lock { _s_mutex } ;                // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()        ) { trace("not_in_tab2",job                              ) ; return false/*keep_fd*/ ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jsrr.seq_id) { trace("bad seq_id2",job,entry.conn.seq_id,jsrr.seq_id) ; return false/*keep_fd*/ ; }
			trace("entry2",entry) ;
			for( Req r : entry.reqs ) if (!r.zombie()) goto Useful ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send(fd,JobStartRpcReply()) ; // silently tell job_exec to give up
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return false/*keep_fd*/ ;
		Useful :
			//                  vvvvvvvvvvvvvvvvvvvvvvv
			jsrr.msg <<set_nl<< s_start(entry.tag,+job) ;
			//                  ^^^^^^^^^^^^^^^^^^^^^^^
			if ( step<4 || !deps_done ) {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				OMsgBuf().send(fd,JobStartRpcReply()) ;                                                           // silently tell job_exec to give up
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				Status status = Status::EarlyErr ;
				if (!deps_done) {
					status        = Status::EarlyChkDeps ;
					start_msg_err = {}                   ;
				}
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				s_end( entry.tag , +job , status ) ;                                                              // dont care about backend, job is dead for other reasons
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				trace("early",start_msg_err) ;
				job_exec = { job , reply.addr , New/*start*/ } ;                                                  // job starts and ends
				JobEndRpcReq end_jrr {                                                                            // before ::move(reply)
					{jsrr.seq_id,jsrr.job}
				,	{ .deps=reply.deps , .end_attrs=EndAttrs() , .status=status , .stderr=start_msg_err.second }  // XXX! : init of end_attrs seems necessary for g++12 -O3 ?
				,	::move(start_msg_err.first)
				} ;
				::string     msg = jsrr.msg ;                                                                     // before ::move(jsrr)
				JobInfoStart jis {
					.eta          =        eta
				,	.submit_attrs = ::move(submit_attrs        )
				,	.rsrcs        = ::move(rsrcs               )
				,	.host         =        reply.addr
				,	.pre_start    = ::move(jsrr                )
				,	.start        = ::move(reply               )
				,	.stderr       = ::move(start_msg_err.second)
				} ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( Proc::Start , ::copy(job_exec) , ::move(jis    ) , false/*report_now*/ , ::move(pre_action_warnings) , ""s , ::move(msg) ) ;
				g_engine_queue.emplace( Proc::End   , ::move(job_exec) , ::move(end_jrr)                                                                         ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				trace("release_start_tab",entry,step) ;
				_s_start_tab.erase(it) ;
				return false/*keep_fd*/ ;
			}
			//
			reply.small_id = _s_small_ids.acquire() ;
			//vvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send(fd,reply) ;                                                                            // send reply ASAP to minimize overhead
			//^^^^^^^^^^^^^^^^^^^^^^
			job_exec            = { job , reply.addr , New/*start*/ , {}/*end*/ } ; SWEAR(+job_exec.start_date) ; // job starts
			entry.start_date    = job_exec.start_date                             ;
			entry.workload      = _s_workload.start(entry.reqs,job)               ;
			entry.conn.host     = job_exec.host                                   ;
			entry.conn.port     = jsrr.port                                       ;
			entry.conn.small_id = reply.small_id                                  ;
		}
		in_addr_t reply_addr = reply.addr ;                                                                       // save before move
		JobInfoStart jis {
			.rule_cmd_crc =        rule->crc->cmd
		,	.stems        = ::move(match.stems         )
		,	.eta          =        eta
		,	.submit_attrs =        submit_attrs
		,	.rsrcs        = ::move(rsrcs               )
		,	.host         =        reply_addr
		,	.pre_start    =        jsrr
		,	.start        = ::move(reply               )
		,	.stderr       =        start_msg_err.second
		} ;
		trace("started",job_exec,reply) ;
		bool report_now =                                                                                         // dont defer long jobs or if a message is to be delivered to user
				+pre_action_warnings
			||	+start_msg_err.second
			||	submit_attrs.reason.tag==JobReasonTag::Retry                                                      // emit retry start message
			||	Delay(job->exec_time)>=start_none_attrs.start_delay                                               // if job is probably long, emit start message immediately
		;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( Proc::Start , ::copy(job_exec) , ::move(jis) , report_now , ::move(pre_action_warnings) , ::move(start_msg_err.second) , jsrr.msg+start_msg_err.first ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (!report_now) {
			Pdate start_report = job_exec.start_date+start_none_attrs.start_delay ;                               // record before moving job_exec
			_s_deferred_report_thread.emplace_at( start_report , jsrr.seq_id , ::move(job_exec) ) ;
		}
		return false/*keep_fd*/ ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_mngt( JobMngtRpcReq&& jmrr , SlaveSockFd const& fd ) {
		switch (jmrr.proc) {
			case JobMngtProc::None       :                                // if connection is lost, ignore it
			case JobMngtProc::Heartbeat  : return false/*keep_fd*/ ;      // received heartbeat probe from job, just receive and ignore
			case JobMngtProc::ChkDeps    :
			case JobMngtProc::DepVerbose :
			case JobMngtProc::Decode     :
			case JobMngtProc::Encode     : SWEAR(+fd,jmrr.proc) ; break ; // fd is needed to reply
			case JobMngtProc::LiveOut    :                        break ; // no reply
		DF}
		Job job { jmrr.job } ;
		Trace trace(BeChnl,"_s_handle_job_mngt",jmrr) ;
		{	Lock lock { _s_mutex } ;                                      // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()        ) { trace("not_in_tab",job                              ) ; return false/*keep_fd*/ ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jmrr.seq_id) { trace("bad seq_id",job,entry.conn.seq_id,jmrr.seq_id) ; return false/*keep_fd*/ ; }
			trace("entry",job,entry) ;
			switch (jmrr.proc) {
				case JobMngtProc::ChkDeps    : //!vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				case JobMngtProc::DepVerbose : g_engine_queue.emplace( jmrr.proc , JobExec(job,entry.conn.host,entry.start_date,New/*end*/) , jmrr.fd , ::move(jmrr.deps) ) ; break ;
				case JobMngtProc::LiveOut    : g_engine_queue.emplace( jmrr.proc , JobExec(job,entry.conn.host,entry.start_date,New/*end*/) ,           ::move(jmrr.txt)  ) ; break ;
				//
				case JobMngtProc::Decode : Codec::g_codec_queue->emplace( jmrr.proc , +job , jmrr.fd , ::move(jmrr.txt) , ::move(jmrr.file) , ::move(jmrr.ctx) ,                entry.reqs ) ; break ;
				case JobMngtProc::Encode : Codec::g_codec_queue->emplace( jmrr.proc , +job , jmrr.fd , ::move(jmrr.txt) , ::move(jmrr.file) , ::move(jmrr.ctx) , jmrr.min_len , entry.reqs ) ; break ;
			DF} //!                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		trace("done") ;
		return false/*keep_fd*/ ;
	}

	bool/*keep_fd*/ Backend::_s_handle_job_end( JobEndRpcReq&& jerr , SlaveSockFd const& ) {
		if (!jerr) return false ;                                                                     // if connection is lost, ignore it
		Job     job { jerr.job } ;
		JobExec je  ;
		Trace trace(BeChnl,"_s_handle_job_end",jerr) ;
		if (jerr.job==_s_starting_job) Lock lock{_s_starting_job_mutex} ;                             // ensure _s_handled_job_start is done for this job
		{	Lock lock { _s_mutex } ;                                                                  // prevent sub-backend from manipulating _s_start_tab from main thread, lock for minimal time
			//
			auto        it    = _s_start_tab.find(+job) ; if (it==_s_start_tab.end()        ) { trace("not_in_tab",job                              ) ; goto Bad ; }
			StartEntry& entry = it->second              ; if (entry.conn.seq_id!=jerr.seq_id) { trace("bad seq_id",job,entry.conn.seq_id,jerr.seq_id) ; goto Bad ; }
			_s_small_ids.release(entry.conn.small_id) ;
			//
			je         = { job , entry.conn.host , entry.start_date , New }                       ;
			/**/         _s_workload.end ( entry.reqs , job                                     ) ;
			je.cost    = _s_workload.cost(              job , entry.workload , entry.start_date ) ;
			je.tokens1 = entry.submit_attrs.tokens1                                               ;
			//
			trace("release_start_tab",job,entry) ;
			// if we have no fd, job end was invented by heartbeat, no acknowledge
			// acknowledge job end before telling backend as backend may wait the end of the job
			//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			auto [msg,ok] = s_end( entry.tag , +job , jerr.digest.status ) ;
			//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			set_nl(jerr.msg) ; jerr.msg += msg ;
			if ( jerr.digest.status==Status::LateLost && !msg ) jerr.msg += "vanished after start\n" ;
			_s_start_tab.erase(it) ;
		}
		trace("info") ;
		job->end_exec() ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace( Proc::End , ::move(je) , ::move(jerr) ) ;                             // /!\ _s_starting_job ensures Start has been queued before we enqueue End
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return false/*keep_fd*/ ;
	Bad :
		JobDigest& digest = jerr.digest ;
		if (+digest.upload_key) Cache::s_tab.at(digest.end_attrs.cache)->dismiss(digest.upload_key) ; // this job corresponds to nothing in server, free up temporary storage copied in job_exec
		return false/*keep_fd*/ ;
	}

	// kill all if ri==0
	void Backend::_s_kill_req(Req r) {
		Trace trace(BeChnl,"s_kill_req",r) ;
		::vmap<Job,pair<StartEntry::Conn,Pdate>> to_wakeup ;
		{	Lock lock { _s_mutex } ;                                                                                     // lock for minimal time
			for( Tag t : iota(All<Tag>) ) if (s_ready(t))
				for( Job j : s_tab[+t]->kill_waiting_jobs(r) ) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					g_engine_queue.emplace( Proc::GiveUp , JobExec(j,New) , r , false/*report*/ ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("queued_in_backend",j) ;
				}
			for( auto jit=_s_start_tab.begin() ; jit!=_s_start_tab.end() ;) {                                            // /!\ we erase entries while iterating
				Job         j = jit->first  ;
				StartEntry& e = jit->second ;
				if (!e) { jit++ ; continue ; }
				if (+r) {
					if ( e.reqs.size()==1 && e.reqs[0]==+r ) goto Kill ;
					for( auto it=e.reqs.begin() ; it!=e.reqs.end() ; it++ ) {                                            // e.reqs is a non-sorted vector, we must search ri by hand
						if (*it!=+r) continue ;
						e.reqs.erase(it) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						g_engine_queue.emplace( Proc::GiveUp , JobExec(j,e.start_date) , r , +e.start_date/*report*/ ) ; // job is useful for some other Req
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
		Pdate last_wrap_around { New } ;
		//
		StartEntry::Conn         conn         ;
		::pair_s<HeartbeatState> lost_report  = {}/*garbage*/ ;
		Status                   status       = {}/*garbage*/ ;
		Pdate                    eta          ;
		::vmap_ss                rsrcs        ;
		SubmitAttrs              submit_attrs ;
		Pdate                    start_date   ;
		Delay                    round_trip          = Delay(New,g_config->network_delay.val()*2) ;
		Delay                    started_min_job_age = Delay(New,g_config->heartbeat    .val()/2) ;                   // first check is after g_config->heartbeat on average (for pef)
		Delay                    spawned_min_job_age = ::max(started_min_job_age,round_trip)      ;                   // ensure jobs have had a minimal time to start and signal it
		for( Job job ;; job=Job(+job+1) ) {
			if (stop.stop_requested()) break ;                                                                        // exit even if sleep_until does not wait
			Pdate now { New } ;
			{	Lock lock { _s_mutex }                    ;                                                           // lock _s_start_tab for minimal time
				auto it   = _s_start_tab.lower_bound(job) ;
				if (it==_s_start_tab.end()) goto WrapAround ;
				StartEntry& entry = it->second ;
				job = it->first ;                                                                                     // job is now the next valid entry
				//
				if ( !entry                                     ) continue ;                                          // not a real entry ==> no check, no wait
				if ( now-entry.spawn_date < spawned_min_job_age ) continue ;                                          // job is too young ==> no check, no wait
				start_date = entry.start_date ;
				conn       = entry.conn       ;
				if (+start_date) goto Wakeup ;
				lost_report = s_heartbeat(entry.tag,job) ;
				if (lost_report.second==HeartbeatState::Alive) goto Next ;                                            // job is still alive
				if (!lost_report.first                       ) lost_report.first = "vanished before start" ;
				//
				status = lost_report.second==HeartbeatState::Err ? Status::EarlyLostErr : Status::EarlyLost ;
				//
				rsrcs        = ::move(entry.rsrcs       ) ;
				submit_attrs = ::move(entry.submit_attrs) ;
				eta          = entry.req_info().first     ;
				_s_start_tab.erase(it) ;
				trace("handle_job",job,entry,status) ;
			}
			{	JobExec      je  { job , New } ;                                                                      // job starts and ends, no host
				JobInfoStart jis {
					.eta          = eta
				,	.submit_attrs = submit_attrs
				,	.rsrcs        = ::move(rsrcs)
				,	.host         = conn.host
				,	.pre_start    { {conn.seq_id,+job} }
				} ;
				JobEndRpcReq jerr { {conn.seq_id,+job} , JobDigest{.status=status} , ::move(lost_report.first) } ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				g_engine_queue.emplace( Proc::Start , ::copy(je) , ::move(jis) , false/*report_now*/ ) ;
				g_engine_queue.emplace( Proc::End   , ::move(je) , ::move(jerr)                      ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Next ;
			}
		Wakeup :
			if ( now-start_date < started_min_job_age ) continue ;                                                    // job is too young ==> no check, no wait
			_s_wakeup_remote(job,conn,start_date,JobMngtProc::Heartbeat) ;
		Next :
			if (!g_config->heartbeat_tick.sleep_for(stop)) break ;                                                    // limit job checks
			continue ;
		WrapAround :
			for( Tag t : iota(All<Tag>) ) if (s_ready(t)) {
				Lock lock { _s_mutex } ;
				s_heartbeat(t) ;
			}
			job = {} ;
			Delay d = g_config->heartbeat ;
			if ((last_wrap_around+d).sleep_until(stop,false/*flush*/)) { last_wrap_around = Pdate(New) ; continue ; } // limit job checks
			else                                                       {                                 break    ; }
		}
		trace("done") ;
	}

	void Backend::s_config( ::array<Config::Backend,N<Tag>> const& config , bool dynamic ) {
		static ::jthread heartbeat_thread { _s_heartbeat_thread_func } ;
		if (!dynamic) {                                                                                                                                      // if dynamic, threads are already running
			_s_job_start_thread      .open( 'S' , _s_handle_job_start       , JobExecBacklog ) ;
			_s_job_mngt_thread       .open( 'M' , _s_handle_job_mngt        , JobExecBacklog ) ;
			_s_job_end_thread        .open( 'E' , _s_handle_job_end         , JobExecBacklog ) ;
			_s_deferred_report_thread.open( 'R' , _s_handle_deferred_report                  ) ;
			_s_deferred_wakeup_thread.open( 'W' , _s_handle_deferred_wakeup                  ) ;
		}
		Trace trace(BeChnl,"s_config",STR(dynamic)) ;
		if (!dynamic) _s_job_exec = *g_lmake_root_s+"_bin/job_exec" ;
		//
		Lock lock{_s_mutex} ;
		for( Tag t : iota(1,All<Tag>) ) {                                                                                                                    // local backend is always available
			Backend*               be  = s_tab [+t] ; if (!be            ) {                                     trace("not_implemented",t  ) ; continue ; }
			Config::Backend const& cfg = config[+t] ; if (!cfg.configured) { be->config_err = "not configured" ; trace("not_configured" ,t  ) ; continue ; } // empty config_err means ready
			try {
				be->config(cfg.dct,cfg.env,dynamic) ;
				be->config_err.clear() ; trace("ready",t  ) ;                                                                                                // .
			} catch (::string const& e) {
				SWEAR(+e) ;                                                                                                                                  // .
				be->config_err = e ;
				Fd::Stderr.write("Warning : backend "+t+" could not be configured : "+e+'\n') ;
				trace("err"  ,t,e) ;
				continue ;
			}
			//
			if (be->is_local()) {
				be->addr = SockFd::LoopBackAddr ;
			} else {
				::string ifce ;
				if (+cfg.ifce) {
					Gil gil ;
					try {
						Ptr<Dict> glbs = py_run(cfg.ifce) ;
						ifce = (*glbs)["interface"].as_a<Str>() ;
					} catch (::string const& e) {
						throw "bad interface for "s+t+'\n'+indent(e,1) ;
					}
				}
				::vmap_s<in_addr_t> addrs = ServerSockFd::s_addrs_self(ifce) ;
				if (addrs.size()>1) {
					::string msg   = "multiple possible interfaces : " ;
					::string host_ = host()                            ;
					::string fqdn_ = fqdn()                            ;
					First    first ;
					for( auto const& [ifce,addr] : addrs ) msg << first("",", ") << ifce <<'('<< ServerSockFd::s_addr_str(addr) <<')' ;
					msg += '\n' ;
					msg << "consider one of :\n" ;
					/**/              msg << "\tlmake.config.backends."<<snake(t)<<".interface = "<<mk_py_str(host_)<<'\n' ;
					if (fqdn_!=host_) msg << "\tlmake.config.backends."<<snake(t)<<".interface = "<<mk_py_str(fqdn_)<<'\n' ;
					for( auto const& [ifce,addr] : addrs ) {
						msg << "\tlmake.config.backends."<<snake(t)<<".interface = "<<mk_py_str(ifce                          )<<'\n' ;
						msg << "\tlmake.config.backends."<<snake(t)<<".interface = "<<mk_py_str(ServerSockFd::s_addr_str(addr))<<'\n' ;
					}
					throw msg ;
				}
				be->addr = addrs[0].second ;
			}
		}
		_s_job_start_thread.wait_started() ;
		_s_job_mngt_thread .wait_started() ;
		_s_job_end_thread  .wait_started() ;
	}

	::vector_s Backend::acquire_cmd_line( Tag tag , Job job , ::vector<ReqIdx> const& reqs , ::vmap_ss&& rsrcs , SubmitAttrs const& submit_attrs ) {
		Trace trace(BeChnl,"acquire_cmd_line",tag,job,reqs,rsrcs,submit_attrs) ;
		_s_mutex.swear_locked() ;
		//
		SWEAR(!_s_start_tab.contains(job),job) ;
		StartEntry& entry = _s_start_tab[job] ;      // create entry
		entry.submit_attrs = submit_attrs          ;
		entry.conn.seq_id  = (*Engine::g_seq_id)++ ;
		entry.spawn_date   = New                   ;
		entry.tag          = tag                   ;
		entry.reqs         = reqs                  ;
		entry.rsrcs        = ::move(rsrcs)         ;
		trace("create_start_tab",job,entry) ;
		::vector_s cmd_line {
			_s_job_exec
		,	_s_job_start_thread.fd.service(s_tab[+tag]->addr)
		,	_s_job_mngt_thread .fd.service(s_tab[+tag]->addr)
		,	_s_job_end_thread  .fd.service(s_tab[+tag]->addr)
		,	::to_string(entry.conn.seq_id                       )
		,	::to_string(+job                                    )
		,	no_slash(*g_repo_root_s)
		,	::to_string(entry.conn.seq_id%g_config->trace.n_jobs)
		} ;
		trace("cmd_line",cmd_line) ;
		return cmd_line ;
	}

}
