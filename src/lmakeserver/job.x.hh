// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#ifdef STRUCT_DECL

ENUM( AncillaryTag
,	Backend
,	Data
,	Dbg
,	KeepTmp
)

ENUM( JobMakeAction
,	Wakeup          // waited nodes are available
,	Makable         // job makability is available
,	Status          // target crcs are available
,	End             // job has completed
,	GiveUp          // job is abandonned, because of error or ^C
)

ENUM_3( JobStep       // must be in chronological order
,	MinCurStats =Dep
,	MaxCurStats1=Done
,	End         =Dep  // value to which step is set at end of execution to distinguish from an actively running job
,	None              // no analysis done yet (not in stats)
,	Dep               // analyzing deps
,	Queued            // waiting for execution
,	Exec              // executing
,	Done              // done execution (or impossible to execute)
,	Hit               // cache hit
)

ENUM( MissingAudit
,	No
,	Steady
,	Modified
)

ENUM( RunStatus
,	Ok
,	DepErr        // job cannot run because some deps are in error
,	MissingStatic // job cannot run because missing static dep
,	Err           // job cannot run because an error was seen before even starting
)

ENUM( SpecialStep // ordered by increasing importance
,	Idle
,	Ok
,	Err
,	Loop
)

namespace Engine {

	struct Job        ;
	struct JobExec    ;
	struct JobTgt     ;
	struct JobTgts    ;
	struct JobData    ;
	struct JobReqInfo ;

	static constexpr uint8_t JobNGuardBits = 2 ; // one to define JobTgt, the other to put it in a CrunchVector

}

#endif
#ifdef STRUCT_DEF

inline NodeGoal mk_goal(JobMakeAction ma) {
	static constexpr NodeGoal s_tab[] {
		NodeGoal::None                  // Wakeup
	,	NodeGoal::Makable               // Makable
	,	NodeGoal::Status                // Status
	,	NodeGoal::Status                // End
	,	NodeGoal::None                  // GiveUp
	} ;
	static_assert(sizeof(s_tab)==N<JobMakeAction>*sizeof(NodeGoal)) ;
	return s_tab[+ma] ;
}

namespace Engine {

	struct Job : JobBase {
		friend ::ostream& operator<<( ::ostream& , Job const ) ;
		using JobBase::side ;
		//
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		using Step       = JobStep       ;
		// cxtors & casts
		using JobBase::JobBase ;
		Job( Rule::SimpleMatch&&          , Req={} , DepDepth lvl=0 ) ; // plain Job, used internally and when repairing, req is only for error reporting
		Job( RuleTgt , ::string const& t  , Req={} , DepDepth lvl=0 ) ; // plain Job, match on target
		Job( Rule    , ::string const& jn , Req={} , DepDepth lvl=0 ) ; // plain Job, match on name, used for repairing or when required from command line
		//
		Job( Special ,               Deps deps               ) ;        // Job used to represent a Req
		Job( Special , Node target , Deps deps               ) ;        // special job
		Job( Special , Node target , ::vector<JobTgt> const& ) ;        // multi
		// accesses
		bool active() const ;
	} ;

	struct JobTgt : Job {
		static_assert(Job::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Job::NGuardBits-1       ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , JobTgt ) ;
		// cxtors & casts
		JobTgt(                                                              ) = default ;
		JobTgt( Job j , bool isp=false                                       ) : Job(j ) { if (+j) is_static_phony(isp)          ; } // if no job, ensure JobTgt appears as false
		JobTgt( RuleTgt rt , ::string const& t , Req req={} , DepDepth lvl=0 ) ;
		JobTgt( JobTgt const& jt                                             ) : Job(jt) { is_static_phony(jt.is_static_phony()) ; }
		//
		JobTgt& operator=(JobTgt const& jt) { Job::operator=(jt) ; is_static_phony(jt.is_static_phony()) ; return *this ; }
		//
		bool is_static_phony(        ) const { return Job::side<1>() ;                          }
		void is_static_phony(bool isp)       { { if (isp) SWEAR(+*this) ; } Job::side<1>(isp) ; }
		bool sure           (        ) const ;
		// services
		bool produces(Node) const ;
	} ;

