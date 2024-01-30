// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#ifdef STRUCT_DECL
namespace Engine {

	struct Job        ;
	struct JobExec    ;
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
	,	Backend
	,	Data
	,	Dbg
	,	KeepTmp
	)

	ENUM_1( JobMakeAction
	,	Dec = Wakeup                   // if >=Dec => n_wait must be decremented
	,	None                           //                                         trigger analysis from dependent
	,	Wakeup                         //                                         a watched dep is available
	,	End                            // if >=End => job is ended,               job has completed
	,	GiveUp                         //                                         job has completed and no further analysis
	)

	ENUM( SpecialStep                  // ordered by increasing importance
	,	Idle
	,	NoFile
	,	Ok
	,	Err
	)

	struct Job : JobBase {
		friend ::ostream& operator<<( ::ostream& , Job const ) ;
		using JobBase::side ;
		//
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		// static data
	protected :
		static ::map <Time::Pdate,Job        > _s_start_date_to_jobs ;         // start_date of all running jobs, used to detect target clashes
		static ::umap<Job        ,Time::Pdate> _s_job_to_end_dates   ;         // end_dates of all jobs that have ended after the earliest start date mentioned above
		static ::map <Time::Pdate,Job        > _s_end_date_to_jobs   ;         // .
		// cxtors & casts
		Job( Rule::SimpleMatch&& , Req={} , DepDepth lvl=0 ) ;                 // plain Job, req is only for error reporting
	public :
		using JobBase::JobBase ;
		Job( RuleTgt , ::string const& t  , Req={} , DepDepth lvl=0 ) ;        // plain Job, match on target
		Job( Rule    , ::string const& jn , Req={} , DepDepth lvl=0 ) ;        // plain Job, match on name, for use when required from command line
		//
		Job( Special ,               Deps deps               ) ;               // Job used to represent a Req
		Job( Special , Node target , Deps deps               ) ;               // special job
		Job( Special , Node target , ::vector<JobTgt> const& ) ;               // multi
		// accesses
		bool active() const ;
		Time::Pdate end_date() const {
			auto it = _s_job_to_end_dates.find(*this) ;
			if (it==_s_job_to_end_dates.end()) return {}         ;             // job still running or has ended long ago, no risk of clash (clash is detected upon the end of the second job)
			else                               return it->second ;
		}
	} ;

	struct JobTgt : Job {
		static_assert(Job::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Job::NGuardBits-1       ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , JobTgt ) ;
		// cxtors & casts
		JobTgt(                                                              ) = default ;
		JobTgt( Job j , bool is=false                                        ) : Job(j ) { is_sure( +j && is )   ; } // if no job, ensure JobTgt appears as false
		JobTgt( RuleTgt rt , ::string const& t , Req req={} , DepDepth lvl=0 ) ;
		JobTgt( JobTgt const& jt                                             ) : Job(jt) { is_sure(jt.is_sure()) ; }
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
		bool produces( Node , bool sure=false ) const ;                        // if sure, reply true only if it is certain than node is produced
	} ;

	struct JobTgts : JobTgtsBase {
		friend ::ostream& operator<<( ::ostream& , JobTgts ) ;
		// cxtors & casts
		using JobTgtsBase::JobTgtsBase ;
	} ;

	struct JobExec : Job {
		friend ::ostream& operator<<( ::ostream& , JobExec const& ) ;
		// cxtors & casts
		JobExec() = default ;
		//
		JobExec( Job j ,               Pdate s={}              ) : Job{j} ,           start_{s}                              {} // not executing or started job
		JobExec( Job j , in_addr_t h , Pdate s={} , Pdate e={} ) : Job{j} , host{h} , start_{s} , end_{e                   } {}
		JobExec( Job j , in_addr_t h , Pdate s    , NewType    ) : Job{j} , host{h} , start_{s} , end_{Time::Pdate::s_now()} {}
		//
		JobExec( Job j , in_addr_t h , NewType           ) : Job{j} , host{h} , start_{Time::Pdate::s_now()}                {} // starting job
		JobExec( Job j ,               NewType           ) : Job{j} ,           start_{Time::Pdate::s_now()}                {} // .
		JobExec( Job j ,               NewType , NewType ) : Job{j} ,           start_{Time::Pdate::s_now()} , end_{start_} {} // instantaneous job, no need to distinguish start, cannot have host
		// services
		// called in main thread after start
		bool/*reported*/ report_start( ReqInfo&    , ::vector<Node> const& report_unlink={} , ::string const& stderr={} , ::string const& backend_msg={} ) const ;
		void             report_start(                                                                                                                   ) const ;
		void             started     ( bool report , ::vector<Node> const& report_unlink={} , ::string const& stderr={} , ::string const& backecn_msg={} ) ;
		//
		void live_out( ReqInfo& , ::string const& ) const ;
		void live_out(            ::string const& ) const ;
		//
		JobRpcReply      job_info( JobProc , ::vector<Dep> const& deps                                     ) const ; // answer to requests from job execution
		bool/*modified*/ end     ( ::vmap_ss const& rsrcs , JobDigest const& , ::string const& backend_msg ) ;       // hit indicates that result is from a cache hit
		void             give_up ( Req={} , bool report=true                                               ) ;       // Req (all if 0) was killed and job was not killed (not started or continue)
		//
		// audit_end returns the report to do if job is finally not rerun
		JobReport audit_end( ::string const& pfx , ReqInfo const&     , ::string const& msg , ::string const& stderr    , size_t max_stderr_len=-1 , bool modified=true , Delay exec_time={} ) const ;
		JobReport audit_end( ::string const& pfx , ReqInfo const& cri ,                       ::string const& stderr={} , size_t max_stderr_len=-1 , bool modified=true , Delay exec_time={} ) const {
			return audit_end(pfx,cri,{}/*msg*/,stderr,max_stderr_len,modified,exec_time) ;
		}
		// start/end date book keeping
	private :
		void _set_start_date() ;
		void _set_end_date  () ;
		// data
	public :
		in_addr_t host   = NoSockAddr ;
		Pdate     start_ ;
		Pdate     end_   ;
	} ;

}
#endif
#ifdef INFO_DEF
namespace Engine {

