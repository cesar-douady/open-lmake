// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 5 times, successively with following macros defined : STRUCT_DECL, STRUCT_DEF, INFO_DEF, DATA_DEF, IMPL

#ifdef STRUCT_DECL

enum class AncillaryTag : uint8_t {
	Backend
,	Data
,	Dbg
,	KeepTmp
} ;

// START_OF_VERSIONING REPO CACHE
enum class JobInfoKind : uint8_t {
	None
,	Start
,	End
,	DepCrcs
} ;
// END_OF_VERSIONING
using JobInfoKinds = BitMap<JobInfoKind> ;

enum class JobMakeAction : uint8_t {
	Wakeup                           // waited nodes are available
,	Status                           // target crcs are available
,	End                              // job has completed
,	GiveUp                           // job is abandonned, because of error or ^C
,	Query                            // used for dep analysis : query only, no action is intiated (for DepsVerbose and ChkDeps)
} ;

enum class JobStep : uint8_t { // must be in chronological order
	None                       // no analysis done yet (not in stats)
,	Dep                        // analyzing deps
,	Queued                     // waiting for execution
,	Exec                       // executing
,	Done                       // done execution (or impossible to execute)
,	Hit                        // cache hit
//
// aliases
,	MinCurStats  = Dep
,	MaxCurStats1 = Done
,	End          = Dep         // value to which step is set at end of execution to distinguish from an actively running job
} ;

enum class MissingAudit : uint8_t {
	No
,	Steady
,	Modified
} ;

enum class RunStatus : uint8_t {
	Ok
,	DepError      // job cannot run because some deps are in error
,	MissingStatic // job cannot run because missing static dep
,	Error         // job cannot run because an error was seen before even starting
} ;

enum class SpecialStep : uint8_t { // ordered by increasing importance
	Steady
,	Ok
,	Err
} ;

namespace Engine {

