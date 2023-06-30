// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#ifdef STRUCT_DECL
namespace Engine {

	struct Job        ;
	struct JobTgt     ;
	struct JobTgts    ;
	struct JobData    ;
	struct JobReqInfo ;

	static constexpr uint8_t JobNGuardBits = 2 ;           // one to define JobTgt, the other to put it in a CrunchVector

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	ENUM( AncillaryTag
	,	Data
	,	KeepTmp
	)

	ENUM_1( JobMakeAction
	,	Dec = Wakeup                   // if >=Dec => n_wait must be decremented
	,	None                           //                                         trigger analysis from dependent
	,	Wakeup                         //                                         a watched dep is available
	,	End                            // if >=End => job is ended,               job has completed
	,	PrematureEnd                   //                                         job was killed before starting
	)

	ENUM_1( SpecialStep
	,	Err = ErrNoFile                // >=Err means error
	,	Idle
	,	NoFile
	,	Steady
	,	Modified
	,	ErrNoFile
	)

	struct Job : JobBase {
		friend ::ostream& operator<<( ::ostream& , Job const ) ;
		using MakeAction = JobMakeAction ;
		using JobBase::side ;

		using ReqInfo = JobReqInfo ;

		// statics
		static ::pair_s<NodeIdx> s_reason_str(JobReason) ;
		// static data
	private :
		static ::shared_mutex    _s_target_dirs_mutex ;
		static ::umap_s<NodeIdx> _s_target_dirs       ;    // dirs created for job execution that must not be deleted // XXX : use Node rather than string

		// cxtors & casts
	public :
		using JobBase::JobBase ;
		Job( RuleTgt , ::string const& target , DepDepth lvl=0 ) ;             // plain Job, match on target
		//
		Job( Special ,               ::vmap<Node,DFlags> const& deps ) ;       // Job used to represent a Req
		Job( Special , Node target , ::vmap<Node,DFlags> const& deps ) ;       // special job
		Job( Special , Node target , ::vector<JobTgt>    const&      ) ;       // multi