	struct JobTgts : JobTgtsBase {
		friend ::ostream& operator<<( ::ostream& , JobTgts ) ;
		// cxtors & casts
		using JobTgtsBase::JobTgtsBase ;
	} ;

	struct JobExec : Job {
		friend ::ostream& operator<<( ::ostream& , JobExec const& ) ;
		// cxtors & casts
		JobExec( Job j={} ,               SigDate s={} , SigDate e={} ) : Job{j} ,           start_date{s} , end_date{e} {}
		JobExec( Job j    , in_addr_t h , SigDate s={} , SigDate e={} ) : Job{j} , host{h} , start_date{s} , end_date{e} {}
		JobExec( Job j    ,               SigDate s    , NewType      ) : Job{j} ,           start_date{s} , end_date{s} {} // end=New means job is instantaneous
		JobExec( Job j    , in_addr_t h , SigDate s    , NewType      ) : Job{j} , host{h} , start_date{s} , end_date{s} {} // .
		// services
		// called in main thread after start
		// /!\ clang does not support default initilization of report_unlks here, so we have to provide a 2nd version of report_start and started
		bool/*reported*/ report_start( ReqInfo&    , ::vector<Node> const& report_unlnks , ::string const& stderr={} , ::string const& backend_msg={} ) const ;
		bool/*reported*/ report_start( ReqInfo&                                                                                                       ) const ;
		void             report_start(                                                                                                                ) const ;
		void             started     ( bool report , ::vector<Node> const& report_unlnks , ::string const& stderr={} , ::string const& backecn_msg={} ) ;
		void             started     ( bool report                                                                                                    ) ;
		//
		void live_out( ReqInfo& , ::string const& ) const ;
		void live_out(            ::string const& ) const ;
		//
		JobMngtRpcReply  job_info( JobMngtProc , ::vector<Dep> const& deps                            ) const ; // answer to requests from job execution
		bool/*modified*/ end     ( ::vmap_ss const& rsrcs , JobDigest const& , ::string&& backend_msg ) ;       // hit indicates that result is from a cache hit
		void             give_up ( Req={} , bool report=true                                          ) ;       // Req (all if 0) was killed and job was not killed (not started or continue)
		//
		// audit_end returns the report to do if job is finally not rerun
		JobReport audit_end( ::string const& pfx , ReqInfo&    , ::string const& msg , ::string const& stderr    , size_t max_stderr_len=-1 , bool modified=true , Delay exec_time={} ) const ;
		JobReport audit_end( ::string const& pfx , ReqInfo& ri ,                       ::string const& stderr={} , size_t max_stderr_len=-1 , bool modified=true , Delay exec_time={} ) const {
			return audit_end(pfx,ri,{}/*msg*/,stderr,max_stderr_len,modified,exec_time) ;
		}
		// data
		in_addr_t host       = NoSockAddr ;
		SigDate   start_date ;
		SigDate   end_date   ;                                                                                  // if no end_date, job is stil on going
	} ;

}

#endif
#ifdef INFO_DEF

namespace Engine {

