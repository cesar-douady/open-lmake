// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#ifdef STRUCT_DECL

#include "rpc_job.hh"

enum class ConnState : uint8_t {
	New
,	Old
,	Lost
} ;

enum class HeartbeatState : uint8_t {
	Alive
,	Lost
,	Err
} ;

namespace Backends {

	struct Backend ;

	using namespace Engine ;

	using Tag = BackendTag ;

	static constexpr Channel BeChnl = Channel::Backend ;

}

using Backends::Backend ;
#endif
#ifdef DATA_DEF

namespace Backends {

	void send_reply( Job , JobMngtRpcReply&& ) ;

	inline ::string get_stderr_file(Job job) { return job.ancillary_file(AncillaryTag::Backend)+"/stderr" ; }

	struct Backend {

		friend void send_reply( Job , JobMngtRpcReply&& ) ;

		using SmallId     = uint32_t          ;
		using CoarseDelay = Time::CoarseDelay ;
		using Pdate       = Time::Pdate       ;
		using Proc        = JobRpcProc        ;

		struct Workload {
			// a job stops being reasonable when it has already run longer than its last known exec_time
			// a workload is a sum of weighted exec times in ms, i.e. with 3 jobs for 4 tokens in parallel workload advances by 4 each ms
			// all delays and dates are rounded to ms to avoid rounding errors
			friend ::string& operator+=( ::string& , Workload const& ) ;
			using Val    = uint64_t                  ;
			using Tokens = Uint<sizeof(Tokens1)*8+1> ;                         // +1 to allow adding 1 without overflow
		private :
			//services
		public :
			void submit( Req r                   , Job ) ;                     // anticipate job execution
			void add   ( Req r                   , Job ) ;                     // queue job if not already started
			void kill  ( Req r                   , Job ) ;                     // finally decide not to execute it
			Val  start ( ::vector<ReqIdx> const& , Job ) ;                     // start an anticipated job
			Val  end   ( ::vector<ReqIdx> const& , Job ) ;                     // end a started job
			//
			void  open_req     ( Req r                                       )       { _queued_cost[+r] = 0 ; }
			void  close_req    ( Req                                         )       {                        }
			Delay cost         ( Job , Val start_workload , Pdate start_date ) const ;
			Pdate submitted_eta( Req                                         ) const ;
		private :
			void _refresh() ;
			// data
			Mutex<MutexLvl::Workload> mutable _mutex               ;
			Val                               _ref_workload        = 0 ;       // total workload at ref_date
			Pdate                             _ref_date            ;           // later than any job start date and end date, always rounded to ms
			::umap<Job,Pdate>                 _eta_tab             ;           // jobs whose eta is post ref_date
			::set<::pair<Pdate,Job>>          _eta_set             ;           // same info, but ordered by dates
			Val                               _reasonable_workload = 0 ;       // sum of (eta-_ref_date) in _eta_tab
			JobIdx                            _running_tokens      = 0 ;       // sum of tokens for all running jobs
			JobIdx                            _reasonable_tokens   = 0 ;       // sum ok tokens in _eta_tab
			//
			::array<Atomic<Delay::Tick>,size_t(1)<<NReqIdxBits> _queued_cost ; // use plain integer so as to use atomic inc/dec instructions because schedule/cancel are called w/o lock
		} ;

		struct StartEntry {
			friend ::string& operator+=( ::string& , StartEntry const& ) ;
			struct Conn {
				friend ::string& operator+=( ::string& , Conn const& ) ;
				// accesses
				bool operator+() const { return seq_id ; }
				// data
				SeqId     seq_id   = 0 ;
				SmallId   small_id = 0 ;
				in_addr_t host     = 0 ;
				in_port_t port     = 0 ;
			} ;
			// cxtors & casts
			StartEntry() = default ;
			// accesses
			bool operator+() const { return +conn ; }
			// services
			::pair<Pdate/*eta*/,bool/*keep_tmp*/> req_info() const ;
			// data
			Conn             conn           ;
			Pdate            spawn_date     ;
			Pdate            start_date     ;
			Workload::Val    workload       = 0            ;
			::uset_s         washed         ;
			::vmap_ss        rsrcs          ;
			::vector<ReqIdx> reqs           ;
			SubmitAttrs      submit_attrs   ;
			uint16_t         max_stderr_len = 0            ;
			Tag              tag            = Tag::Unknown ;
		} ;