		// accesses
		::string name     () const ;
		::string user_name() const ;
		//
		bool           has_req      (Req           ) const ;
		ReqInfo const& c_req_info   (Req           ) const ;
		ReqInfo      & req_info     (Req           ) const ;
		ReqInfo      & req_info     (ReqInfo const&) const ;                   // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs         (              ) const ;
		::vector<Req>  running_reqs (              ) const ;
		::vector<Req>  old_done_reqs(              ) const ;
		//
		bool active() const ;
		//
		//services
		void                            fill_rpc_reply( JobRpcReply& , Rule::SimpleMatch const& , ::vector_s const& rsrcs ) const ; // thread-safe
		::vector<Node>/*report_unlink*/ wash          ( Rule::SimpleMatch const&                                          ) const ; // thread-safe
		//
		void     end_exec      (                               ) const ;       // thread-safe
		::string ancillary_file(AncillaryTag=AncillaryTag::Data) const ;
		::string special_stderr(Node                           ) const ;
		::string special_stderr(                               ) const ;       // cannot declare a default value for incomplete type Node
		//
		void              invalidate_old() ;
		Rule::SimpleMatch simple_match  () const ;                             // thread-safe
		Rule::Match       match         () const ;
		//
		void set_pressure( ReqInfo& , CoarseDelay ) const ;
		//
		JobReason make( ReqInfo& , RunAction , JobReason={} , MakeAction=MakeAction::None , CoarseDelay const* old_exec_time=nullptr , bool wakeup_watchers=true ) ;
		//
		JobReason make( ReqInfo& ri , MakeAction ma ) { return make(ri,RunAction::None,{},ma) ; } // need same signature as for Node::make to use in templated watcher wake up
		//
		bool/*maybe_new_deps*/ submit( ReqInfo& , JobReason , CoarseDelay pressure ) ;
		//
		bool/*ok*/ forget() ;
		//
		void add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) ;
		//
		void audit_end_special( Req , SpecialStep , Node ) const ;
		void audit_end_special( Req , SpecialStep        ) const ;
		//
		void audit_end( ::string const& pfx , ReqInfo const& , ::string const& stderr , AnalysisErr const& analysis_err , size_t stderr_len , bool modified , Delay exec_time={} ) const ;
		//
	private :
		bool/*maybe_new_deps*/ _submit_special  ( Req                                                                                                     ) ;
		bool                   _targets_ok      ( Req      , Rule::Match const&                                                                           ) ;
		bool/*maybe_new_deps*/ _submit_plain    ( ReqInfo& ,             JobReason ,              CoarseDelay pressure                                    ) ;
		void                   _set_pressure_raw( ReqInfo& , CoarseDelay                                                                                  ) const ;
		JobReason              _make_raw        ( ReqInfo& , RunAction , JobReason , MakeAction , CoarseDelay const* old_exec_time , bool wakeup_watchers ) ;

	} ;

	struct JobTgt : Job {
		static_assert(Job::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Job::NGuardBits-1       ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , JobTgt const ) ;
		// cxtors & casts
		JobTgt(                                                 ) = default ;
		JobTgt( Job j , bool is=false                           ) : Job(j ) { is_sure( +j && is )   ; } // if no job, ensure JobTgt appears as false
		JobTgt( RuleTgt rt , ::string const& t , DepDepth lvl=0 ) ;
		JobTgt( JobTgt const& jt                                ) : Job(jt) { is_sure(jt.is_sure()) ; }
		//
		JobTgt& operator=(JobTgt const& jt) { Job::operator=(jt) ; is_sure(jt.is_sure()) ; return *this ; }
		// accesses
		Idx operator+() const { return Job::operator+() | is_sure()<<(NValBits-1) ; }
		//
		bool is_sure(        ) const { return Job::side<1>(   ) ; }
		void is_sure(bool val)       {        Job::side<1>(val) ; }
		bool sure   (        ) const ;
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const = delete ; // { return Job::side<W,LSB+1>(   ) ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       = delete ; // {        Job::side<W,LSB+1>(val) ; }
		// services
		Bool3 produces(Node) const ;
	} ;

	struct JobTgts : JobTgtsBase {
		friend ::ostream& operator<<( ::ostream& , JobTgts const ) ;
		// cxtors & casts
		using JobTgtsBase::JobTgtsBase ;
	} ;

	struct JobExec : Job {
		friend ::ostream& operator<<( ::ostream& , JobExec const ) ;
		// cxtors & casts
		JobExec(                                      ) = default ;
		JobExec( Job j , ::string&& h , ProcessDate d ) : Job{j} , host{::move(h)} , date{d} {}
		JobExec( Job j ,                ProcessDate d ) : Job{j} ,                   date{d} {}
		// services
		void             report_start ( ReqInfo& , ::vector<Node> const& report_unlink={} ) const ;
		void             report_start (                                                   ) const ; // called in engine thread after start if started called with false
		void             started      ( bool report , ::vector<Node> const& report_unlink ) ;       // called in engine thread after start
		void             live_out     ( ::string const&                                   ) const ;
		JobRpcReply      job_info     ( JobProc , ::vector<Node> const& deps              ) const ; // answer to requests from job execution
		bool/*modified*/ end          ( ProcessDate start , JobDigest const&              ) ;       // hit indicates that result comes from a cache hit
		void             premature_end( Req , bool report=true                            ) ;       // Req is killed but job is necessary for some other req
		void             not_started  (                                                   ) ;       // Req was killed before it started
		//
		//
		void audit_end( ::string const& pfx , ReqInfo const& , ::string const& stderr , AnalysisErr const& analysis_err , size_t stderr_len , bool modified , Delay exec_time={} ) const ;
		// data
		::string    host ;             // host executing the job
		ProcessDate date ;             // date at which action has been created (may be reported later to user, but with this date)
	} ;
}
#endif
#ifdef DATA_DEF
namespace Engine {

