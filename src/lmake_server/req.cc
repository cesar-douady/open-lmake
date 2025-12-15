// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include <stdexcept>

using namespace Disk ;
using namespace Time ;

namespace Engine {

	//
	// Req
	//

	SmallIds<ReqIdx,true/*ThreadSafe*/>         Req::s_small_ids      ;
	Mutex<MutexLvl::Req>                        Req::s_reqs_mutex     ;
	Mutex<             >                        Req::s_req_idxs_mutex ;
	::vector<ReqData>                           Req::s_store(1)       ;
	::vector<Req>                               Req::_s_reqs_by_start ;
	::vector<Req>                               Req::_s_reqs_by_eta   ;
	::array<atomic<bool>,1<<(sizeof(ReqIdx)*8)> Req::_s_zombie_tab    = { true } ; // Req 0 is zombie, all other ones are not

	::string& operator+=( ::string& os , Req const r ) { // START_OF_NO_COV
		return os << "Rq(" << int(+r) << ')' ;
	}                                                    // END_OF_NO_COV

	void Req::make(EngineClosureReq const& ecr) {
		SWEAR(s_store.size()>+self) ;             // ensure data exist
		ReqData& data = *self ;
		//
		data._open_log() ;
		//
		if (ecr.options.flags[ReqFlag::Ete]) data.et1 = data.start_pdate ;
		else                                 data.et1 = data.start_pdate ;
		//
		data.idx_by_start = s_n_reqs()  ;
		data.idx_by_eta   = s_n_reqs()  ;         // initially, eta is far future
		data.options      = ecr.options ;
		data.audit_fd     = ecr.fd      ;
		data.jobs .set_dflt(self) ;
		data.nodes.set_dflt(self) ;
		//
		{	Lock lock { s_req_idxs_mutex } ;
			_s_reqs_by_start.push_back(self) ;
		} //!                                             eta                                                    push_self
		if (ecr.options.flags[ReqFlag::Ete]) _adjust_eta( Pdate(New)+Delay(ecr.options.flag_args[+ReqFlag::Ete]) , true  ) ;
		else                                 _adjust_eta( {}                                                     , true  ) ;
		//
		Trace trace("Rmake",self,s_n_reqs(),data.start_ddate,data.start_pdate) ;
		try {
			if (ecr.options.flags[ReqFlag::RetryOnError]) data.n_retries    = from_string<uint8_t>(ecr.options.flag_args[+ReqFlag::RetryOnError]                 ) ;
			if (ecr.options.flags[ReqFlag::MaxRuns     ]) data.n_runs       = from_string<uint8_t>(ecr.options.flag_args[+ReqFlag::MaxRuns     ]                 ) ;
			if (ecr.options.flags[ReqFlag::MaxSubmits  ]) data.n_submits    = from_string<uint8_t>(ecr.options.flag_args[+ReqFlag::MaxSubmits  ]                 ) ;
			if (ecr.options.flags[ReqFlag::Nice        ]) data.nice         = from_string<uint8_t>(ecr.options.flag_args[+ReqFlag::Nice        ]                 ) ;
			if (ecr.options.flags[ReqFlag::CacheMethod ]) data.cache_method = mk_enum<CacheMethod>(ecr.options.flag_args[+ReqFlag::CacheMethod ]                 ) ;
			JobIdx                                        n_jobs            = from_string<JobIdx >(ecr.options.flag_args[+ReqFlag::Jobs        ],true/*empty_ok*/) ;
			if (ecr.is_job()) data.job = ecr.job()                                                                                ;
			else              data.job = Job( Special::Req , Deps(ecr.targets(),FullAccesses,DflagsDfltStatic,true/*parallel*/) ) ;
			Backend::s_open_req( +self , n_jobs ) ;
			data.has_backend = true ;
			trace("job",data.job) ;
			//
			Job::ReqInfo& jri = data.job->req_info(self) ;
			jri.live_out = self->options.flags[ReqFlag::LiveOut] ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			data.job->make( jri , JobMakeAction::Status , {}/*JobReason*/ , No/*speculate*/ ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			for( Node d : data.job->deps ) {
				/**/                           if (!d->done(self)              ) continue ;
				Job j = d->conform_job_tgt() ; if (!j                          ) continue ;
				/**/                           if (j->run_status!=RunStatus::Ok) continue ;
				//
				self->up_to_dates.push_back(d) ;
			}
			chk_end() ;
		} catch (::string const& e) {
			close() ;
			throw ;
		}
	}

	void Req::kill(bool ctrl_c) {
		Trace trace("Rkill",self) ;
		SWEAR(zombie()) ;                                                           // zombie has already been set
		if (ctrl_c) audit_ctrl_c( self->audit_fd , self->log_fd , self->options ) ;
		Backend::s_kill_req(+self) ;
	}

	void Req::close() {
		Trace trace("Rclose",self) ;
		SWEAR(  self->is_open  ()                     ) ;
		SWEAR( !self->n_running() , self->n_running() ) ;
		g_kpi.reqs.push_back({
			.n_job_req_info  = self->jobs .size()
		,	.n_node_req_info = self->nodes.size()
		}) ;
		if (self->has_backend) Backend::s_close_req(+self) ;
		// erase req from sorted vectors by physically shifting reqs that are after
		{	Lock lock{s_req_idxs_mutex} ;
			Idx n_reqs = s_n_reqs() ;
			for( Idx i : iota( self->idx_by_start , n_reqs-1 ) ) {
				_s_reqs_by_start[i]               = _s_reqs_by_start[i+1] ;
				_s_reqs_by_start[i]->idx_by_start = i                     ; // adjust
			}
			_s_reqs_by_start.pop_back() ;
			for( Idx i : iota( self->idx_by_eta , n_reqs-1 ) ) {
				_s_reqs_by_eta[i]             = _s_reqs_by_eta[i+1] ;
				_s_reqs_by_eta[i]->idx_by_eta = i                   ;       // adjust
			}
			_s_reqs_by_eta.pop_back() ;
		}
	}

	void Req::new_eta() {
		if (self->options.flags[ReqFlag::Ete]) {
			self->et2 = ::min( Delay() , self->et1-Pdate(New) ) ;
			return ;
		}
		Pdate now       = New                                                       ;
		Pdate new_eta   = Backend::s_submitted_eta(self) + self->stats.waiting_cost ;
		Pdate old_eta   = self->et1                                                 ;
		Delay old_ete   = old_eta-now                                               ;
		Delay delta_ete = new_eta>old_eta ? new_eta-old_eta : old_eta-new_eta       ; // cant use ::abs(new_eta-old_eta) because of signedness
		//
		if ( delta_ete.val() > (old_ete.val()>>4) ) {                                 // else eta did not change significatively
			_adjust_eta(new_eta) ;
			Backend::s_new_req_etas() ;                                               // tell backends that etas changed significatively
		}
		self->et2 = new_eta-now ;
	}

	void Req::_adjust_eta( Pdate eta , bool push_self ) {
			Trace trace("R_adjust_eta",self->et1,eta) ;
			// reorder _s_reqs_by_eta and adjust idx_by_eta to reflect new order
			bool changed    = false              ;
			Lock lock       { s_req_idxs_mutex } ;
			Idx  idx_by_eta = self->idx_by_eta   ;
			//
			if (+eta     ) self->et1 = eta ;                                                                 // eta must be upated while lock is held as it is read in other threads
			if (push_self) _s_reqs_by_eta.push_back(self) ;
			//
			while ( idx_by_eta>0 && _s_reqs_by_eta[idx_by_eta-1]->et1>self->et1 ) {                          // if eta is decreased
				( _s_reqs_by_eta[idx_by_eta  ] = _s_reqs_by_eta[idx_by_eta-1] )->idx_by_eta = idx_by_eta   ; // swap w/ prev entry
				( _s_reqs_by_eta[idx_by_eta-1] = self                         )->idx_by_eta = idx_by_eta-1 ; // .
				changed = true ;
			}
			if (changed) return ;
			while ( idx_by_eta+1<s_n_reqs() && _s_reqs_by_eta[idx_by_eta+1]->et1<self->et1 ) {               // if eta is increased
				( _s_reqs_by_eta[idx_by_eta  ] = _s_reqs_by_eta[idx_by_eta+1] )->idx_by_eta = idx_by_eta   ; // swap w/ next entry
				( _s_reqs_by_eta[idx_by_eta+1] = self                         )->idx_by_eta = idx_by_eta+1 ; // .
			}
	}

	void Req::_report_cycle(Node node) {
		::uset<Node>   seen      ;
		::vmap_s<Node> cycle     ;
		::uset<Rule>   to_raise  ;
		::vector<Node> to_forget ;
		First          first     ;
		::string       cycle_str ;
		for( Node d=node ; seen.insert(d).second ;) {
			NodeStatus dns = d->status() ;
			::string   dr  ;
			if ( dns!=NodeStatus::Unknown && dns>=NodeStatus::Uphill ) {
				d  = d->dir     ;
				dr = "<uphill>" ;
				goto Next ;                                                     // there is no rule for uphill
			}
			for( Job j : d->conform_job_tgts(d->c_req_info(self)) )             // 1st pass to find done rules which we suggest to raise the prio of to avoid the loop
				if (j->c_req_info(self).done()) to_raise.insert(j->rule()) ;
			for( Job j : d->conform_job_tgts(d->c_req_info(self)) ) {           // 2nd pass to find the loop
				JobReqInfo const& cjri = j->c_req_info(self) ;
				if (cjri.done()          ) continue ;
				if (cjri.speculative_wait) to_forget.push_back(d) ;
				for( Node dd : j->deps ) {
					if (dd->done(self)) continue ;
					d  = dd                     ;
					dr = j->rule()->user_name() ;
					if (!seen.contains(d)) cycle_str << first("(",",")<<mk_py_str(d->name()) ;
					goto Next ;
				}
				fail_prod("not done but all deps are done :",j->name()) ;       // NO_COV
			}
			fail_prod("not done but all pertinent jobs are done :",d->name()) ; // NO_COV
		Next :
			cycle.emplace_back(dr,d) ;
		}
		cycle_str += first("",",)",")") ;
		self->audit_node( Color::Err , "cycle detected for",node ) ;
		Node   deepest   = cycle.back().second                                                        ;
		bool   seen_loop = deepest==node                                                              ;
		size_t w         = ::max<size_t>( cycle , [](auto const& r_n) { return r_n.first.size() ; } ) ;
		for( size_t i : iota(cycle.size()) ) {
			const char* prefix ;
			/**/ if ( seen_loop && i==0 && i==cycle.size()-1 )   prefix = "^-- " ;
			else if ( seen_loop && i==0                      )   prefix = "^   " ;
			else if (                      i==cycle.size()-1 )   prefix = "+-- " ;
			else if ( seen_loop && i!=0                      )   prefix = "|   " ;
			else if ( cycle[i].second==deepest               ) { prefix = "+-> " ; seen_loop = true ; }
			else                                                 prefix = "    " ;
			self->audit_node( Color::Note , prefix+widen(cycle[i].first,w),cycle[i].second , 1 ) ;
		}
		if ( +to_forget || +cycle_str ) {
			/**/                      self->audit_info( Color::Note , "consider some of :\n"     ) ;
			for( Node n : to_forget ) self->audit_node( Color::Note , "lforget -d" , n       , 1 ) ;
			::set_s sub_repos_s ; for( Rule r : to_raise ) sub_repos_s.insert(r->sub_repo_s) ;
			for( ::string const& sub_repo_s : sub_repos_s ) {
				/**/                                                     self->audit_info( Color::Note , "add to "+sub_repo_s+"Lmakefile.py :"             , 1 ) ;
				for( Rule r : to_raise  ) if (r->sub_repo_s==sub_repo_s) self->audit_info( Color::Note , r->name+".prio = "+::to_string(r->user_prio)+"+1" , 2 ) ;
			}
			if (+cycle_str) {
				self->audit_info( Color::Note , "add to Lmakefile.py :"        , 1 ) ;
				self->audit_info( Color::Note , "for t in "+cycle_str+" :"     , 2 ) ;
				self->audit_info( Color::Note , "class MyAntiRule(AntiRule) :" , 3 ) ;
				self->audit_info( Color::Note , "target = t"                   , 4 ) ;
			}
		}
	}

	bool/*overflow*/ Req::_report_err( Dep const& dep , size_t& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl ) {
		if (dep.dflags[Dflag::IgnoreError]) return false/*overflow*/ ;
		if (seen_nodes.contains(dep)      ) return false/*overflow*/ ;
		seen_nodes.insert(dep) ;
		NodeReqInfo const& cri = dep->c_req_info(self) ;
		const char*        err = nullptr               ;
		switch (dep->status()) {
			case NodeStatus::Multi     :                                  err = "multi"                                        ; break ;
			case NodeStatus::Transient :                                  err = "missing transient sub-file"                   ; break ;
			case NodeStatus::Uphill    : if (dep.dflags[Dflag::Required]) err = "missing required sub-file"                    ; break ;
			case NodeStatus::Src       : if (dep->crc==Crc::None        ) err = dep.frozen()?"missing frozen":"missing source" ; break ;
			case NodeStatus::SrcDir    : if (dep.dflags[Dflag::Required]) err = "missing required"                             ; break ;
			case NodeStatus::Plain :
				if (cri.overwritten)
					err = "overwritten" ;
				else if (+dep->conform_job_tgts(cri))
					for( Job job : dep->conform_job_tgts(cri) ) {
						if (_report_err( job , dep , n_err , seen_stderr , seen_jobs , seen_nodes , lvl )) return true/*overflow*/ ;
					}
				else
					err = "not built" ;                                                              // if no better explanation found
			break ;
			case NodeStatus::None :
				if      (dep->manual({dep->name()})>=Manual::Changed) err = "dangling" ;
				else if (dep.dflags[Dflag::Required]                    ) err = "missing"  ;
			break ;
		DF}                                                                                          // NO_COV
		if (err) return self->_send_err( false/*intermediate*/ , err , dep->name() , n_err , lvl ) ;
		else     return false/*overflow*/                                                          ;
	}

	bool/*overflow*/ Req::_report_err( Job job , Node target , size_t& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl ) {
		if (seen_jobs.contains(job)) return false/*overflow*/ ;
		seen_jobs.insert(job) ;
		JobReqInfo const& jri = job->c_req_info(self) ;
		if (!jri.done()) return false/*overflow*/ ;
		if (!job->err()) return false/*overflow*/ ;
		//
		bool intermediate = job->run_status==RunStatus::DepError ;
		Rule r            = job->rule()                          ;
		if ( self->_send_err( intermediate , r->name , +target?target->name():job->name() , n_err , lvl ) ) return true/*overflow*/ ;
		//
		if ( !seen_stderr && job->run_status==RunStatus::Ok ) { // show first stderr
			if (is_infinite(r->special)) {
				MsgStderr msg_stderr = job->special_msg_stderr(true/*short_msg*/) ;
				self->audit_info( Color::Note , msg_stderr.msg    , lvl+1 ) ;
				self->audit_info( Color::None , msg_stderr.stderr , lvl+1 ) ;
				seen_stderr = true ;
			} else if (job.is_plain()) {
				JobEndRpcReq jerr = job.job_info(JobInfoKind::End).end ;
				if (!jerr) self->audit_info( Color::Note , "no stderr available" , lvl+1 ) ;
				else       seen_stderr = self->audit_stderr( job , jerr.msg_stderr , jerr.digest.max_stderr_len , lvl ) ;
			}
		}
		if (intermediate)
			for( Dep const& d : job->deps )
				if ( _report_err( d , n_err , seen_stderr , seen_jobs , seen_nodes , lvl+1 ) ) return true/*overflow*/ ;
		return false/*overflow*/ ;
	}

	void Req::_do_chk_end() {
		Job               job     = self->job               ;
		JobReqInfo const& cri     = job->c_req_info(self)   ;
		bool              job_err = job->status!=Status::Ok ;
		Trace trace("chk_end",self,cri,job,job->status) ;
		//
		// refresh codec files
		for( ::string const& f : self->refresh_codecs ) {
			trace("refresh_codecs",self->refresh_codecs) ;
			Job job { Rule(Special::Codec) , Codec::CodecFile::s_file(f) } ;
			if (!job) continue ;                                                        // ignore errors as there is nothing much we can do
			job->refresh_codec(self) ;
		}
		self->refresh_codecs = {} ;
		//
		self->audit_stats  (       ) ;
		self->audit_summary(job_err) ;
		//
		if (zombie()                     ) { trace("zombie") ; goto Done ; }
		if (!job_err                     ) { trace("ok"    ) ; goto Done ; }
		//
		if (!job->c_req_info(self).done()) {
			for( Dep const& d : job->deps )
				if (!d->done(self)) { _report_cycle(d) ; trace("cycle") ; goto Done ; }
			fail_prod("job not done but all deps are done :",job->name()) ;             // NO_COV
		} else {
			trace("err",job->rule()->special) ;
			size_t       n_err       = g_config->max_err_lines ? g_config->max_err_lines : size_t(-1) ;
			bool         seen_stderr = false                                                          ;
			::uset<Job > seen_jobs   ;
			::uset<Node> seen_nodes  ;
			NfsGuard     nfs_guard   { g_config->file_sync }                                          ;
			if (job->rule()->special==Special::Req) {
				for( Dep const& d : job->deps ) if (d->status()<=NodeStatus::Makable)       _report_err    (d     ,n_err,seen_stderr,seen_jobs,seen_nodes) ;
				for( Dep const& d : job->deps ) if (d->status()> NodeStatus::Makable) self->_report_no_rule(d,&nfs_guard                                 ) ;
			} else {
				/**/                                                                        _report_err    (job,{},n_err,seen_stderr,seen_jobs,seen_nodes) ;
			}
		}
	Done :
		self->audit_status(!job_err) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace(ReqProc::Close,self) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("done") ;
	} ;

	//
	// ReqInfo
	//

	::string& operator+=( ::string& os , ReqInfo const& ri ) {                        // START_OF_NO_COV
		return os<<"ReqInfo("<<ri.req<<",W:"<<ri.n_wait<<"->"<<ri.n_watchers()<<')' ;
	}                                                                                 // END_OF_NO_COV

	void ReqInfo::_add_watcher(Watcher watcher) {
		switch (_n_watchers) {
			//                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case VectorMrkr : _watchers_v->emplace_back( watcher) ;                 break ; // vector stays vector , simple
			default         : _watchers_a[_n_watchers] = watcher  ; _n_watchers++ ; break ; // array stays array   , simple
			//                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			case NWatchers :                                                                // array becomes vector, complex
				::vector<Watcher>& ws = *new ::vector<Watcher> ; ws.reserve(NWatchers+1) ;
				for( Watcher const& w : _watchers_a ) ws.push_back(w) ;
				//vvvvvvvvvvvvvvvvvvv
				ws.push_back(watcher) ;
				//^^^^^^^^^^^^^^^^^^^
				_watchers_a.~array() ;
				new (&_watchers_v) ::unique_ptr<::vector<Watcher>>{&ws} ;
				_n_watchers = VectorMrkr ;
		}
	}

	void ReqInfo::wakeup_watchers() {
		SWEAR(!waiting()) ;                                                          // dont wake up watchers if we are not ready
		::vector<Watcher> watchers ;                                                 // copy watchers aside before calling them as during a call, we could become not done and be waited for again
		auto go = [&]( Watcher* start , WatcherIdx n ) {
			// we are done for a given RunAction, but calling make on a dependent may raise the RunAciton and we can become waiting() again
			for( Watcher* p=start ; p<start+n ; p++ )
				if      (waiting()     ) _add_watcher(*p) ;                          // if waiting again, add back watchers we have got and that we no more want to call
				else if (p->is_a<Job>()) Job (*p)->wakeup(Job (*p)->req_info(req)) ; // ok, we are still done, we can call watcher
				else                     Node(*p)->wakeup(Node(*p)->req_info(req)) ; // .
		} ;
		// move watchers aside before calling them as during a call, we could become not done and be waited for again
		if (_n_watchers==VectorMrkr) {
			::unique_ptr<::vector<Watcher>> watchers = ::move(_watchers_v) ;
			_watchers_v.~unique_ptr() ;                                              // transform vector into array as there is no watchers any more
			new(&_watchers_a) ::array <Watcher,NWatchers> ;
			_n_watchers = 0 ;
			go( watchers->data() , watchers->size() ) ;
		} else {
			::array watchers = _watchers_a ;
			uint8_t n        = _n_watchers ;
			_n_watchers = 0 ;
			go( watchers.data() , n ) ;
		}
	}

	//
	// ReqData
	//

	void ReqData::clear() {
		Trace trace("clear",job) ;
		SWEAR( !n_running() , n_running() ) ;
		if ( +job && job->rule()->special==Special::Req ) job.pop(idx());
		self = {} ;
	}

	void ReqData::_open_log() {
		static ::string const Last = cat(AdminDirS,"last_output") ;
		Trace trace("_open_log") ;
		Pdate    now { New }         ;
		::string day = now.day_str() ;
		unlnk(Last) ;
		start_pdate = now ;
		if ( uint32_t hd = g_config->console.history_days ) {
			::string lcl_log_dir_s = "outputs/"+day+'/' ;
			::string lcl_log_file  ;
			::string log_file      ;
			for( int i=0 ;; i++ ) {                                                              // try increasing resolution in file name until no conflict
				lcl_log_file = lcl_log_dir_s+now.str(i,true/*in_day*/) ;
				log_file     = AdminDirS+lcl_log_file                  ;
				if (FileInfo(log_file).tag()==FileTag::None) break ;                             // else conflict => try higher resolution
				SWEAR( i<=9 , i ) ;                                                              // at ns resolution, it impossible to have a conflict
			}
			trace(log_file) ;
			//
			::string log_dir_s = AdminDirS+lcl_log_dir_s ;
			if (mk_dir_s(log_dir_s)<log_dir_s.size()-1) {                                        // dir was created, check if we must unlink old ones, this is slow but happens at most once a day
				::string   outputs_dir_s = cat(AdminDirS,"outputs/") ;
				::vector_s entries       = lst_dir_s(outputs_dir_s)  ;
				trace(hd,entries.size()) ;
				if (entries.size()>hd) {
					::sort(entries) ;
					for( ::string const& e : ::span_s(entries.data(),entries.size()-hd) ) {
						SWEAR( e!=day , e,day ) ;                                                // day is supposed to be the most recent and we keep at least 1 entry
						::string f = outputs_dir_s+e ;
						trace("unlnk",f) ;
						unlnk( f , {.dir_ok=true} ) ;
					}
				}
			}
			log_fd = Fd( log_file , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ) ;
			try         { sym_lnk(Last,lcl_log_file) ;                                         }
			catch (...) { exit(Rc::System,"cannot create symlink ",Last," to ",lcl_log_file) ; }
			start_ddate = FileInfo(log_file).date ;                                              // use log_file as a date marker
		} else {
			trace("no_log") ;
			AcFd( Last , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ) ;                              // use Last as a marker, just to gather its date
			start_ddate = FileInfo(Last).date ;
			unlnk(Last) ;
		}
	}

	void ReqData::audit_summary(bool err) const {
		bool warning = +frozen_jobs || +no_triggers || +clash_nodes ;
		audit_info( err ? Color::Err : warning ? Color::Warning : Color::Note ,
			"+---------+\n"
			"| SUMMARY |\n"
			"+---------+\n"
		) ;
		size_t wk = ::max(::strlen("elapsed"),::strlen("startup")) ;
		size_t wn = 0                                              ;
		for( JobReport jr : iota(All<JobReport>) ) if ( stats.ended[+jr] || jr==JobReport::Done ) {
			wk = ::max( wk , snake(jr)                    .size() ) ;
			wn = ::max( wn , ::to_string(stats.ended[+jr]).size() ) ;
		}
		for( JobReport jr : iota(All<JobReport>) ) if ( stats.ended[+jr] || jr==JobReport::Done ) {
			Color c = Color::Note ;
			switch (jr) {
				case JobReport::Failed  :
				case JobReport::LostErr : c = Color::Err     ; break ;
				case JobReport::Lost    : c = Color::Warning ; break ;
				case JobReport::Steady  :
				case JobReport::Done    : c = Color::Ok      ; break ;
			DN}
			::string t = +stats.jobs_time[+jr] ? stats.jobs_time[+jr].short_str() : ::string(Delay::ShortStrSz,' ') ;
			audit_info( c , widen(snake_str(jr),wk)+" time : "+t+" ("+widen(cat(stats.ended[+jr]),wn,true/*right*/)+" jobs)" ) ;
		}
		/**/                                   audit_info( Color::Note , cat(widen("elapsed",wk)," time : ",(Pdate(New)-start_pdate).short_str()) ) ;
		if (+options.startup_dir_s           ) audit_info( Color::Note , cat(widen("startup",wk)," dir  : ",options.startup_dir_s,rm_slash      ) ) ;
		//
		if (+up_to_dates) {
			static ::string src_msg       = "file is a source"       ;
			static ::string anti_msg      = "file is anti"           ;
			static ::string plain_ok_msg  = "was already up to date" ;
			static ::string plain_err_msg = "was already in error"   ;
			size_t w = 0 ;
			for( Node n : up_to_dates ) n->set_buildable() ;
			for( Node n : up_to_dates )
				if      (n->is_src_anti()                ) w = ::max(w,(FileInfo(n->name()).exists()?src_msg     :anti_msg     ).size()) ;
				else if (n->status()<=NodeStatus::Makable) w = ::max(w,(n->ok()!=No                 ?plain_ok_msg:plain_err_msg).size()) ;
			for( Node n : up_to_dates )
				if      (n->is_src_anti()                ) audit_node( Color::Warning                     , widen(FileInfo(n->name()).exists()?src_msg     :anti_msg     ,w)+" :" , n ) ;
				else if (n->status()<=NodeStatus::Makable) audit_node( n->ok()==No?Color::Err:Color::Note , widen(n->ok()!=No                 ?plain_ok_msg:plain_err_msg,w)+" :" , n ) ;
		}
		if (+frozen_jobs) {
			::vector<Job> frozen_jobs_sorted = frozen_jobs ;
			size_t w = 0 ;
			for( Job j : frozen_jobs_sorted ) w = ::max( w , j->rule()->user_name().size() ) ;
			for( Job j : frozen_jobs_sorted ) audit_info( j->err()?Color::Err:Color::Warning , "frozen "+widen(j->rule()->user_name(),w) , j->name() ) ;
		}
		if (+frozen_nodes) {
			::vector<Node> frozen_nodes_sorted = frozen_nodes ;
			for( Node n : frozen_nodes_sorted ) audit_node( Color::Warning , "frozen " , n ) ;
		}
		if (+no_triggers) {
			::vector<Node> no_triggers_sorted = no_triggers ;
			for( Node n : no_triggers_sorted ) audit_node( Color::Warning , "no trigger" , n ) ;
		}
		if (+clash_nodes) {
			::vmap<Node,::pair<Job,Job>> clash_nodes_sorted = clash_nodes ;
			audit_info( Color::Warning , "These files have been written by several simultaneous jobs and lmake was unable to reliably recover\n" ) ;
			for( auto [n,jj] : clash_nodes_sorted ) {
				size_t w = ::max( jj.first->rule()->user_name().size() , jj.second->rule()->user_name().size() ) ;
				audit_node( Color::Warning                             , {}                                      , n                 , 1/*lvl*/ ) ;
				audit_info( jj.first ->err()?Color::Err:Color::Warning , widen(jj.first ->rule()->user_name(),w) , jj.first ->name() , 2/*lvl*/ ) ;
				audit_info( jj.second->err()?Color::Err:Color::Warning , widen(jj.second->rule()->user_name(),w) , jj.second->name() , 2/*lvl*/ ) ;
			}
			if ( Rule r=job->rule() ; r->special!=Special::Req) {
				audit_info( Color::Warning , "consider : lmake -R "+mk_shell_str(r->user_name())+" -J "+mk_file(job->name(),FileDisplay::Shell) ) ;
			} else {
				::string dl ;
				for( Dep const& d : job->deps ) dl<<' '<<mk_shell_str(d->name()) ;
				audit_info( Color::Warning , "consider : lmake"+dl ) ;
			}
		}
	}

	void ReqData::audit_job( Color c , Pdate date , ::string const& step , ::string const& rule_name , ::string const& job_name , in_addr_t host , Delay exe_time ) const {
		::string msg ;
		if (g_config->console.date_prec!=uint8_t(-1)) msg <<            date.str(g_config->console.date_prec,true/*in_day*/)                             <<' ' ;
		if (g_config->console.host_len              ) msg <<      widen(SockFd::s_host(host)                                ,g_config->console.host_len) <<' ' ;
		/**/                                          msg <<      widen(step                                                ,StepSz                    )       ;
		/**/                                          msg <<' '<< widen(rule_name                                           ,Rule::s_rules->name_sz    )       ;
		if (g_config->console.has_exe_time          ) msg <<' '<< widen((+exe_time?exe_time.short_str():"")                 ,6                         )       ;
		/**/                                          msg <<' '<<       mk_file(job_name)                                                                      ;
		audit( audit_fd , log_fd , options , c , msg ) ;
		last_info = {} ;
	}

	static void          _audit_status( Fd out , Fd log , ReqOptions const& ro , bool ok )       { audit_status (out     ,log   ,ro     ,ok?Rc::Ok:Rc::Fail) ; } // allow access to global function ...
	/**/   void ReqData::audit_status (                                          bool ok ) const { _audit_status(audit_fd,log_fd,options,ok                ) ; } // ... w/o naming namespace

	bool/*seen*/ ReqData::audit_stderr( Job j , MsgStderr const& msg_stderr , uint16_t max_stderr_len , DepDepth lvl ) const {
		if (+msg_stderr.msg   ) audit_info( Color::Note , msg_stderr.msg , lvl+1 ) ;
		if (!msg_stderr.stderr) return +msg_stderr.msg ;
		if (max_stderr_len) {
			::string_view shorten = first_lines(msg_stderr.stderr,max_stderr_len) ;
			if (shorten.size()<msg_stderr.stderr.size()) {
				audit_as_is(::string(shorten)) ;
				audit_info( Color::Note , "... (for full content : lshow -e -R "+mk_shell_str(j->rule()->user_name())+" -J "+mk_file(j->name(),FileDisplay::Shell)+" )" , lvl+1 ) ;
				return true ;
			}
		}
		audit_as_is(msg_stderr.stderr) ;
		return true ;
	}

	void ReqData::audit_stats() const {
		try {
			ReqRpcReply rrr{
				ReqRpcReplyProc::Stdout
			,	title(options,cat(
					stats.ended[+JobReport::Failed]                   ? cat( "failed:"   ,  stats.ended[+JobReport::Failed]              ,' '        ) : ""s
				,	                                                    cat( "done:"     , (stats.done()-stats.ended[+JobReport::Failed])            )
				,	+g_config->caches && stats.ended[+JobReport::Hit] ? cat( " hit:"     ,  stats.ended[+JobReport::Hit   ]                          ) : ""s
				,	stats.ended[+JobReport::Rerun ]                   ? cat( " rerun:"   ,  stats.ended[+JobReport::Rerun ]                          ) : ""s
				,	                                                    cat( " running:" ,  stats.cur(JobStep::Exec  )                               )
				,	stats.cur(JobStep::Queued)                        ? cat( " queued:"  ,  stats.cur(JobStep::Queued)                               ) : ""s
				,	stats.cur(JobStep::Dep   )>1                      ? cat( " waiting:" , (stats.cur(JobStep::Dep   )-!options.flags[ReqFlag::Job]) ) : ""s // suppress job representing Req itself
				,	g_config->console.show_ete                        ? cat( " - ETE:"   ,  et2.short_str()                                          ) : ""s
				,	g_config->console.show_eta                        ? cat( " - ETA:"   ,  et1.str(0/*prec*/,true/*in_day*/)                        ) : ""s
				))
			} ;
			OMsgBuf(rrr).send( audit_fd , {}/*key*/ ) ;
		} catch (::string const&) {}                    // if client has disappeared, well, we cannot do much
	}

	bool/*overflow*/ ReqData::_send_err( bool intermediate , ::string const& pfx , ::string const& target , size_t& n_err , DepDepth lvl ) {
		if (!n_err) return true/*overflow*/ ;
		n_err-- ;
		if (n_err) audit_info( intermediate?Color::HiddenNote:Color::Err , widen(pfx,::max(size_t(26)/*missing transient sub-file*/,Rule::s_rules->name_sz)) , target , lvl ) ;
		else       audit_info( Color::Warning                            , "..."                                                                                            ) ;
		return !n_err/*overflow*/ ;
	}

	void ReqData::_report_no_rule( Node node , NfsGuard* nfs_guard , DepDepth lvl ) {
		::string                        name      = node->name() ;
		::vmap<RuleTgt,Rule::RuleMatch> mrts      ;                        // matching rules
		RuleTgt                         art       ;                        // set if an anti-rule matches
		RuleIdx                         n_missing = 0            ;         // number of rules missing deps
		//
		if (node->buildable==Buildable::PathTooLong) {
			audit_node( Color::Warning , "name is too long :" , node , lvl ) ;
			audit_info( Color::Note    , cat("consider : lmake.config.max_path = ",name.size()," (or larger)") , lvl+1 ) ;
			return ;
		}
		//
		if ( node->status()==NodeStatus::Uphill || node->status()==NodeStatus::Transient ) {
			Node dir ; for( dir=node->dir ; +dir && (dir->status()==NodeStatus::Uphill||dir->status()==NodeStatus::Transient) ; dir=dir->dir ) ;
			swear_prod(+dir                              ,"dir is buildable for",name,"but cannot find buildable dir"                  ) ;
			swear_prod(dir->status()<=NodeStatus::Makable,"dir is buildable for",name,"but cannot find buildable dir until",dir->name()) ;
			/**/                                audit_node( Color::Err  , "no rule for"        , node , lvl   ) ;
			if (dir->status()==NodeStatus::Src) audit_node( Color::Note , "dir is a source :"  , dir  , lvl+1 ) ;
			else                                audit_node( Color::Note , "dir is buildable :" , dir  , lvl+1 ) ;
			return ;
		}
		//
		Rule prev_rule ;
		for( RuleTgt rt : Node::s_rule_tgts(name).view() ) {               // first pass to gather info : mrts : matching rules, n_missing : number of missing deps
			Rule            r = rt->rule    ; if (r==prev_rule) continue ; // only consider first match for any given rule
			Rule::RuleMatch m { rt , name } ;
			if (!m                              )              continue ;
			if (rt->rule->special==Special::Anti) { art = rt ; break    ; }
			//
			if ( JobTgt jt{::copy(m),rt.sure()} ; +jt && jt->run_status!=RunStatus::MissingStatic ) goto Continue ; // do not pass self as req to avoid generating error message at cxtor time
			try                      { rt->rule->deps_attrs.eval(m) ; }
			catch (MsgStderr const&) { goto Continue ;                }                                             // do not consider rule if deps cannot be computed
			prev_rule = rt->rule ;
			n_missing++ ;
		Continue :
			mrts.emplace_back(rt,::move(m)) ;
		}
		//
		if ( !art && !mrts                                          ) audit_node( Color::Err  , "no rule match"      , node , lvl   ) ;
		else                                                          audit_node( Color::Err  , "no rule for"        , node , lvl   ) ;
		if ( !art && FileInfo(name,{.nfs_guard=nfs_guard}).exists() ) audit_node( Color::Note , "consider : git add" , node , lvl+1 ) ;
		//
		for( auto const& [rt,m] : mrts ) {                                                     // second pass to do report
			Rule              r           = rt->rule                ;
			JobTgt            jt          { ::copy(m) , rt.sure() } ;                          // do not pass self as req to avoid generating error message at cxtor time
			::string          reason      ;
			Node              missing_dep ;
			::vmap_s<DepSpec> static_deps ;
			//
			if ( +jt && jt->run_status!=RunStatus::MissingStatic  ) {
				reason << "does not produce it" ;
				goto Report ;
			}
			if ( ::pair_s<VarIdx> msg=m.reject_msg() ; +msg.first ) {
				::pair_s<RuleData::MatchEntry> const& k_me = r->matches[msg.second] ;
				reason << "non-canonic "<<k_me.second.flags.kind()<<' '<<k_me.first<<" : "<<msg.first ;
				goto Report ;
			}
			//
			try                              { static_deps = r->deps_attrs.dep_specs(m) ;                                                         }
			catch (::string  const& msg    ) { reason << "cannot compute its deps :\n"<<indent<' ',2>(msg                       ) ; goto Report ; }
			catch (MsgStderr const& msg_err) { reason << "cannot compute its deps :\n"<<indent<' ',2>(msg_err.msg+msg_err.stderr) ; goto Report ; }
			//
			for( bool search_non_buildable : {true,false} )                                    // first search a non-buildable, if not found, search for non makable as deps have been made
				for( auto const& [k,ds] : static_deps ) {
					if (!is_canon(ds.txt,true/*ext_ok*/)) {
						if (search_non_buildable) continue ;                                   // non-canonic deps are detected after non-buidlable ones
						const char* tl = +options.startup_dir_s ? " (top-level)" : "" ;
						if (+ds.txt) reason = "non-canonic static dep "+k+tl+" : "+ds.txt ;
						else         reason = "empty static dep "      +k                 ;
						goto Report ;
					}
					Node d { ds.txt } ; SWEAR( +d , ds.txt ) ;
					if ( search_non_buildable ? d->buildable>Buildable::No : d->status()<=NodeStatus::Makable ) continue ;
					missing_dep = d ;
					SWEAR(+missing_dep) ;                                                      // else why wouldn't it apply ?!?
					FileTag tag = FileInfo(missing_dep->name(),{.nfs_guard=nfs_guard}).tag() ;
					reason = "misses static dep " + k + (tag>=FileTag::Target?" (existing)":tag==FileTag::Dir?" (dir)":"") ;
					goto Report ;
				}
		Report :
			if (+missing_dep) audit_node( Color::Note , "rule "+r->user_name()+' '+reason+" :" , missing_dep , lvl+1 ) ;
			else              audit_info( Color::Note , "rule "+r->user_name()+' '+reason      ,               lvl+1 ) ;
			//
			if ( +missing_dep && n_missing==1 && (!g_config->max_err_lines||lvl<g_config->max_err_lines) ) _report_no_rule( missing_dep , nfs_guard , lvl+2 ) ;
		}
		//
		if (+art) audit_info( Color::Note , "anti-rule "+art->rule->user_name()+" matches" , lvl+1 ) ;
	}

	//
	// JobAudit
	//

	::string& operator+=( ::string& os , JobAudit const& ja ) { // START_OF_NO_COV
		/**/                os << "JobAudit(" << ja.report ;
		if (+ja.msg       ) os <<','<< ja.msg              ;
		if ( ja.has_stderr) os <<",has_stderr"             ;
		return              os <<')'                       ;

	} // END_OF_NO_COV

}