		struct DeferredEntry {
			friend ::string& operator+=( ::string& , DeferredEntry const& ) ;
			// cxtors & casts
			DeferredEntry( SeqId si=0 , JobExec je={} ) : seq_id{si} , job_exec{je} {}
			// data
			SeqId   seq_id   = 0 ;
			JobExec job_exec ;
		} ;

		using JobStartThread = ServerThread    <JobStartRpcReq,false/*Flush*/> ;
		using JobMngtThread  = ServerThread    <JobMngtRpcReq ,false/*Flush*/> ;
		using JobEndThread   = ServerThread    <JobEndRpcReq  ,false/*Flush*/> ;
		using DeferredThread = TimedQueueThread<DeferredEntry ,false/*Flush*/> ;
		// statics
		static bool            s_ready     (Tag) ;
		static ::string const& s_config_err(Tag) ;
		//
		static void s_config( ::array<Config::Backend,N<Tag>> const& config , bool dyn , bool first_time ) ;             // send warnings on first time only
		// sub-backend is responsible for job (i.e. answering to heart beat and kill) from submit to start
		// then it is top-backend that mangages it until end, at which point it is transfered back to engine
		// called from engine thread
		static void                  s_open_req    (             Req , JobIdx n_jobs                          ) ;
		static void                  s_close_req   (             Req                                          ) ;
		static void                  s_submit      ( Tag , Job , Req , SubmitAttrs     && , ::vmap_ss&& rsrcs ) ;
		static bool/*miss_live_out*/ s_add_pressure( Tag , Job , Req , SubmitAttrs const&                     ) ;
		static void                  s_set_pressure( Tag , Job , Req , SubmitAttrs const&                     ) ;
		//
		static void s_kill_all    (     ) {             _s_kill_req( ) ; }
		static void s_kill_req    (Req r) { SWEAR(+r) ; _s_kill_req(r) ; }
		static void s_new_req_etas(     ) ;
		static void s_launch      (     ) ;
		//
		static Pdate s_submitted_eta(Req r) { return _s_workload.submitted_eta(r) ; }
		// called by job_exec thread
		static ::string/*msg*/          s_start    ( Tag , Job          ) ;                                              // called by job_exec  thread, sub-backend lock must have been takend by caller
		static ::pair_s<bool/*retry*/>  s_end      ( Tag , Job , Status ) ;                                              // .
		static void                     s_heartbeat( Tag                ) ;                                              // called by heartbeat thread, sub-backend lock must have been takend by caller
		static ::pair_s<HeartbeatState> s_heartbeat( Tag , Job          ) ;                                              // called by heartbeat thread, sub-backend lock must have been takend by caller
		//
	protected :
		static void s_register( Tag t , Backend& be ) {
			s_tab[+t].reset(&be) ;
		}
	private :
		static void            _s_kill_req               ( Req={}                                                    ) ; // kill all if req==0
		static void            _s_wakeup_remote          ( Job , StartEntry::Conn const& , Pdate start , JobMngtProc ) ;
		static void            _s_heartbeat_thread_func  ( ::stop_token                                              ) ;
		static bool/*keep_fd*/ _s_handle_job_start       ( JobStartRpcReq&& , SlaveSockFd const& ={}                 ) ;
		static bool/*keep_fd*/ _s_handle_job_mngt        ( JobMngtRpcReq && , SlaveSockFd const& ={}                 ) ;
		static bool/*keep_fd*/ _s_handle_job_end         ( JobEndRpcReq  && , SlaveSockFd const& ={}                 ) ;
		static void            _s_handle_deferred_report ( ::stop_token                                              ) ;
		static void            _s_handle_deferred_wakeup ( DeferredEntry&&                                           ) ;
		static void            _s_start_tab_erase        ( ::map<Job,StartEntry>::iterator                           ) ;
		// static data
	public :
		static ::unique_ptr<Backend> s_tab[N<Tag>] ;
	protected :
		static Mutex<MutexLvl::Backend> _s_mutex ;
	private :
		static ::string                             _s_job_exec                      ;
		static WakeupThread<false/*Flush*/>         _s_deferred_report_thread        ;
		static DeferredThread                       _s_deferred_wakeup_thread        ;
		static JobStartThread                       _s_job_start_thread              ;
		static JobMngtThread                        _s_job_mngt_thread               ;
		static JobEndThread                         _s_job_end_thread                ;
		static SmallIds<SmallId,true/*ThreadSafe*/> _s_small_ids                     ;
		static Atomic<JobIdx>                       _s_starting_job                  ;                 // this job is starting when _starting_job_mutex is locked
		static Mutex<MutexLvl::StartJob>            _s_starting_job_mutex            ;
		static ::map<Job,StartEntry>                _s_start_tab                     ;                 // use map instead of umap because heartbeat iterates over while tab is moving
		static Workload                             _s_workload                      ;                 // book keeping of workload
		static ::map <Pdate,JobExec>                _s_deferred_report_queue_by_date ;
		static ::umap<Job  ,Pdate  >                _s_deferred_report_queue_by_job  ;
		// cxtors & casts
	public :
		virtual ~Backend() = default ;                                                                 // ensure all fields of sub-backends are correctly destroyed
		// services
		// PER_BACKEND : these virtual functions must be implemented by sub-backend, some of them have default implementations that do nothing when meaningful
		virtual ::vmap_ss descr (                                                                    ) const { return {}   ; }
		virtual void      config( ::vmap_ss const& /*dct*/ , ::vmap_ss const& /*env*/ , bool /*dyn*/ )       {               }
		//
		virtual void          open_req         ( Req    , JobIdx /*n_jobs*/ ) {}                       // called before any operation on req , n_jobs is the max number of jobs that can be launched
		virtual void          new_req_etas     (                            ) {}                       // inform backend that req has a new eta, which may change job priorities
		virtual void          close_req        ( Req                        ) {}                       // called after all operations on req (only open is legal after that)
		virtual ::vector<Job> kill_waiting_jobs( Req={}                     ) = 0 ;                    // kill all waiting jobs for this req (all if 0), return killed jobs
		virtual void          kill_job         ( Job                        ) = 0 ;                    // job must be spawned
		//
		virtual void submit      ( Job , Req , SubmitAttrs const& , ::vmap_ss&& /*rsrcs*/ ) = 0 ;      // submit a new job
		virtual void add_pressure( Job , Req , SubmitAttrs const&                         ) {}         // add a new req for an already submitted job
		virtual void set_pressure( Job , Req , SubmitAttrs const&                         ) {}         // set a new pressure for an existing req of a job
		//
		virtual void                     launch   (          ) = 0 ;                                   // called to trigger launch of waiting jobs
		virtual ::string/*msg*/          start    (Job       ) = 0 ;                                   // tell sub-backend job started, return an informative message
		virtual ::pair_s<bool/*retry*/>  end      (Job,Status) { return {}                         ; } // tell sub-backend job ended, return a message and whether to retry jobs with garbage status
		virtual void                     heartbeat(          ) {                                     } // regularly called between launch and start
		virtual ::pair_s<HeartbeatState> heartbeat(Job       ) { return {{},HeartbeatState::Alive} ; } // regularly called between launch and start, initially with enough delay for job to connect
		//
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& /*rsrcs*/ , ::vmap_s<size_t> const& /*capacity*/ , JobIdx ) const { return {} ; } // map resources for this backend to local resources
		//
		virtual ::vmap_s<size_t> const& capacity() const { FAIL("only for local backend") ; }                                   // NO_COV
	protected :
		::vector_s acquire_cmd_line( Tag , Job , ::vector<ReqIdx>&& , ::vmap_ss&& rsrcs , SubmitAttrs&& ) ; // must be called once before job is launched, SubmitAttrs must be the operator| of ...
		/**/                                                                                                // ... the submit/add_pressure corresponding values for the job
		// data
	public :
		in_addr_t addr       = 0 ;
		::string  config_err ;
	} ;

}

