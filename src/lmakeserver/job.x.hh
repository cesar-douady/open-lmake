// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
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
,	Status          // target crcs are available
,	End             // job has completed
,	GiveUp          // job is abandonned, because of error or ^C
,	Query           // used for dep analysis : query only, no action is intiated (for DepsVerbose and ChkDeps)
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

}

#endif
#ifdef STRUCT_DEF

namespace Engine {

	struct JobInfo1 : ::variant< Void/*None*/ , JobInfoStart/*Start*/ , JobEndRpcReq/*End*/ , ::vector<Crc>/*DepCrcs*/ > {
		using Kind = JobInfoKind ;
		// cxtors & casts
		using ::variant< Void , JobInfoStart, JobEndRpcReq , ::vector<Crc> >::variant ; // necessary for clang++-14
		// accesses
		/**/             Kind kind() const { return Kind(index()) ; }
		template<Kind K> bool is_a() const { return index()==+K   ; }
		//
		JobInfoStart  const& start   () const { return ::get<JobInfoStart >(self) ; }
		JobInfoStart       & start   ()       { return ::get<JobInfoStart >(self) ; }
		JobEndRpcReq  const& end     () const { return ::get<JobEndRpcReq >(self) ; }
		JobEndRpcReq       & end     ()       { return ::get<JobEndRpcReq >(self) ; }
		::vector<Crc> const& dep_crcs() const { return ::get<::vector<Crc>>(self) ; }
		::vector<Crc>      & dep_crcs()       { return ::get<::vector<Crc>>(self) ; }
	} ;

	struct Job : JobBase {
		friend ::string& operator+=( ::string& , Job const ) ;
		friend struct JobData ;
		//
		using JobBase::side ;
		//
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		using Step       = JobStep       ;
		// statics
		static void s_init() {
			s_record_thread.open('J',
				[](::pair<Job,JobInfo1> const& jji)->void {
					Trace trace("s_record_thread",jji.first,jji.second.kind()) ;
					jji.first.record(jji.second) ;
				}
			) ;
		}
		// static data
		static QueueThread<::pair<Job,JobInfo1>,true/*Flush*/,true/*QueueAccess*/> s_record_thread ;
		// cxtors & casts
		using JobBase::JobBase ;
		Job( Rule::RuleMatch&&                                 , Req={} , DepDepth lvl=0 ) ; // plain Job, used internally and when repairing, req is only for error reporting
		Job( RuleTgt , ::string const& t  , Bool3 chk_psfx=Yes , Req={} , DepDepth lvl=0 ) ; // plain Job, chk_psfx=Maybe means check size only, match on target
		Job( Rule    , ::string const& jn , Bool3 chk_psfx=Yes , Req={} , DepDepth lvl=0 ) ; // plain Job, chk_psfx=Maybe means check size only, match on name, for repairing or job in command line
		//
		Job( Special ,               Deps deps               ) ;                             // Job used to represent a Req
		Job( Special , Node target , Deps deps               ) ;                             // special job
		Job( Special , Node target , ::vector<JobTgt> const& ) ;                             // multi
		//
		explicit operator ::string() const ;
		// accesses
		::string ancillary_file(AncillaryTag tag=AncillaryTag::Data) const ;
		// services
		JobInfo job_info(BitMap<JobInfoKind> need=~BitMap<JobInfoKind>()) const ;            // read job info from ancillary file, taking care of queued events
		//
		void record(JobInfo1 const&) const ;
		void record(JobInfo  const&) const ;
	} ;