	ENUM_1( RunStatus
	,	Err = TargetErr                // >=Err means job is in error before even starting
	,	Complete                       // job was run
	,	NoDep                          // job was not run because of missing static dep
	,	TargetErr                      // job was not run because of a manual static target
	,	DepErr                         // job was not run because of dep error
	,	RsrcsErr                       // job was not run because of resources could not be computed
	)

	struct JobData {
		using Idx = Job::Idx ;
		// statics
		static bool s_frozen(Status status) { return status==Status::Frozen || status==Status::ErrFrozen ; }
		// cxtors & casts
		JobData() = default ;
		JobData( Special sp , Deps ds={} ) : deps{ds} , rule{sp} {             // special Job, all deps
			SWEAR(sp!=Special::Unknown) ;
			exec_gen = rule->x_match_cmd.spec.force ? ExecGenForce : NExecGen ; // unless forced, special jobs are always exec_ok
		}
		JobData( Rule r , Deps sds ) : deps{sds} , rule{r} {                   // plain Job, static deps
			SWEAR(!rule.is_special()) ;
		}
		//
		JobData           (JobData&& jd) : JobData(jd) {                                 jd.star_targets.forget() ; jd.deps.forget() ;                }
		~JobData          (            ) {                                                  star_targets.pop   () ;    deps.pop   () ;                }
		JobData& operator=(JobData&& jd) { SWEAR(rule==jd.rule) ; *this = mk_const(jd) ; jd.star_targets.forget() ; jd.deps.forget() ; return *this ; }
	private :
		JobData           (JobData const&) = default ;
		JobData& operator=(JobData const&) = default ;
		// accesses
	public :
		bool cmd_ok    () const { return exec_gen >=                     rule->cmd_gen                  ; }
		bool exec_ok   () const { return exec_gen >= (status==Status::Ok?rule->cmd_gen:rule->rsrcs_gen) ; } // dont care about rsrcs if job went ok
		bool forced    () const { return exec_gen == ExecGenForce                                       ; }
		bool frozen    () const { return s_frozen(status)                                               ; }
		bool is_special() const { return rule.is_special() || frozen()                                  ; }
		//
		void exec_ok(bool ok) { SWEAR(!rule.is_special()) ; if (!forced()) exec_gen = ok ? rule->rsrcs_gen : 0 ; } // forced jobs stay forced
		//
		//
		::pair<Delay,bool/*from_rule*/> best_exec_time() const {
			if (rule.is_special()) return { {}              , false } ;
			if (+exec_time       ) return {       exec_time , false } ;
			else                   return { rule->exec_time , true  } ;
		}
		//
		bool sure    () const ;
		bool force   () const { return exec_gen==ExecGenForce ;                }
		void mk_sure ()       { match_gen = Rule::s_match_gen ; _sure = true ; }
		void mk_force()       { exec_gen  = ExecGenForce      ;                }
		//
		bool err() const {
			if (run_status>=RunStatus::Err     ) return true                ;
			if (run_status!=RunStatus::Complete) return false               ;
			else                                 return status>=Status::Err ;
		}
		//
		// data
		DiskDate         db_date      ;                                                         //     64 bits,                        oldest db_date at which job is coherent (w.r.t. its state)
		ProcessDate      end_date     ;                                                         //     64 bits,
		Targets          star_targets ;                                                         //     32 bits, owned, for plain jobs
		Deps             deps         ;                                                         // 31<=32 bits, owned
		Rule             rule         ;                                                         //     16 bits,                        can be retrieved from full_name, but would be slower
		CoarseDelay      exec_time    ;                                                         //     16 bits       , for plain jobs
		ExecGen          exec_gen     = 0                   ;                                   //   <= 8 bits,      , for plain jobs, cmd generation of rule
		mutable MatchGen match_gen    = 0                   ;                                   //   <= 8 bits,                        if <Rule::s_match_gen => deemed !sure
		Tokens           tokens       = 1                   ;                                   //   <= 8 bits,      , for plain jobs, number of tokens for eta computation
		RunStatus        run_status:3 = RunStatus::Complete ; static_assert(+RunStatus::N< 8) ; //      3 bits
		Status           status    :4 = Status::New         ; static_assert(+Status   ::N<16) ; //      4 bits
	private :
		mutable bool     _sure     :1 = false               ;                                   //      1 bit
	} ;
	static_assert(sizeof(JobData)==32) ;                                       // check expected size