	struct SubmitInfo ;
	struct JobInfo    ;
	struct JobInfo1   ;
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
		static void s_init() ;
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
		bool is_plain(bool frozen_ok=false) const ;
		//
		::string ancillary_file(AncillaryTag tag=AncillaryTag::Data) const ;
		// services
		JobInfo job_info( BitMap<JobInfoKind> need=~BitMap<JobInfoKind>()) const ;           // read job info from ancillary file, taking care of queued events
		//
		void record(JobInfo1 const&) const ;
		void record(JobInfo  const&) const ;
		//
		using JobBase::pop ;
		void pop(Req req) ;
	} ;

	struct JobTgt : Job {
		static_assert(Job::NGuardBits>=1) ;                                                                                                          // need 1 bit to store is_static_phony bit
		static constexpr uint8_t NGuardBits = Job::NGuardBits-1       ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::string& operator+=( ::string& , JobTgt ) ;
		// cxtors & casts
		JobTgt() = default ;
		JobTgt( Job j , bool isp=false                                                            ) : Job{j} { if (+j) _set_is_static_phony(isp) ; } // if no job, ensure !sure()
		JobTgt( RuleTgt rt , ::string const& t , Bool3 chk_psfx=Yes , Req req={} , DepDepth lvl=0 ) ;                                                // chk_psfx=Maybe means check size only
		JobTgt( Rule::RuleMatch&& m , bool sure                     , Req req={} , DepDepth lvl=0 ) ;
		JobTgt( JobTgt const& jt                                                                  ) : JobTgt{jt,jt._is_static_phony()} {}
		//
		JobTgt& operator=(JobTgt const& jt) { Job::operator=(jt) ; _set_is_static_phony(jt._is_static_phony()) ; return self ; }
		//
		bool sure() const ;
	private :
		bool _is_static_phony    (        ) const { return Job::side<1>() ;                             }
		void _set_is_static_phony(bool isp)       { { if (isp) SWEAR(+self) ; } Job::set_side<1>(isp) ; }
		// services
	public :
		bool produces( Node , bool actual=false ) const ; // if actual, return if node was actually produced, in addition to being officially produced
	} ;

	struct JobTgts : JobTgtsBase {
		friend ::string& operator+=( ::string& , JobTgts ) ;
		// cxtors & casts
		using JobTgtsBase::JobTgtsBase ;
	} ;

	struct JobExec : Job {
		friend ::string& operator+=( ::string& , JobExec const& ) ;
		struct EndDigest {
			bool          can_upload        = true               ;
			bool          has_new_deps      = false              ;
			bool          has_unstable_deps = false              ;
			JobReason     target_reason     = JobReasonTag::None ;
			MsgStderr     msg_stderr        ;
			::string      severe_msg        ;
			::vector<Req> running_reqs      ;
		} ;
		// cxtors & casts
	public :
		JobExec() = default ;
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
		void             started     ( bool report , ::vmap<Node,FileActionTag> const& report_unlnks , MsgStderr const& ={} )       ; // txts is {backend_msg,stderr}
		//
		void live_out    ( ReqInfo& , ::string const& ) const ;
		void live_out    (            ::string const& ) const ;
		void add_live_out(            ::string const& ) const ;
		//
		JobMngtRpcReply manage     ( EngineClosureJobMngt const& ) const ; // answer to requests from job execution
		EndDigest       end_analyze( JobDigest<Node> &/*inout*/  )       ;
		void            end        ( JobDigest<Node>&&           )       ;
		void            give_up    ( Req={} , bool report=true   )       ; // Req (all if 0) was killed and job was not killed (not started or continue)
		//
		// audit_end returns the report to do if job is finally not rerun
		JobReport audit_end( ReqInfo&    , bool with_stats , ::string const& pfx    , MsgStderr const&           , Delay exe_time={} , bool retry=false ) const ;
		JobReport audit_end( ReqInfo& ri , bool with_stats , ::string const& pfx={} , ::string const& stderr={}  , Delay exe_time={} , bool retry=false ) const {
			return audit_end( ri , with_stats , pfx , MsgStderr{.stderr=stderr} , exe_time , retry ) ;
		}
		size_t hash() const {
			Hash::Fnv fnv ;                                                // good enough
			fnv += +Job(self)        ;
			fnv +=  host             ;
			fnv +=  cost.hash()      ;
			fnv +=  start_date.val() ;
			fnv +=  end_date  .val() ;
			return +fnv ;
		}
		// data
		CacheIdx    cache_idx1     = 0 ;                                   // 0 means no cache
		Tokens1     tokens1        = 0 ;
		uint16_t    max_stderr_len = 0 ;
		in_addr_t   host           = 0 ;
		CoarseDelay cost           ;                                       // exec time / average number of running job during execution
		Pdate       start_date     ;
		Pdate       end_date       ;                                       // if no end_date, job is stil on going
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
		JobReqInfo() = default ;
		JobReqInfo( Req r , Job ) : ReqInfo{r} {}
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
			DF}                                                        // NO_COV
		}
		// data
		struct State {
			friend ::string& operator+=( ::string& , State const& ) ;
			struct Bits {
				RunStatus err  :NBits<RunStatus> = {}    ;
				bool      modif:1                = false ;
			} ;
			// cxtors & casts
			State() = default ;
			// data
			JobReason reason      = {}    ;                            //  36  <= 64 bits, reason to run job when deps are ready, due to dep analysis
			bool      missing_dsk = false ;                            //   1  <=  8 bits, if true <=>, a dep has been checked but not on disk and analysis must be redone if job has to run
			Bits      proto       = {}    ;                            //   3  <=  8 bits, seen including last parallel chunk
			Bits      stamped     = {}    ;                            //   3  <=  8 bits, seen before    last parallel chunk
		} ;