	struct JobTgt : Job {
		static_assert(Job::NGuardBits>=1) ;                                                                                                               // need 1 bit to store is_static_phony bit
		static constexpr uint8_t NGuardBits = Job::NGuardBits-1       ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::string& operator+=( ::string& , JobTgt ) ;
		// cxtors & casts
		JobTgt(                                                                                   ) = default ;
		JobTgt( Job j , bool isp=false                                                            ) : Job(j ) { if (+j) is_static_phony(isp)          ; } // if no job, ensure JobTgt appears as false
		JobTgt( RuleTgt rt , ::string const& t , Bool3 chk_psfx=Yes , Req req={} , DepDepth lvl=0 ) ;                                                     // chk_psfx=Maybe means check size only
		JobTgt( JobTgt const& jt                                                                  ) : Job(jt) { is_static_phony(jt.is_static_phony()) ; }
		//
		JobTgt& operator=(JobTgt const& jt) { Job::operator=(jt) ; is_static_phony(jt.is_static_phony()) ; return self ; }
		//
		bool is_static_phony(        ) const { return Job::side<1>() ;                         }
		void is_static_phony(bool isp)       { { if (isp) SWEAR(+self) ; } Job::side<1>(isp) ; }
		bool sure           (        ) const ;
		// services
		bool produces( Node , bool actual=false ) const ; // if actual, return if node was actually produced, in addition to being officially produced
	} ;

	struct JobTgts : JobTgtsBase {
		friend ::string& operator+=( ::string& , JobTgts ) ;
		// cxtors & casts
		using JobTgtsBase::JobTgtsBase ;
	} ;

	struct JobExec : Job {
		friend ::string& operator+=( ::string& , JobExec const& ) ;
		// cxtors & casts
	public :
		JobExec(                                         ) = default ;
		JobExec( Job j ,               Pdate s           ) : Job{j} ,           start_date{s} , end_date{s} {}
		JobExec( Job j , in_addr_t h , Pdate s           ) : Job{j} , host{h} , start_date{s} , end_date{s} {}
		JobExec( Job j ,               Pdate s , Pdate e ) : Job{j} ,           start_date{s} , end_date{e} {}
		JobExec( Job j , in_addr_t h , Pdate s , Pdate e ) : Job{j} , host{h} , start_date{s} , end_date{e} {}
		// services
		// called in main thread after start
		// /!\ clang does not support default initilization of report_unlks here, so we have to provide a 2nd version of report_start and started
		bool/*reported*/ report_start( ReqInfo&    , ::vmap<Node,FileActionTag> const& report_unlnks , MsgStderr const& ={} ) const ; // txts is {backend_msg,stderr}
		bool/*reported*/ report_start( ReqInfo& ri                                                                          ) const ;
		void             report_start(                                                                                      ) const ;
		void             started     ( bool report , ::vmap<Node,FileActionTag> const& report_unlnks , MsgStderr const& ={} ) ;       // txts is {backend_msg,stderr}
		//
		void live_out    ( ReqInfo& , ::string const& ) const ;
		void live_out    (            ::string const& ) const ;
		void add_live_out(            ::string const& ) const ;
		//
		JobMngtRpcReply  job_analysis( JobMngtProc , ::vector<Dep> const& deps ) const ; // answer to requests from job execution
		void             end         ( JobDigest<Node>&&                       ) ;
		void             give_up     ( Req={} , bool report=true               ) ;       // Req (all if 0) was killed and job was not killed (not started or continue)
		//
		// audit_end returns the report to do if job is finally not rerun
		JobReport audit_end( ReqInfo&    , bool with_stats , ::string const& pfx , MsgStderr const&           , uint16_t max_stderr_len=0 , Delay exec_time={} , bool retry=false ) const ;
		JobReport audit_end( ReqInfo& ri , bool with_stats , ::string const& pfx , ::string const& stderr={}  , uint16_t max_stderr_len=0 , Delay exec_time={} , bool retry=false ) const {
			return audit_end( ri , with_stats , pfx , MsgStderr{.stderr=stderr} , max_stderr_len , exec_time , retry ) ;
		}
		// data
		in_addr_t   host       = 0 ;
		CoarseDelay cost       ;                                                         // exec time / average number of running job during execution
		Tokens1     tokens1    = 0 ;
		Pdate       start_date ;
		Pdate       end_date   ;                                                         // if no end_date, job is stil on going
	} ;

}

#endif
#ifdef INFO_DEF

namespace Engine {