	ENUM( MissingAudit
	,	No
	,	Steady
	,	Modified
	)

	struct JobReqInfo : ReqInfo<Node> {                                        // watchers of Job's are Node's
		using Base = ReqInfo<Node> ;
		friend ::ostream& operator<<( ::ostream& , JobReqInfo const& ) ;
		using Lvl        = JobLvl        ;
		using MakeAction = JobMakeAction ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		bool running() const {
			switch (lvl) {
				case Lvl::Queued :
				case Lvl::Exec   : return true  ;
				default          : return false ;
			}
		}
		bool done(RunAction ra=RunAction::Status) const { return done_>=ra ; }
		// services
		void update( RunAction , MakeAction , Job ) ;
		void add_watcher( Node watcher , NodeReqInfo& watcher_req_info ) { Base::add_watcher(watcher,watcher_req_info) ; }
		void chk() const {
			SWEAR(done_<=RunAction::Dsk) ;
			switch (lvl) {
				case Lvl::None   : SWEAR(n_wait==0) ; break ;                  // not started yet, cannot wait anything
				case Lvl::Done   : SWEAR(n_wait==0) ; break ;                  // done, cannot wait anything anymore
				case Lvl::Queued :
				case Lvl::Exec   : SWEAR(n_wait==1) ; break ;                  // if running, we are waiting for job execution
				default          : SWEAR(n_wait> 0) ; break ;                  // we must be waiting something if not Done nor None
			}
		}
		// data
		NodeIdx    dep_lvl          = 0                   ;                                   // 31<=32 bits
		RunAction  done_         :3 = RunAction ::None    ; static_assert(+RunAction ::N<8) ; //      3 bits , action for which we are done
		Lvl        lvl           :3 = Lvl       ::None    ; static_assert(+Lvl       ::N<8) ; //      3 bits
		BackendTag backend       :2 = BackendTag::Unknown ; static_assert(+BackendTag::N<4) ; //      2 bits
		bool       start_reported:1 = false               ;                                   //      1 bits , if true <=> start message has been reported to user
	} ;
	static_assert(sizeof(JobReqInfo)==40) ;                                    // check expected size

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// Job
	//

	inline Job::Job( Special sp ,          ::vmap<Node,DFlags> const& deps ) : Job{                               New , sp,deps } { SWEAR(sp==Special::Req) ; }
	inline Job::Job( Special sp , Node t , ::vmap<Node,DFlags> const& deps ) : Job{ {t.name(),Rule(sp).job_sfx()},New , sp,deps } {}

	inline ::string Job::name() const {
		return full_name((*this)->rule->job_sfx_len()) ;
	}
	inline ::string Job::user_name() const {
		::string res = name() ;
		for( char& c : res ) if (c==Rule::StarMrkr) c = '*' ;
		return res ;
	}