//		ReqInfo                                                        //        128 bits, inherits
		State            state                ;                        //  43  <= 96 bits, dep analysis state
		DepsIter::Digest iter                 ;                        // ~20+6<= 64 bits, deps up to this one statisfy required action
		JobReason        reason               ;                        //  36  <= 64 bits, reason to run job when deps are ready, forced (before deps) or asked by caller (after deps)
		uint16_t         n_runs               = 0     ;                //         16 bits, number of times job has been rerun
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
		Step _step:NBits<Step> = {} ;                                  //          3 bits
	} ;

	//
	// JobInfo
	//

	struct SubmitInfo {
		friend ::string& operator+=( ::string& , SubmitInfo const& ) ;
		// services
		SubmitInfo& operator|=(SubmitInfo const& si) {
			// cache, deps and tag are independent of req but may not always be present
			if (!cache_idx1  ) cache_idx1    =                si.cache_idx1   ; else if (+si.cache_idx1  ) SWEAR( cache_idx1  ==si.cache_idx1   , cache_idx1  ,si.cache_idx1   ) ;
			if (!deps        ) deps          =                si.deps         ; else if (+si.deps        ) SWEAR( deps        ==si.deps         , deps        ,si.deps         ) ;
			/**/               live_out     |=                si.live_out     ;
			/**/               nice          = ::min(nice    ,si.nice     )   ;
			/**/               pressure      = ::max(pressure,si.pressure )   ;
			/**/               reason       |=                si.reason       ;
			/**/               tokens1       = ::max(tokens1 ,si.tokens1  )   ;
			if (!used_backend) used_backend  =                si.used_backend ; else if (+si.used_backend) SWEAR( used_backend==si.used_backend , used_backend,si.used_backend ) ;
			return self ;
		}
		SubmitInfo operator|(SubmitInfo const& si) const {
			SubmitInfo res = self ;
			res |= si ;
			return res ;
		}
		void cache_cleanup() ;
		void chk(bool for_cache=false) const ;
		// data
		// START_OF_VERSIONING REPO CACHE
		CacheIdx            cache_idx1   = 0     ; // 0 means no cache
		::vmap_s<DepDigest> deps         = {}    ;
		bool                live_out     = false ;
		uint8_t             nice         = -1    ; // -1 means not specified
		Time::CoarseDelay   pressure     = {}    ;
		JobReason           reason       = {}    ;
		Tokens1             tokens1      = 0     ;
		BackendTag          used_backend = {}    ; // tag actually used (possibly made local because asked tag is not available)
		// END_OF_VERSIONING
	} ;

	struct JobInfoStart {
		friend ::string& operator+=( ::string& , JobInfoStart const& ) ;
		// accesses
		bool operator+() const { return +pre_start ; }
		// services
		void cache_cleanup() ;                 // clean up info before uploading to cache
		void chk(bool for_cache=false) const ;
		// data
		// START_OF_VERSIONING REPO CACHE
		Hash::Crc        rule_crc_cmd = {} ;
		::vector_s       stems        = {} ;
		Time::Pdate      eta          = {} ;
		SubmitInfo       submit_info  = {} ;
		::vmap_ss        rsrcs        = {} ;
		JobStartRpcReq   pre_start    = {} ;
		JobStartRpcReply start        = {} ;
		// END_OF_VERSIONING
	} ;

	struct JobInfo {
		JobInfo() = default ;
		JobInfo( ::string const& ancillary_file , JobInfoKinds need=~JobInfoKinds() ) { fill_from(ancillary_file,need) ; }
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , start,end,dep_crcs ) ;
		}
		void fill_from( ::string const& ancillary_file , JobInfoKinds need=~JobInfoKinds() ) ;
		//
		void update_digest(                    ) ;         // update crc in digest from dep_crcs
		void cache_cleanup(                    ) ;         // clean up info before uploading to cache
		void chk          (bool for_cache=false) const ;
		// data
		// START_OF_VERSIONING REPO CACHE
		JobInfoStart                            start    ;
		JobEndRpcReq                            end      ;
		::vector<::pair<Hash::Crc,bool/*err*/>> dep_crcs ; // optional, if not provided in end.digest.deps
		// END_OF_VERSIONING
	} ;
	::string cache_repo_cmp( JobInfo const& info_cache , JobInfo const& info_repo ) ;

	struct JobInfo1
	:	             ::variant< ::monostate/*None*/ , JobInfoStart/*Start*/ , JobEndRpcReq/*End*/ , ::vector<::pair<Crc,bool/*err*/>>/*DepCrcs*/ >
	{	using Base = ::variant< ::monostate/*None*/ , JobInfoStart/*Start*/ , JobEndRpcReq/*End*/ , ::vector<::pair<Crc,bool/*err*/>>/*DepCrcs*/ > ;
		using Kind = JobInfoKind ;
		// cxtors & casts
		using Base::variant ; // necessary for clang++-14
		// accesses
		/**/             Kind kind() const { return Kind(index()) ; }
		template<Kind K> bool is_a() const { return index()==+K   ; }
		//
		JobInfoStart                      const& start   () const { return ::get<JobInfoStart                     >(self) ; }
		JobInfoStart                           & start   ()       { return ::get<JobInfoStart                     >(self) ; }
		JobEndRpcReq                      const& end     () const { return ::get<JobEndRpcReq                     >(self) ; }
		JobEndRpcReq                           & end     ()       { return ::get<JobEndRpcReq                     >(self) ; }
		::vector<::pair<Crc,bool/*err*/>> const& dep_crcs() const { return ::get<::vector<::pair<Crc,bool/*err*/>>>(self) ; }
		::vector<::pair<Crc,bool/*err*/>>      & dep_crcs()       { return ::get<::vector<::pair<Crc,bool/*err*/>>>(self) ; }
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct JobData : JobDataBase {
		using Idx        = JobIdx        ;
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		// static data
	private :
		static Mutex<MutexLvl::TargetDir> _s_target_dirs_mutex ;
		static ::umap<Node,Idx/*cnt*/>    _s_target_dirs       ; // dirs created for job execution that must not be deleted
		static ::umap<Node,Idx/*cnt*/>    _s_hier_target_dirs  ; // uphill hierarchy of _s_target_dirs
		// cxtors & casts
	public :
		JobData() = delete ;
		JobData( JobName n                                       ) : JobDataBase{n}                                       {                     }
		JobData( JobName n , Special sp , Deps ds={}             ) : JobDataBase{n} , deps{ds } , rule_crc{Rule(sp)->crc} {                     } // special Job, all deps
		JobData( JobName n , Rule::RuleMatch const& m , Deps sds ) : JobDataBase{n} , deps{sds} , rule_crc{m.rule->crc  } { _reset_targets(m) ; } // plain Job, static targets and deps
		//
		JobData           (JobData&& jd) ;
		~JobData          (            ) ;
		JobData& operator=(JobData&& jd) ;
	private :
		JobData           (JobData const&) = default ;
		JobData& operator=(JobData const&) = default ;
		void _reset_targets(Rule::RuleMatch const&) ;
		void _reset_targets(                      ) { _reset_targets(rule_match()) ; }
		void _close        (                      ) ;
		// accesses
	public :
		bool has_targets() const { return rule()->special>=Special::HasTargets ; }
		bool is_dep     () const { return rule()->special==Special::Dep        ; }
		//
		Node             & asking    ()       { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.asking                   ; }
		Node        const& asking    () const { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.asking                   ; }
		Targets          & targets   ()       { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.targets                  ; }
		Targets     const& targets   () const { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.targets                  ; }
		CoarseDelay      & exe_time  ()       { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.exe_time                 ; }
		CoarseDelay const& exe_time  () const { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.exe_time                 ; }
		CoarseDelay        c_exe_time() const {                                          ; return has_targets() ? _if_plain.exe_time : CoarseDelay() ; }
		CoarseDelay      & cost      ()       { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.cost                     ; }
		CoarseDelay const& cost      () const { SWEAR( has_targets() , rule()->special ) ; return                 _if_plain.cost                     ; }
		CoarseDelay        c_cost    () const {                                          ; return has_targets() ? _if_plain.cost     : CoarseDelay() ; }
		Fd               & fd        ()       { SWEAR( is_dep     () , rule()->special ) ; return                 _if_dep  .fd                       ; }
		Fd          const& fd        () const { SWEAR( is_dep     () , rule()->special ) ; return                 _if_dep  .fd                       ; }
		SeqId            & seq_id    ()       { SWEAR( is_dep     () , rule()->special ) ; return                 _if_dep  .seq_id                   ; }
		SeqId       const& seq_id    () const { SWEAR( is_dep     () , rule()->special ) ; return                 _if_dep  .seq_id                   ; }
		Job              & asking_job()       { SWEAR( is_dep     () , rule()->special ) ; return                 _if_dep  .asking_job               ; }
		Job         const& asking_job() const { SWEAR( is_dep     () , rule()->special ) ; return                 _if_dep  .asking_job               ; }
		//
		CoarseDelay phy_exe_time() const { return cache_hit_info==CacheHitInfo::Hit ? CoarseDelay() : exe_time() ; }
		//
		Job      idx () const { return Job::s_idx(self) ; }
		Rule     rule() const { return rule_crc->rule   ; }                                                                       // thread-safe
		::string name() const {
			::string res ;
			if ( Rule r=rule() ; +r )   res = full_name(r->job_sfx_len()) ;
			else                      { res = full_name(                ) ; res.resize(res.find(RuleData::JobMrkr)) ; }           // heavier, but works without rule
			return res ;
		}
		::string unique_name() const ;
		//
		ReqInfo const& c_req_info  ( Req                                        ) const ;
		ReqInfo      & req_info    ( Req                                        ) const ;
		ReqInfo      & req_info    ( ReqInfo const&                             ) const ;                                         // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs        (                                            ) const ;
		::vector<Req>  running_reqs( bool with_zombies=true , bool hit_ok=false ) const ;
		bool           running     ( bool with_zombies=true , bool hit_ok=false ) const ;                                         // fast implementation of +running_reqs(...)
		//
		bool cmd_ok  (                    ) const { return                      rule_crc->state<=RuleCrcState::CmdOk ; }
		bool rsrcs_ok(                    ) const { return is_ok(status)!=No || rule_crc->state==RuleCrcState::Ok    ; }          // dont care about rsrcs if job went ok
		bool is_plain(bool frozen_ok=false) const { return rule()->is_plain() && (frozen_ok||!idx().frozen())        ; }
		bool has_req (Req                 ) const ;
		//
		void set_exec_ok() {                                                                                                      // set official rule_crc (i.e. with the right cmd and rsrcs crc's)
			Rule r = rule() ; SWEAR(r->is_plain(),r->special) ;
			rule_crc = r->crc ;
		}
		//
		bool sure   () const ;
		void mk_sure()       { match_gen = Rule::s_match_gen ; _sure = true ; }
		bool err() const {
			switch (run_status) {
				case RunStatus::Ok            : return is_ok(status)!=Yes ;
				case RunStatus::DepError      : return true               ;
				case RunStatus::MissingStatic : return false              ;
				case RunStatus::Error         : return true               ;
			DF}                                                                                                                   // NO_COV
		}
		bool missing() const { return run_status==RunStatus::MissingStatic ; }
		// services
		::vmap<Node,FileAction> pre_actions( Rule::RuleMatch const& , bool no_incremental , bool mark_target_dirs=false ) const ; // thread-safe
		//
		Tflags tflags(Node target) const ;
		//
		void      end_exec          (                                     ) const ;                                               // thread-safe
		::string  ancillary_file    ( AncillaryTag tag=AncillaryTag::Data ) const { return idx().ancillary_file(tag) ; }
		MsgStderr special_msg_stderr( Node , bool short_msg=false         ) const ;
		MsgStderr special_msg_stderr(        bool short_msg=false         ) const ;                                               // cannot declare a default value for incomplete type Node
		//
		Rule::RuleMatch rule_match    (                                             ) const ;                                     // thread-safe
		void            estimate_stats(                                             )       ;                                     // may be called any time
		void            estimate_stats(                                     Tokens1 )       ;                                     // must not be called during job execution as cost must stay stable
		void            record_stats  ( Delay exe_time , CoarseDelay cost , Tokens1 )       ;                                     // .
		//
		void set_pressure( ReqInfo& , CoarseDelay ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ;                                                   // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			if ( speculate==No && ri.reported && ri.done() ) {
				if      (err()                ) { audit_end(ri,false/*with_stats*/,"was_") ; req->stats.move( JobReport::Speculative , JobReport::Failed , phy_exe_time() ) ; }
				else if (ri.modified_speculate)                                              req->stats.move( JobReport::Speculative , JobReport::Done   , phy_exe_time() ) ;
				else                                                                         req->stats.move( JobReport::Speculative , JobReport::Steady , phy_exe_time() ) ;
			}
			_propag_speculate(ri) ;
		}
		//
		JobReason/*err*/ make( ReqInfo& , MakeAction , JobReason={} , Bool3 speculate=Yes , bool wakeup_watchers=true ) ;
		//
		void wakeup(ReqInfo& ri) { make(ri,MakeAction::Wakeup) ; }
		//
		void refresh_codec(Req) ;
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		void add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) ;
		//
		void audit_end_special( Req , SpecialStep , Bool3 modified , Node ) const ;                                               // modified=Maybe means file is new
		void audit_end_special( Req , SpecialStep , Bool3 modified        ) const ;                                               // cannot use default Node={} as Node is incomplete
		//
		template<class... A> void audit_end(A&&... args) const ;
	private :
		void _propag_speculate(ReqInfo const&) const ;
		//
		void _submit_codec   ( Req                             )       ;
		void _submit_special ( ReqInfo&                        )       ;
		void _submit_plain   ( ReqInfo& , CoarseDelay pressure )       ;
		void _do_set_pressure( ReqInfo& , CoarseDelay          ) const ;
		// data
		// START_OF_VERSIONING REPO
		struct IfPlain {
			Node        asking   ;                                    //     32 bits,        last target needing this job
			Targets     targets  ;                                    //     32 bits, owned, for plain jobs
			CoarseDelay exe_time ;                                    //     16 bits,        for plain jobs
			CoarseDelay cost     ;                                    //     16 bits,        exe_time / average number of parallel jobs during execution, /!\ must be stable during job execution
		} ;
		struct IfDep {
			SeqId seq_id     = 0 ;                                    //     64 bits
			Fd    fd         ;                                        //     32 bits
			Job   asking_job ;                                        //     32 bits
		} ;
	public :
		//JobName        name                               ;         //     32 bits, inherited
		Deps             deps                               ;         // 31<=32 bits, owned
		RuleCrc          rule_crc                           ;         //     32 bits
		Tokens1          tokens1                            = 0     ; //      8 bits, for plain jobs, number of tokens - 1 for eta estimation
		mutable MatchGen match_gen                          = 0     ; //      8 bits, if <Rule::s_match_gen => deemed !sure
		RunStatus        run_status    :NBits<RunStatus   > = {}    ; //      3 bits
		BackendTag       backend       :NBits<BackendTag  > = {}    ; //      2 bits  backend asked for last execution
		CacheHitInfo     cache_hit_info:NBits<CacheHitInfo> = {}    ; //      3 bits
		Status           status        :NBits<Status      > = {}    ; //      4 bits
		bool             incremental   :1                   = false ; //      1 bit , job was last run with existing incremental targets
	private :
		mutable bool _sure          :1 = false ;                      //      1 bit
		Bool3        _reliable_stats:2 = No    ;                      //      2 bits, if No <=> no known info, if Maybe <=> guestimate only, if Yes <=> recorded info
	public :
		union {
			IfPlain  _if_plain = {} ;                                 //     96 bits
			IfDep    _if_dep   ;                                      //    128 bits
		} ;
		// END_OF_VERSIONING
	} ;
	static_assert(sizeof(JobData)==32) ;                              // ensure size is a power of 2 to maximize cache perf

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
	inline Job::Job( Special sp ,          Deps deps ) : Job{                                 New , sp,deps } { SWEAR( sp==Special::Req || sp==Special::Dep , sp ) ; }
	inline Job::Job( Special sp , Node t , Deps deps ) : Job{ {t->name(),Rule(sp)->job_sfx()},New , sp,deps } { SWEAR( sp!=Special::Plain                        ) ; }

	inline bool Job::is_plain(bool frozen_ok) const {
		return +self && self->is_plain(frozen_ok) ;
	}

	inline void Job::s_init() {
		s_record_thread.open('J',
			[](::pair<Job,JobInfo1> const& jji)->void {
				Trace trace("s_record_thread",jji.first,jji.second.kind()) ;
				jji.first.record(jji.second) ;
			}
		) ;
	}

	//
	// JobTgt
	//

	inline JobTgt::JobTgt( RuleTgt rt , ::string const& t , Bool3 chk_psfx , Req r , DepDepth lvl ) : JobTgt{ Job(rt,t,chk_psfx,r,lvl) , rt.sure() } {} // chk_psfx=Maybe means check size only
	inline JobTgt::JobTgt( Rule::RuleMatch&& m , bool sure                 , Req r , DepDepth lvl ) : JobTgt{ Job(::move(m)    ,r,lvl) , sure      } {}

	inline bool JobTgt::sure() const {
		return _is_static_phony() && self->sure() ;
	}

	inline bool JobTgt::produces(Node t,bool actual) const {
		if ( self->missing()                            ) return false                           ; // missing jobs produce nothing
		if (  actual && self->run_status!=RunStatus::Ok ) return false                           ; // jobs has not run, it has actually produced nothing
		if ( !actual && self->err()                     ) return true                            ; // jobs in error are deemed to produce all their potential targets
		if ( !actual && sure()                          ) return true                            ;
		if ( t->has_actual_job(self)                    ) return t->actual_tflags[Tflag::Target] ; // .
		//
		auto it = ::lower_bound( self->targets() , {t,{}} ) ;
		return it!=self->targets().end() && *it==t && it->tflags[Tflag::Target] ;
	}

	//
	// JobExec
	//

	inline bool/*reported*/ JobExec::report_start( ReqInfo& ri ) const { return report_start(ri,{}) ; }

	//
	// JobData
	//

	inline void JobData::_close() {
		if (has_targets()) targets().pop() ;
		/**/               deps     .pop() ;
	}
	inline JobData::JobData           (JobData&& jd) : JobData(jd) {                                                         jd._close() ;               }
	inline JobData::~JobData          (            )               {                                                            _close() ;               }
	inline JobData& JobData::operator=(JobData&& jd)               { SWEAR(rule()==jd.rule(),rule(),jd.rule()) ; self = jd ; jd._close() ; return self ; }

	inline MsgStderr JobData::special_msg_stderr( bool short_msg                  ) const { return special_msg_stderr({},short_msg) ; }
	inline void      JobData::audit_end_special ( Req r , SpecialStep s , Bool3 m ) const { return audit_end_special(r,s,m,{}     ) ; }

	inline Tflags JobData::tflags(Node target) const {
		Target t = *::lower_bound( targets() , {target,{}} ) ;
		SWEAR(t==target) ;
		return t.tflags ;
	}

	inline Rule::RuleMatch JobData::rule_match() const { // thread-safe
		return Rule::RuleMatch(idx()) ;
	}

	inline void JobData::estimate_stats() {                                                       // can be called any time, but only record on first time, so cost stays stable during job execution
		if (_reliable_stats!=No) return ;
		if (!has_targets()     ) return ;
		Rule r = rule() ;
		exe_time()      = r->exe_time ;
		cost    ()      = r->cost()   ;
		_reliable_stats = Maybe       ;
	}
	inline void JobData::estimate_stats( Tokens1 tokens1 ) {                                      // only called before submit, so cost stays stable during job execution
		if (_reliable_stats==Yes) return ;
		if (!has_targets()      ) return ;
		Rule r = rule() ;
		exe_time()      = r->exe_time                     ;
		cost    ()      = r->cost_per_token * (tokens1+1) ;
		_reliable_stats = Maybe                           ;
	}
	inline void JobData::record_stats( Delay exe_time_ , CoarseDelay cost_ , Tokens1 tokens1_ ) { // only called in end, so cost stays stable during job execution
		exe_time()      = exe_time_ ;
		cost    ()      = cost_     ;
		tokens1         = tokens1_  ;
		_reliable_stats = Yes       ;
		rule()->new_job_report( exe_time_ , cost_ , tokens1_ ) ;
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
		JobExec je { idx() , New } ;
		je.max_stderr_len = rule()->start_ancillary_attrs.spec.max_stderr_len ; // in case it is not dynamic
		je.audit_end(::forward<A>(args)...) ;
	}

	inline bool JobData::sure() const {
		if (match_gen<Rule::s_match_gen) {
			match_gen = Rule::s_match_gen                                                                                        ;
			_sure     = ::none_of( deps , [](Dep const& d) { return d.dflags[Dflag::Static] && d->buildable<Buildable::Yes ; } ) ; // only static deps are required to exist for job to be selected
		}
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