#endif
#ifdef IMPL

namespace Backends {

	inline void  Backend::Workload::submit( Req r , Job j ) { Lock lock{_mutex} ;                            _queued_cost[+r] += Delay(j->cost).val() ; }
	inline void  Backend::Workload::add   ( Req r , Job j ) { Lock lock{_mutex} ; if (!_eta_tab.contains(j)) _queued_cost[+r] += Delay(j->cost).val() ; }
	inline void  Backend::Workload::kill( Req r , Job j ) {
		Delay::Tick dly = Delay(j->cost).val() ;
		SWEAR( _queued_cost[+r]>=dly , _queued_cost[+r] , dly ) ;
		_queued_cost[+r] -= dly ;
	}
	inline Pdate Backend::Workload::submitted_eta(Req r) const {
		Lock lock{_mutex} ;
		Pdate res = _ref_date + Delay(New,_queued_cost[+r]) ;
		if (_running_tokens) res += Delay(_reasonable_workload/1000./_running_tokens) ; // divide by 1000 to convert to seconds
		return res ;
	}

	inline bool            Backend::s_ready     (Tag t) { return +t && s_tab[+t] && !s_tab[+t]->config_err ; }
	inline ::string const& Backend::s_config_err(Tag t) { return                     s_tab[+t]->config_err ; }
	//
	// nj is the maximum number of job backend may run on behalf of this req
	#define LCK(...) TraceLock lock{_s_mutex,BeChnl,"s_backend"} ; Trace trace(BeChnl,__VA_ARGS__)
	inline void Backend::s_open_req    (Req r,JobIdx nj) { LCK("s_open_req"    ,r) ; _s_workload.open_req (r) ; for( Tag t : iota(All<Tag>) ) if (s_ready(t)) s_tab[+t]->open_req    (r,nj) ; }
	inline void Backend::s_close_req   (Req r          ) { LCK("s_close_req"   ,r) ; _s_workload.close_req(r) ; for( Tag t : iota(All<Tag>) ) if (s_ready(t)) s_tab[+t]->close_req   (r   ) ; }
	inline void Backend::s_new_req_etas(               ) { LCK("s_new_req_etas"  ) ;                            for( Tag t : iota(All<Tag>) ) if (s_ready(t)) s_tab[+t]->new_req_etas(    ) ; }
	inline void Backend::s_launch      (               ) { LCK("s_launch"        ) ;                            for( Tag t : iota(All<Tag>) ) if (s_ready(t)) s_tab[+t]->launch      (    ) ; }
	#undef LCK
	//
	inline ::string/*msg*/          Backend::s_start    ( Tag t , Job j            ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_start"    ,t,j) ; return s_tab[+t]->start    (j  ) ; }
	inline ::pair_s<bool/*retry*/>  Backend::s_end      ( Tag t , Job j , Status s ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_end"      ,t,j) ; return s_tab[+t]->end      (j,s) ; }
	inline void                     Backend::s_heartbeat( Tag t                    ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_heartbeat",t  ) ; return s_tab[+t]->heartbeat(   ) ; }
	inline ::pair_s<HeartbeatState> Backend::s_heartbeat( Tag t , Job j            ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_heartbeat",t,j) ; return s_tab[+t]->heartbeat(j  ) ; }

}

#endif