	struct JobReqInfo : ReqInfo {                                              // watchers of Job's are Node's
		friend ::ostream& operator<<( ::ostream& , JobReqInfo const& ) ;
		using Lvl        = JobLvl        ;
		using MakeAction = JobMakeAction ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// accesses
		bool running() const {
			switch (lvl) {
				case Lvl::Queued :
				case Lvl::Exec   : return true  ;
				default          : return false ;
			}
		}
		// services
		void update( RunAction , MakeAction , JobData const& ) ;
		void add_watcher( Node watcher , NodeReqInfo& watcher_req_info ) { ReqInfo::add_watcher(watcher,watcher_req_info) ; }
		void chk() const {
			switch (lvl) {
				case Lvl::None   : SWEAR(n_wait==0) ; break ;                  // not started yet, cannot wait anything
				case Lvl::Done   : SWEAR(n_wait==0) ; break ;                  // done, cannot wait anything anymore
				case Lvl::Queued :
				case Lvl::Exec   : SWEAR(n_wait==1) ; break ;                  // if running, we are waiting for job execution
				default          : SWEAR(n_wait> 0) ; break ;                  // we must be waiting something if not Done nor None
			}
		}
		// data
		// req independent (identical for all Req's) : these fields are there as there is no Req-independent non-persistent table
		NodeIdx      dep_lvl            = 0                     ; // ~20<=32 bits
		Lvl          lvl             :3 = Lvl         ::None    ; //       3 bits
		JobReasonTag force           :5 = JobReasonTag::None    ; //       5 bits
		BackendTag   backend         :2 = BackendTag  ::Unknown ; //       2 bits
		bool         start_reported  :1 = false                 ; //       1 bit , if true <=> start message has been reported to user
		bool         speculative_deps:1 = false                 ; //       1 bit , if true <=> job is waiting for speculative deps only
		Bool3        speculate       :2 = Yes                   ; //       2 bits, Yes : prev dep not ready, Maybe : prev dep in error
	} ;
	static_assert(sizeof(JobReqInfo)==24) ;                                    // check expected size

}
#endif
#ifdef DATA_DEF
namespace Engine {

	ENUM_1( RunStatus
	,	Err = TargetErr                // >=Err means job is in error before even starting
	,	Complete                       // job was run
	,	NoDep                          // job was not run because of missing static dep
	,	NoFile                         // job was not run because it is a missing file in a source dir
	,	TargetErr                      // job was not run because of a manual static target
	,	DepErr                         // job was not run because of dep error
	,	RsrcsErr                       // job was not run because of resources could not be computed
	)