	struct JobReqInfo : ReqInfo {                                      // watchers of Job's are Node's
		friend ::string& operator+=( ::string& , JobReqInfo const& ) ;
		using Step       = JobStep       ;
		using MakeAction = JobMakeAction ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// accesses
		bool running(bool hit_ok=false) const {
			switch (step()) {
				case Step::Queued :
				case Step::Exec   : return true   ;
				case Step::Hit    : return hit_ok ;
				default           : return false  ;
			}
		}
		bool done() const { return step()>=Step::Done ; }
		//
		Step step(          ) const { return _step ; }
		void step(Step s,Job) ;
		// services
		void reset( Job j , bool has_run=false ) {
			if (has_run) {
				force  = false ;                                       // cmd has been executed, it is not new any more
				reason = {}    ;                                       // reasons were to trigger the ending job, there are none now
			}
			if (step()>Step::Dep) step(Step::Dep,j) ;
			iter  = {} ;
			state = {} ;
		}
		void add_watcher( Node watcher , NodeReqInfo& watcher_req_info ) {
			ReqInfo::add_watcher(watcher,watcher_req_info) ;
		}
		void chk() const {
			switch (step()) {
				case Step::None   : SWEAR(n_wait==0) ; break ;         // not started yet, cannot wait anything
				case Step::Dep    : SWEAR(n_wait> 0) ; break ;         // we must be waiting something if analysing Dep
				case Step::Queued :                                    // if running, we are waiting for job execution
				case Step::Exec   : SWEAR(n_wait==1) ; break ;         // .
				case Step::Done   :                                    // done, cannot wait anything anymore
				case Step::Hit    : SWEAR(n_wait==0) ; break ;
			DF}
		}
		// data
		struct State {
			friend ::string& operator+=( ::string& , State const& ) ;
			// cxtors & casts
			State() = default ;
			// data
			JobReason reason          = {}    ;                        //  36  <= 64 bits, reason to run job when deps are ready, due to dep analysis
			bool      missing_dsk  :1 = false ;                        //          1 bit , if true <=>, a dep has been checked but not on disk and analysis must be redone if job has to run
			RunStatus stamped_err  :2 = {}    ;                        //          2 bits, errors seen in dep until iter before    last parallel chunk
			RunStatus proto_err    :2 = {}    ;                        //          2 bits, errors seen in dep until iter including last parallel chunk
			bool      stamped_modif:1 = false ;                        //          1 bit , modifs seen in dep until iter before    last parallel chunk
			bool      proto_modif  :1 = false ;                        //          1 bit , modifs seen in dep until iter including last parallel chunk
		} ;
//		ReqInfo                                                        //        128 bits, inherits
		State            state                ;                        //  43  <= 96 bits, dep analysis state
		DepsIter::Digest iter                 ;                        // ~20+6<= 64 bits, deps up to this one statisfy required action
		JobReason        reason               ;                        //  36  <= 64 bits, reason to run job when deps are ready, forced (before deps) or asked by caller (after deps)
		uint16_t         n_submits            = 0     ;                //         16 bits, number of times job has been rerun
		uint8_t          n_losts              = 0     ;                //          8 bits, number of times job has been lost
		uint8_t          n_retries            = 0     ;                //          8 bits, number of times job has been seen in error
		bool             force             :1 = false ;                //          1 bit , if true <=> job must run because reason
		bool             start_reported    :1 = false ;                //          1 bit , if true <=> start message has been reported to user
		bool             speculative_wait  :1 = false ;                //          1 bit , if true <=> job is waiting for speculative deps only
		Bool3            speculate         :2 = Yes   ;                //          2 bits, Yes : prev dep not ready, Maybe : prev dep in error (percolated)
		bool             reported          :1 = false ;                //          1 bit , used for delayed report when speculating
		bool             modified          :1 = false ;                //          1 bit , modified when last run
		bool             modified_speculate:1 = false ;                //          1 bit , modified when marked speculative
		bool             miss_live_out     :1 = false ;                //          1 bit , live_out info has not been sent to user
	private :
		Step _step:3 = {} ;                                            //          3 bits
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct JobData : JobNodeData {
		using Idx        = JobIdx        ;
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		// static data
	private :
		static Mutex<MutexLvl::TargetDir> _s_target_dirs_mutex ;
		static ::umap<Node,Idx/*cnt*/>    _s_target_dirs       ;                                                         // dirs created for job execution that must not be deleted
		static ::umap<Node,Idx/*cnt*/>    _s_hier_target_dirs  ;                                                         // uphill hierarchy of _s_target_dirs
		// cxtors & casts
	public :
		JobData(                                  ) = default ;
		JobData( Name n                           ) : JobNodeData{n}                                      {}
		JobData( Name n , Special sp , Deps ds={} ) : JobNodeData{n} , deps{ds} , rule_crc{Rule(sp)->crc} {}             // special Job, all deps
		//
		JobData( Name n , Rule::RuleMatch const& m , Deps sds ) : JobNodeData{n} , deps{sds} , rule_crc{m.rule->crc} {   // plain Job, static targets and deps
			SWEAR(!m.rule.is_shared()) ;
			_reset_targets(m) ;
		}
		//
		JobData           (JobData&& jd) ;
		~JobData          (            ) ;
		JobData& operator=(JobData&& jd) ;
	private :
		JobData           (JobData const&) = default ;
		JobData& operator=(JobData const&) = default ;
		void _reset_targets(Rule::RuleMatch const&) ;
		void _reset_targets(                      ) { _reset_targets(rule_match()) ; }
		// accesses
	public :
		Job      idx () const { return Job::s_idx(self) ; }
		Rule     rule() const { return rule_crc->rule   ; }
		::string name() const {
			::string res ;
			if ( Rule r=rule() ; +r )   res = full_name(r->job_sfx_len()) ;
			else                      { res = full_name(                ) ; res.resize(res.find(RuleData::JobMrkr)) ; }  // heavier, but works without rule
			return res ;
		}
		::string unique_name() const ;
		//
		ReqInfo const& c_req_info  ( Req                                        ) const ;
		ReqInfo      & req_info    ( Req                                        ) const ;
		ReqInfo      & req_info    ( ReqInfo const&                             ) const ;                                // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs        (                                            ) const ;
		::vector<Req>  running_reqs( bool with_zombies=true , bool hit_ok=false ) const ;
		bool           running     ( bool with_zombies=true , bool hit_ok=false ) const ;                                // fast implementation of +running_reqs(...)
		//
		bool cmd_ok    (   ) const { return                      rule_crc->state<=RuleCrcState::CmdOk ; }
		bool rsrcs_ok  (   ) const { return is_ok(status)!=No || rule_crc->state==RuleCrcState::Ok    ; }                // dont care about rsrcs if job went ok
		bool is_special(   ) const { return rule()->is_special() || idx().frozen()                    ; }
		bool has_req   (Req) const ;
		//
		void set_exec_ok() { Rule r = rule() ; SWEAR(!r->is_special(),r->special) ; rule_crc = r->crc ; }                // set official rule_crc (i.e. with the right cmd and rsrcs crc's)
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
		vmap<Node,FileAction> pre_actions( Rule::RuleMatch const& , bool mark_target_dirs=false ) const ;                // thread-safe
		//
		Tflags tflags(Node target) const ;
		//
		void      end_exec          (                                     ) const ;                                      // thread-safe
		::string  ancillary_file    ( AncillaryTag tag=AncillaryTag::Data ) const { return idx().ancillary_file(tag) ; }
		MsgStderr special_msg_stderr( Node , bool short_msg=false         ) const ;
		MsgStderr special_msg_stderr(        bool short_msg=false         ) const ;                                      // cannot declare a default value for incomplete type Node
		//
		Rule::RuleMatch rule_match    (                                              ) const ;                           // thread-safe
		void            estimate_stats(                                              ) ;                                 // may be called any time
		void            estimate_stats(                                      Tokens1 ) ;                                 // must not be called during job execution as cost must stay stable
		void            record_stats  ( Delay exec_time , CoarseDelay cost , Tokens1 ) ;                                 // .
		//
		void set_pressure( ReqInfo& , CoarseDelay ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ;                                          // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			if ( speculate==No && ri.reported && ri.done() ) {
				if      (err()                ) { audit_end(ri,false/*with_stats*/,"was_") ; req->stats.move( JobReport::Speculative , JobReport::Failed , exec_time ) ; }
				else if (ri.modified_speculate)                                              req->stats.move( JobReport::Speculative , JobReport::Done   , exec_time ) ;
				else                                                                         req->stats.move( JobReport::Speculative , JobReport::Steady , exec_time ) ;
			}
			_propag_speculate(ri) ;
		}
		//
		JobReason/*err*/ make( ReqInfo& , MakeAction , JobReason={} , Bool3 speculate=Yes , bool wakeup_watchers=true ) ;
		//
		void wakeup(ReqInfo& ri) { make(ri,MakeAction::Wakeup) ; }
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		void add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) ;
		//
		void audit_end_special( Req , SpecialStep , Bool3 modified , Node ) const ;                                      // modified=Maybe means file is new
		void audit_end_special( Req , SpecialStep , Bool3 modified        ) const ;                                      // cannot use default Node={} as Node is incomplete
		//
		template<class... A> void audit_end(A&&... args) const ;
	private :
		void _propag_speculate(ReqInfo const&) const ;
		//
		void                   _submit_special ( ReqInfo&                        ) ;                                     // special never report new deps
		bool/*maybe_new_deps*/ _submit_plain   ( ReqInfo& , CoarseDelay pressure ) ;
		void                   _do_set_pressure( ReqInfo& , CoarseDelay          ) const ;
		// data
		// START_OF_VERSIONING
	public :
		//Name           name         ;              //     32 bits, inherited
		Node             asking       ;              //     32 bits,        last target needing this job
		Targets          targets      ;              //     32 bits, owned, for plain jobs
		Deps             deps         ;              // 31<=32 bits, owned
		RuleCrc          rule_crc     ;              //     32 bits
		CoarseDelay      exec_time    ;              //     16 bits,        for plain jobs
		CoarseDelay      cost         ;              //     16 bits,        exec_time / average number of parallel jobs during execution, /!\ must be stable during job execution
		Tokens1          tokens1      = 0  ;         //      8 bits,        for plain jobs, number of tokens - 1 for eta estimation
		mutable MatchGen match_gen    = 0  ;         //      8 bits,        if <Rule::s_match_gen => deemed !sure
		RunStatus        run_status:3 = {} ;         //      3 bits
		Status           status    :4 = {} ;         //      4 bits
		BackendTag       backend   :2 = {} ;         //      2 bits         backend asked for last execution
	private :
		mutable bool     _sure          :1 = false ; //      1 bit
		Bool3            _reliable_stats:2 = No    ; //      2 bits,        if No <=> no known info, if Maybe <=> guestimate only, if Yes <=> recorded info
		// END_OF_VERSIONING
	} ;

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// Job
	//

	inline Job::operator ::string() const { return self->name() ; }

	inline Job::Job( RuleTgt rt , ::string const& t  , Bool3 chk_psfx , Req req , DepDepth lvl ) : Job{Rule::RuleMatch(rt,t ,chk_psfx),req,lvl} {} // chk_psfx=Maybe means check size only
	inline Job::Job( Rule    r  , ::string const& jn , Bool3 chk_psfx , Req req , DepDepth lvl ) : Job{Rule::RuleMatch(r ,jn,chk_psfx),req,lvl} {} // .
	//
	inline Job::Job( Special sp ,          Deps deps ) : Job{                                 New , sp,deps } { SWEAR(sp==Special::Req  ) ; }
	inline Job::Job( Special sp , Node t , Deps deps ) : Job{ {t->name(),Rule(sp)->job_sfx()},New , sp,deps } { SWEAR(sp!=Special::Plain) ; }

	//
	// JobTgt
	//

	inline JobTgt::JobTgt( RuleTgt rt , ::string const& t , Bool3 chk_psfx , Req r , DepDepth lvl ) : JobTgt{ Job(rt,t,chk_psfx,r,lvl) , rt.sure() } {} // chk_psfx=Maybe means check size only

	inline bool JobTgt::sure() const {
		return is_static_phony() && self->sure() ;
	}

	inline bool JobTgt::produces(Node t,bool actual) const {
		if ( self->missing()                            ) return false                             ; // missing jobs produce nothing
		if (  actual && self->run_status!=RunStatus::Ok ) return false                             ; // jobs has not run, it has actually produced nothing
		if ( !actual && self->err()                     ) return true                              ; // jobs in error are deemed to produce all their potential targets
		if ( !actual && sure()                          ) return true                              ;
		if ( t->has_actual_job(self)                    ) return t->actual_tflags()[Tflag::Target] ; // .
		//
		auto it = ::lower_bound( self->targets , {t,{}} ) ;
		return it!=self->targets.end() && *it==t && it->tflags[Tflag::Target] ;
	}

	//
	// JobExec
	//

	inline bool/*reported*/ JobExec::report_start( ReqInfo& ri ) const { return report_start(ri,{}) ; }

	//
	// JobData
	//

	inline JobData::JobData           (JobData&& jd) : JobData(jd) {                                           jd.targets.forget() ; jd.deps.forget() ;               }
	inline JobData::~JobData          (            ) {                                                            targets.pop   () ;    deps.pop   () ;               }
	inline JobData& JobData::operator=(JobData&& jd) { SWEAR(rule()==jd.rule(),rule(),jd.rule()) ; self = jd ; jd.targets.forget() ; jd.deps.forget() ; return self ; }

	inline MsgStderr JobData::special_msg_stderr( bool short_msg                  ) const { return special_msg_stderr({},short_msg) ; }
	inline void      JobData::audit_end_special ( Req r , SpecialStep s , Bool3 m ) const { return audit_end_special(r,s,m,{}     ) ; }

	inline Tflags JobData::tflags(Node target) const {
		Target t = *::lower_bound( targets , {target,{}} ) ;
		SWEAR(t==target) ;
		return t.tflags ;
	}

	inline Rule::RuleMatch JobData::rule_match() const {
		return Rule::RuleMatch(idx()) ;
	}

	inline void JobData::estimate_stats() {                                                        // can be called any time, but only record on first time, so cost stays stable during job execution
		if (_reliable_stats!=No) return ;
		Rule r = rule() ;
		cost            = r->cost()    ;
		exec_time       = r->exec_time ;
		_reliable_stats = Maybe        ;
	}
	inline void JobData::estimate_stats( Tokens1 tokens1 ) {                                       // only called before submit, so cost stays stable during job execution
		if (_reliable_stats==Yes) return ;
		Rule r = rule() ;
		cost            = r->cost_per_token * (tokens1+1) ;
		exec_time       = r->exec_time                    ;
		_reliable_stats = Maybe                           ;
	}
	inline void JobData::record_stats( Delay exec_time_ , CoarseDelay cost_ , Tokens1 tokens1_ ) { // only called in end, so cost stays stable during job execution
		exec_time       = exec_time_ ;
		cost            = cost_      ;
		tokens1         = tokens1_   ;
		_reliable_stats = Yes        ;
		rule()->new_job_report( exec_time_ , cost_ , tokens1_ ) ;
	}

	inline void JobData::add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void JobData::set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                   // if pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_do_set_pressure(ri,pressure) ;
	}

	template<class... A> void JobData::audit_end(A&&... args) const {
		JobExec(idx(),New).audit_end(::forward<A>(args)...) ;
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

	inline bool              JobData::has_req   (Req r             ) const { return Req::s_store[+r      ].jobs.contains  (    idx()) ; }
	inline JobReqInfo const& JobData::c_req_info(Req r             ) const { return Req::s_store[+r      ].jobs.c_req_info(    idx()) ; }
	inline JobReqInfo      & JobData::req_info  (Req r             ) const { return Req::s_store[+r      ].jobs.req_info  (r  ,idx()) ; }
	inline JobReqInfo      & JobData::req_info  (ReqInfo const& cri) const { return Req::s_store[+cri.req].jobs.req_info  (cri,idx()) ; }
	//
	inline ::vector<Req> JobData::reqs() const { return Req::s_reqs(self) ; }

}

#endif