	inline bool Job::has_req(Req r) const {
		return Req::s_store[+r].jobs.contains(*this) ;
	}
	inline Job::ReqInfo const& Job::c_req_info(Req r) const {
		::umap<Job,ReqInfo> const& req_infos = Req::s_store[+r].jobs ;
		auto                       it        = req_infos.find(*this) ;         // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].jobs.dflt ;
		else                     return it->second                 ;
	}
	inline Job::ReqInfo& Job::req_info(Req r) const {
		return Req::s_store[+r].jobs.try_emplace(*this,ReqInfo(r)).first->second ;
	}
	inline Job::ReqInfo& Job::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].jobs.dflt) return req_info(cri.req)         ; // allocate
		else                                         return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> Job::reqs() const { return Req::reqs(*this) ; }

	inline ::string Job::special_stderr   (                            ) const { return special_stderr   (         {}) ; }
	inline void     Job::audit_end_special( Req req , SpecialStep step ) const { return audit_end_special(req,step,{}) ; }

	inline Rule::SimpleMatch Job::simple_match() const { return Rule::SimpleMatch(*this) ; }
	inline Rule::Match       Job::match       () const { return Rule::Match      (*this) ; }

	inline void Job::invalidate_old() {
		if ( +(*this)->rule && (*this)->rule.old() ) pop() ;
	}

	inline void Job::add_watcher( ReqInfo& ri , Node watcher , Node::ReqInfo& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void Job::set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                              // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri,pressure) ;
	}

	inline bool Job::active() const { return +*this && !(*this)->rule.old() ; }

	inline JobReason Job::make( ReqInfo& ri , RunAction run_action , JobReason reason , MakeAction make_action , CoarseDelay const* old_exec_time , bool wakeup_watchers ) {
		if ( ri.done(run_action) && !make_action ) return JobReasonTag::None ; // fast path
		return _make_raw(ri,run_action,reason,make_action,old_exec_time,wakeup_watchers) ;
	}

	inline bool/*maybe_new_deps*/ Job::submit( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		if ((*this)->is_special()) return _submit_special(ri.req            ) ;
		else                       return _submit_plain  (ri,reason,pressure) ;
	}

	inline void Job::audit_end( ::string const& pfx , ReqInfo const& cri , ::string const& stderr , AnalysisErr const& analysis_err , size_t stderr_len , bool modified , Delay exec_time ) const {
		JobExec(*this,{},ProcessDate::s_now()).audit_end(pfx,cri,stderr,analysis_err,stderr_len,modified,exec_time) ;
	}

	//
	// JobTgt
	//

	inline JobTgt::JobTgt( RuleTgt rt , ::string const& t , DepDepth lvl ) : JobTgt{ Job(rt,t,lvl) , rt.sure() } {}

	inline bool JobTgt::sure() const { return is_sure() && (*this)->sure() ; }

	inline Bool3 JobTgt::produces(Node t) const {
		if ( (*this)->run_status==RunStatus::NoDep ) return No    ;
		if ( is_sure()                             ) return Yes   ;
		if ( (*this)->err()                        ) return Maybe ;            // if job is in error, we do not trust actual star targets
		if ( t->has_actual_job_tgt(*this)          ) return Yes   ;            // fast path
		//
		return No | ::binary_search( (*this)->star_targets , t ) ;
	}

	//
	// JobData
	//

	inline bool JobData::sure() const {
		if (match_gen>=Rule::s_match_gen) return _sure ;
		_sure     = false             ;
		match_gen = Rule::s_match_gen ;
		for( Dep const& d : deps ) {
			if (!d.flags[DFlag::Static]) break ;
			if (d->buildable!=Yes      ) return false ;
		}
		_sure = true ;
		return true ;
	}

	//
	// JobReqInfo
	//

	inline void JobReqInfo::update( RunAction run_action , MakeAction make_action , Job job ) {
		if ( job->status<=Status::Garbage && run_action>=RunAction::Status ) run_action = RunAction::Run ;
		if (make_action>=MakeAction::Dec) {
			SWEAR(n_wait) ;
			n_wait-- ;
		}
		if (run_action>action) {                                               // increasing action requires to reset checks
			lvl     = lvl & Lvl::Dep ;
			dep_lvl = 0              ;
			action  = run_action     ;
		}
		if (n_wait) {
			SWEAR(make_action<MakeAction::End) ;
		} else if (
			req->zombie                                                        // zombie's need not check anything
		||	make_action==MakeAction::PrematureEnd                              // if not started, no further analysis
		||	( action==RunAction::Makable && job->sure() )                      // no need to check deps, they are guaranteed ok if sure
		) {
			lvl   = Lvl::Done      ;
			done_ = done_ | action ;
		} else if (make_action==MakeAction::End) {
			lvl     = lvl & Lvl::Dep ;                                         // we just ran, reset analysis
			dep_lvl = 0              ;
			action  = run_action     ;                                         // we just ran, we are allowed to decrease action
		}
		SWEAR(lvl!=Lvl::End) ;
	}

}
#endif