	struct JobData : DataBase {
		using Idx        = JobIdx        ;
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		// static data
	private :
		static ::shared_mutex              _s_target_dirs_mutex ;
		static ::umap<Node,NodeIdx/*cnt*/> _s_target_dirs       ;              // dirs created for job execution that must not be deleted
		static ::umap<Node,NodeIdx/*cnt*/> _s_hier_target_dirs  ;              // uphill hierarchy of _s_target_dirs
		// cxtors & casts
	public :
		JobData(                                  ) = default ;
		JobData( Name n                           ) : DataBase{n}                       {} // special Job, all deps
		JobData( Name n , Special sp , Deps ds={} ) : DataBase{n} , deps{ds} , rule{sp} {  // special Job, all deps
			SWEAR(sp!=Special::Unknown) ;
			exec_gen = NExecGen ;                                                          // special jobs are always exec_ok
		}
		JobData( Name n , Rule r , Deps sds ) : DataBase{n} , deps{sds} , rule{r} {        // plain Job, static deps
			SWEAR(!rule.is_shared()) ;
		}
		//
		JobData           (JobData&& jd) ;
		~JobData          (            ) ;
		JobData& operator=(JobData&& jd) ;
	private :
		JobData           (JobData const&) = default ;
		JobData& operator=(JobData const&) = default ;
		// accesses
	public :
		Job      idx () const { return Job::s_idx(*this)              ; }
		::string name() const { return full_name(rule->job_sfx_len()) ; }
		//
		bool active() const { return !rule.old() ; }
		//
		ReqInfo const& c_req_info  (Req                   ) const ;
		ReqInfo      & req_info    (Req                   ) const ;
		ReqInfo      & req_info    (ReqInfo const&        ) const ;                   // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs        (                      ) const ;
		::vector<Req>  running_reqs(bool with_zombies=true) const ;
		bool           running     (bool with_zombies=true) const ;                   // fast implementation of +running_reqs(...)
		//
		bool cmd_ok    (   ) const { return                      exec_gen >= rule->cmd_gen   ; }
		bool rsrcs_ok  (   ) const { return is_ok(status)!=No || exec_gen >= rule->rsrcs_gen ; } // dont care about rsrcs if job went ok
		bool is_special(   ) const { return rule->is_special() || idx().frozen()             ; }
		bool has_req   (Req) const ;
		//
		void exec_ok(bool ok) { SWEAR(!rule->is_special(),rule->special) ; exec_gen = ok ? rule->rsrcs_gen : 0 ; }
		//
		::pair<Delay,bool/*from_rule*/> best_exec_time() const {
			if (rule->is_special()) return { {}              , false } ;
			if (+exec_time        ) return {       exec_time , false } ;
			else                    return { rule->exec_time , true  } ;
		}
		//
		bool sure   () const ;
		void mk_sure()       { match_gen = Rule::s_match_gen ; _sure = true ; }
		bool err() const {
				if (run_status>=RunStatus::Err     ) return true               ;
				if (run_status!=RunStatus::Complete) return false              ;
				else                                 return is_ok(status)!=Yes ;
		}
		bool missing() const { return run_status<RunStatus::Err && run_status!=RunStatus::Complete ; }
		// services
		::pair<vmap<Node,FileAction>,vector<Node>/*warn_unlink*/> pre_actions( Rule::SimpleMatch const& , bool manual_ok=false , bool mark_target_dirs=false ) const ; // thread-safe
		//
		void     end_exec      (                               ) const ;       // thread-safe
		::string ancillary_file(AncillaryTag=AncillaryTag::Data) const ;
		::string special_stderr(Node                           ) const ;
		::string special_stderr(                               ) const ;       // cannot declare a default value for incomplete type Node
		//
		void              invalidate_old() ;
		Rule::SimpleMatch simple_match  () const ;                             // thread-safe
		::vector<Node>    targets       () const ;
		//
		void set_pressure( ReqInfo& , CoarseDelay ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ; // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			if ( speculate==No && ri.done() && err() ) audit_end("was_",ri) ;
			_propag_speculate(ri) ;
		}
		//
		JobReason make(
			ReqInfo&
		,	RunAction
		,	JobReason                          = {}
		,	Node               asking_         = {}
		,	Bool3              speculate       = Yes
		,	MakeAction                         = MakeAction::None
		,	CoarseDelay const* old_exec_time   = nullptr
		,	bool               wakeup_watchers = true
		) ;
		//
		void make( ReqInfo& ri , MakeAction ma ) { make(ri,RunAction::None,{}/*reason*/,{}/*asking*/,Yes/*speculate*/,ma) ; } // for wakeup
		//
		bool/*maybe_new_deps*/ submit( ReqInfo& , JobReason , CoarseDelay pressure ) ;
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		void add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) ;
		//
		void audit_end_special( Req , SpecialStep , Bool3 modified , Node ) const ; // modified=Maybe means file is new
		void audit_end_special( Req , SpecialStep , Bool3 modified        ) const ; // cannot use default Node={} as Node is incomplete
		//
		template<class... A> void audit_end(A&&... args) const ;
	private :
		void _propag_speculate(ReqInfo const&) const ;
		//
		::pair<SpecialStep,Bool3/*modified*/> _update_target( Node target , ::string const& target_name , bool star , Disk::NfsGuard& ) ;
		//
		bool/*maybe_new_deps*/ _submit_special  ( ReqInfo&                                    ) ;
		bool/*maybe_new_deps*/ _submit_plain    ( ReqInfo& , JobReason , CoarseDelay pressure ) ;
		void                   _set_pressure_raw( ReqInfo& ,             CoarseDelay          ) const ;
		// data
	public :
		//Name           name                     ;                                                         //     32 bits, inherited
		Node             asking                   ;                                                         //     32 bits,        last target needing this job
		Targets          star_targets             ;                                                         //     32 bits, owned, for plain jobs
		Deps             deps                     ;                                                         // 31<=32 bits, owned
		Rule             rule                     ;                                                         //     16 bits,        can be retrieved from full_name, but would be slower
		CoarseDelay      exec_time                ;                                                         //     16 bits,        for plain jobs
		ExecGen          exec_gen  :NExecGenBits  = 0                   ;                                   //   <= 8 bits,        for plain jobs, cmd generation of rule
		mutable MatchGen match_gen :NMatchGenBits = 0                   ;                                   //   <= 8 bits,        if <Rule::s_match_gen => deemed !sure
		Tokens1          tokens1                  = 0                   ;                                   //   <= 8 bits,        for plain jobs, number of tokens - 1 for eta computation
		RunStatus        run_status:3             = RunStatus::Complete ; static_assert(+RunStatus::N< 8) ; //      3 bits
		Status           status    :4             = Status   ::New      ; static_assert(+Status   ::N<16) ; //      4 bits
	private :
		mutable bool     _sure     :1             = false               ;                                   //      1 bit
	} ;
	static_assert(sizeof(JobData)==24) ;                                       // check expected size

	ENUM( MissingAudit
	,	No
	,	Steady
	,	Modified
	)

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// Job
	//

	inline Job::Job( RuleTgt rt , ::string const& t  , Req req , DepDepth lvl ) : Job{Rule::SimpleMatch(rt,t ),req,lvl} {}
	inline Job::Job( Rule    r  , ::string const& jn , Req req , DepDepth lvl ) : Job{Rule::SimpleMatch(r ,jn),req,lvl} {}
	//
	inline Job::Job( Special sp ,          Deps deps ) : Job{                                New , sp,deps } { SWEAR(sp==Special::Req  ) ; }
	inline Job::Job( Special sp , Node t , Deps deps ) : Job{ {t->name(),Rule(sp).job_sfx()},New , sp,deps } { SWEAR(sp!=Special::Plain) ; }

	inline bool Job::active() const { return +*this && (*this)->active() ; }

	//
	// JobTgt
	//

	inline JobTgt::JobTgt( RuleTgt rt , ::string const& t , Req r , DepDepth lvl ) : JobTgt{ Job(rt,t,r,lvl) , rt.sure() } {}

	inline bool JobTgt::sure() const { return is_sure() && (*this)->sure() ; }

	inline bool JobTgt::produces( Node t , bool sure ) const {
		if ((*this)->missing()          ) return false ;                       // missing jobs produce nothing
		if (is_sure()                   ) return true  ;
		if ((*this)->err()              ) return !sure ;                       // jobs in error are deemed to produce all their potential targets
		if (t->has_actual_job_tgt(*this)) return true  ;                       // fast path
		//
		return ::binary_search( (*this)->star_targets , t ) ;
	}

	//
	// JobData
	//

	inline JobData::JobData           (JobData&& jd) : JobData(jd) {                                    jd.star_targets.forget() ; jd.deps.forget() ;                }
	inline JobData::~JobData          (            ) {                                                     star_targets.pop   () ;    deps.pop   () ;                }
	inline JobData& JobData::operator=(JobData&& jd) { SWEAR(rule==jd.rule,rule,jd.rule) ; *this = jd ; jd.star_targets.forget() ; jd.deps.forget() ; return *this ; }

	inline ::string JobData::special_stderr   (                                 ) const { return special_stderr   (      {}) ; }
	inline void     JobData::audit_end_special( Req r , SpecialStep s , Bool3 m ) const { return audit_end_special(r,s,m,{}) ; }

	inline Rule::SimpleMatch JobData::simple_match() const { return Rule::SimpleMatch(idx()) ; }

	inline void JobData::invalidate_old() {
		if ( +rule && rule.old() ) idx().pop() ;
	}

	inline void JobData::add_watcher( ReqInfo& ri , Node watcher , Node::ReqInfo& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void JobData::set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                   // if pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri,pressure) ;
	}

	inline bool/*maybe_new_deps*/ JobData::submit( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		ri.force = JobReasonTag::None ;                                                                      // job is submitted, that was the goal, now void looping
		if (is_special()) return _submit_special(ri                ) ;
		else              return _submit_plain  (ri,reason,pressure) ;
	}

	template<class... A> inline void JobData::audit_end(A&&... args) const {
		JobExec(idx(),New,New).audit_end(::forward<A>(args)...) ;
	}

	inline bool JobData::sure() const {
		if (match_gen<Rule::s_match_gen) {
			_sure     = false             ;
			match_gen = Rule::s_match_gen ;
			for( Dep const& d : deps ) {
				if (!d.dflags[Dflag::Static]   ) continue    ;                 // we are only interested in static targets, other ones may not exist and do not prevent job from being built
				if (d->buildable<Buildable::Yes) goto Return ;
			}
			_sure = true ;
		}
	Return :
		return _sure ;
	}

	inline JobData::ReqInfo const& JobData::c_req_info(Req r) const {
		::umap<Job,ReqInfo> const& req_infos = Req::s_store[+r].jobs ;
		auto                       it        = req_infos.find(idx()) ;         // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].jobs.dflt ;
		else                     return it->second                 ;
	}
	inline JobData::ReqInfo& JobData::req_info(Req req) const {
		auto te = Req::s_store[+req].jobs.try_emplace(idx(),ReqInfo(req)) ;
		return te.first->second ;
	}
	inline JobData::ReqInfo& JobData::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].jobs.dflt) return req_info(cri.req)         ; // allocate
		else                                         return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> JobData::reqs() const { return Req::reqs(*this) ; }

	inline bool JobData::has_req(Req r) const {
		return Req::s_store[+r].jobs.contains(idx()) ;
	}

	//
	// JobReqInfo
	//

	inline void JobReqInfo::update( RunAction run_action , MakeAction make_action , JobData const& job ) {
		SWEAR(run_action!=RunAction::Dsk) ;                                                                // Dsk is only for Node's
		Bool3 ok = is_ok(job.status) ;
		if ( ok==Maybe && action>=RunAction::Status ) run_action = RunAction::Run ;
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
			SWEAR( make_action<MakeAction::End , make_action ) ;
		} else if (
			( req->zombie                              )                       // zombie's need not check anything
		||	( make_action==MakeAction::GiveUp          )                       // if not started, no further analysis
		||	( action==RunAction::Makable && job.sure() )                       // no need to check deps, they are guaranteed ok if sure
		) {
			done_ = done_ | action ;
		} else if (make_action==MakeAction::End) {
			lvl     = lvl & Lvl::Dep ;                                         // we just ran, reset analysis
			dep_lvl = 0              ;
			action  = run_action     ;                                         // we just ran, we are allowed to decrease action
		}
		if (done(action)) lvl = Lvl::Done ;
		SWEAR(lvl!=Lvl::End) ;
	}

}
#endif