	struct JobReqInfo : ReqInfo {                                        // watchers of Job's are Node's
		friend ::ostream& operator<<( ::ostream& , JobReqInfo const& ) ;
		using Step       = JobStep       ;
		using MakeAction = JobMakeAction ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// accesses
		bool running() const {
			switch (step()) {
				case Step::Queued :
				case Step::Exec   : return true  ;
				default           : return false ;
			}
		}
		bool done(bool is_full=false) const { return step()>=Step::Done && (!is_full||full) ; }
		//
		Step step(      ) const { return _step ; }
		void step(Step s) ;
		// services
		void reset() {
			if (step()>Step::Dep) step(Step::Dep) ;
			iter  = {}      ;
			state = State() ;
		}
		void add_watcher( Node watcher , NodeReqInfo& watcher_req_info ) {
			ReqInfo::add_watcher(watcher,watcher_req_info) ;
		}
		void chk() const {
			switch (step()) {
				case Step::None   : SWEAR(n_wait==0) ; break ;           // not started yet, cannot wait anything
				case Step::Dep    : SWEAR(n_wait> 0) ; break ;           // we must be waiting something if analysing Dep
				case Step::Queued :                                      // if running, we are waiting for job execution
				case Step::Exec   : SWEAR(n_wait==1) ; break ;           // .
				case Step::Done   :                                      // done, cannot wait anything anymore
				case Step::Hit    : SWEAR(n_wait==0) ; break ;
			DF}
		}
		// data
		struct State {
			friend ::ostream& operator<<( ::ostream& , State const& ) ;
			JobReason reason          = {}    ;                          //  36  <= 64 bits, reason to run job when deps are ready, due to dep analysis
			bool      missing_dsk  :1 = false ;                          //          1 bit , if true <=>, a dep has been checked but not on disk and analysis must be redone if job has to run
			RunStatus stamped_err  :2 = {}    ;                          //          2 bits, errors seen in dep until iter before    last parallel chunk, Maybe means missing static
			RunStatus proto_err    :2 = {}    ;                          //          2 bits, errors seen in dep until iter including last parallel chunk, Maybe means missing static
			bool      stamped_modif:1 = false ;                          //          1 bit , modifs seen in dep until iter before    last parallel chunk
			bool      proto_modif  :1 = false ;                          //          1 bit , modifs seen in dep until iter including last parallel chunk
		} ;
		DepsIter::Digest iter               ;                            // ~20+6<= 64 bits, deps up to this one statisfy required action
		State            state              ;                            //  43  <= 96 bits, dep analysis state
		JobReason        reason             ;                            //  36  <= 64 bits, reason to run job when deps are ready, forced (before deps) or asked by caller (after deps)
		uint8_t          n_submits          = 0     ;                    //          8 bits, number of times job has been submitted to avoid infinite loop
		bool             force           :1 = false ;                    //          1 bit , if true <=> job must run because reason
		bool             full            :1 = false ;                    //          1 bit , if true <=>, job result is asked, else only makable
		bool             start_reported  :1 = false ;                    //          1 bit , if true <=> start message has been reported to user
		bool             speculative_wait:1 = false ;                    //          1 bit , if true <=> job is waiting for speculative deps only
		Bool3            speculate       :2 = Yes   ;                    //          2 bits, Yes : prev dep not ready, Maybe : prev dep in error (percolated)
		bool             reported        :1 = false ;                    //          1 bit , used for delayed report when speculating
		BackendTag       backend         :2 = {}    ;                    //          2 bits
	private :
		Step _step:3 = {} ;                                              //          3 bits
	} ;
	static_assert(sizeof(JobReqInfo)==48) ;                              // check expected size, XXX : optimize size, can be 32

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct JobData : DataBase {
		using Idx        = JobIdx        ;
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		// static data
	private :
		static Mutex<MutexLvl::TargetDir>  _s_target_dirs_mutex ;
		static ::umap<Node,NodeIdx/*cnt*/> _s_target_dirs       ; // dirs created for job execution that must not be deleted
		static ::umap<Node,NodeIdx/*cnt*/> _s_hier_target_dirs  ; // uphill hierarchy of _s_target_dirs
		// cxtors & casts
	public :
		JobData(                                  ) = default ;
		JobData( Name n                           ) : DataBase{n}                                            {}
		JobData( Name n , Special sp , Deps ds={} ) : DataBase{n} , deps{ds} , rule{sp} , exec_gen{NExecGen} {}                                // special Job, all deps, always exec_ok
		JobData( Name n , Rule::SimpleMatch const& m , Deps sds ) : DataBase{n} , deps{sds} , rule{m.rule} {                                   // plain Job, static targets and deps
			SWEAR(!rule.is_shared()) ;
			_reset_targets(m) ;
		}
		//
		JobData           (JobData&& jd) ;
		~JobData          (            ) ;
		JobData& operator=(JobData&& jd) ;
	private :
		JobData           (JobData const&) = default ;
		JobData& operator=(JobData const&) = default ;
		void _reset_targets(Rule::SimpleMatch const&) ;
		void _reset_targets(                        ) { _reset_targets(simple_match()) ; }
		// accesses
	public :
		Job      idx () const { return Job::s_idx(*this)              ; }
		::string name() const { return full_name(rule->job_sfx_len()) ; }
		//
		bool active() const { return !rule.old() ; }
		//
		ReqInfo const& c_req_info  (Req                   ) const ;
		ReqInfo      & req_info    (Req                   ) const ;
		ReqInfo      & req_info    (ReqInfo const&        ) const ;                                                                            // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs        (                      ) const ;
		::vector<Req>  running_reqs(bool with_zombies=true) const ;
		bool           running     (bool with_zombies=true) const ;                                                                            // fast implementation of +running_reqs(...)
		//
		bool cmd_ok    (   ) const { return                      exec_gen >= rule->cmd_gen   ; }
		bool rsrcs_ok  (   ) const { return is_ok(status)!=No || exec_gen >= rule->rsrcs_gen ; }                                               // dont care about rsrcs if job went ok
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
			switch (run_status) {
				case RunStatus::Ok            : return is_ok(status)!=Yes ;
				case RunStatus::DepErr        : return true               ;
				case RunStatus::MissingStatic : return false              ;
				case RunStatus::Err           : return true               ;
			DF}
		}
		bool missing() const { return run_status==RunStatus::MissingStatic ; }
		// services
		::pair<vmap<Node,FileAction>,vector<Node>/*warn_unlnk*/> pre_actions( Rule::SimpleMatch const& , bool mark_target_dirs=false ) const ; // thread-safe
		//
		Tflags tflags(Node target) const ;
		//
		void     end_exec      (                               ) const ;            // thread-safe
		::string ancillary_file(AncillaryTag=AncillaryTag::Data) const ;
		JobInfo  job_info      (                               ) const { return  {                ancillary_file() } ; }
		void     write_job_info(JobInfo const& ji              ) const { ji.write(Disk::dir_guard(ancillary_file())) ; }
		::string special_stderr(Node                           ) const ;
		::string special_stderr(                               ) const ;            // cannot declare a default value for incomplete type Node
		//
		void              invalidate_old() ;
		Rule::SimpleMatch simple_match  () const ;                                  // thread-safe
		//
		void set_pressure( ReqInfo& , CoarseDelay ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ;     // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			if ( speculate==No && ri.reported && ri.done() && err() ) audit_end("was_",ri) ;
			_propag_speculate(ri) ;
		}
		//
		JobReason/*err*/ make( ReqInfo& , MakeAction , JobReason={} , Bool3 speculate=Yes , CoarseDelay const* old_exec_time=nullptr , bool wakeup_watchers=true ) ;
		//
		void wakeup(ReqInfo& ri) { make(ri,MakeAction::Wakeup) ; }
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
		bool/*maybe_new_deps*/ _submit_special  ( ReqInfo&                                    ) ;
		bool/*maybe_new_deps*/ _submit_plain    ( ReqInfo& , JobReason , CoarseDelay pressure ) ;
		void                   _set_pressure_raw( ReqInfo& ,             CoarseDelay          ) const ;
		// data
		// START_OF_VERSIONING
	public :
		//Name           name                     ;                                 //     32 bits, inherited
		Node             asking                   ;                                 //     32 bits,        last target needing this job
		Targets          targets                  ;                                 //     32 bits, owned, for plain jobs
		Deps             deps                     ;                                 // 31<=32 bits, owned
		Rule             rule                     ;                                 //     16 bits,        can be retrieved from full_name, but would be slower
		CoarseDelay      exec_time                ;                                 //     16 bits,        for plain jobs
		ExecGen          exec_gen  :NExecGenBits  = 0     ;                         //      8 bits,        for plain jobs, cmd generation of rule
		mutable MatchGen match_gen :NMatchGenBits = 0     ;                         //      8 bits,        if <Rule::s_match_gen => deemed !sure
		Tokens1          tokens1                  = 0     ;                         //      8 bits,        for plain jobs, number of tokens - 1 for eta computation
		RunStatus        run_status:3             = {}    ;                         //      3 bits
		Status           status    :4             = {}    ;                         //      4 bits
	private :
		mutable bool     _sure     :1             = false ;                         //      1 bit
		// END_OF_VERSIONING
	} ;
	static_assert(sizeof(JobData)==24) ;                                            // check expected size

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
	// JobExec
	//

	inline bool/*reported*/ JobExec::report_start(ReqInfo& ri) const { return report_start(ri,{}) ; }
	inline void             JobExec::started     (bool     r )       {        started     (r ,{}) ; }

	//
	// JobTgt
	//

	inline JobTgt::JobTgt( RuleTgt rt , ::string const& t , Req r , DepDepth lvl ) : JobTgt{ Job(rt,t,r,lvl) , rt.sure() } {}

	inline bool JobTgt::sure() const { return is_static_phony() && (*this)->sure() ; }

	inline bool JobTgt::produces(Node t) const {
		if ((*this)->missing()      ) return false                             ; // missing jobs produce nothing
		if ((*this)->err()          ) return true                              ; // jobs in error are deemed to produce all their potential targets
		if (sure()                  ) return true                              ;
		if (t->has_actual_job(*this)) return t->actual_tflags()[Tflag::Target] ; // .
		//
		auto it = ::lower_bound( (*this)->targets , {t,{}} ) ;
		return it!=(*this)->targets.end() && *it==t && it->tflags[Tflag::Target] ;
	}

	//
	// JobData
	//

	inline JobData::JobData           (JobData&& jd) : JobData(jd) {                                    jd.targets.forget() ; jd.deps.forget() ;                }
	inline JobData::~JobData          (            ) {                                                     targets.pop   () ;    deps.pop   () ;                }
	inline JobData& JobData::operator=(JobData&& jd) { SWEAR(rule==jd.rule,rule,jd.rule) ; *this = jd ; jd.targets.forget() ; jd.deps.forget() ; return *this ; }

	inline ::string JobData::special_stderr   (                                 ) const { return special_stderr   (      {}) ; }
	inline void     JobData::audit_end_special( Req r , SpecialStep s , Bool3 m ) const { return audit_end_special(r,s,m,{}) ; }

	inline Tflags JobData::tflags(Node target) const {
		Target t = *::lower_bound( targets , {target,{}} ) ;
		SWEAR(t==target) ;
		return t.tflags ;
	}

	inline Rule::SimpleMatch JobData::simple_match() const { return Rule::SimpleMatch(idx()) ; }

	inline void JobData::invalidate_old() {
		if ( +rule && rule.old() ) idx().pop() ;
	}

	inline void JobData::add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void JobData::set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                   // if pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri,pressure) ;
	}

	template<class... A> void JobData::audit_end(A&&... args) const {
		JobExec(idx(),New,New).audit_end(::forward<A>(args)...) ;
	}

	inline bool JobData::sure() const {
		if (match_gen<Rule::s_match_gen) {
			_sure     = false             ;
			match_gen = Rule::s_match_gen ;
			for( Dep const& d : deps ) {
				if (!d.dflags[Dflag::Static]   ) continue    ; // we are only interested in static targets, other ones may not exist and do not prevent job from being built
				if (d->buildable<Buildable::Yes) goto Return ;
			}
			_sure = true ;
		}
	Return :
		return _sure ;
	}

	inline JobReqInfo const& JobData::c_req_info(Req r) const {
		::umap<Job,ReqInfo> const& req_infos = Req::s_store[+r].jobs ;
		auto                       it        = req_infos.find(idx()) ;                  // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].jobs.dflt ;
		else                     return it->second                 ;
	}
	inline JobReqInfo& JobData::req_info(Req req) const {
		auto te = Req::s_store[+req].jobs.try_emplace(idx(),ReqInfo(req)) ;
		return te.first->second ;
	}
	inline JobReqInfo& JobData::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].jobs.dflt) return req_info(cri.req)         ; // allocate
		else                                         return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> JobData::reqs() const { return Req::s_reqs(*this) ; }

	inline bool JobData::has_req(Req r) const {
		return Req::s_store[+r].jobs.contains(idx()) ;
	}

	//
	// JobReqInfo
	//

	inline void JobReqInfo::step(Step s) {
		if ( _step==s                                             ) return ;                  // fast path
		if ( _step>=Step::MinCurStats && _step<Step::MaxCurStats1 ) req->stats.cur(_step)-- ;
		if ( s    >=Step::MinCurStats && s    <Step::MaxCurStats1 ) req->stats.cur(s    )++ ;
		_step = s ;
	}

}

#endif
