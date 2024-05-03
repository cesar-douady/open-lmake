// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_job.hh"

#ifdef STRUCT_DECL

// ENUM macro does not work inside namespace's

ENUM(ConnState
,	New
,	Old
,	Lost
)

ENUM(HeartbeatState
,	Alive
,	Lost
,	Err
)

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

	void send_reply( JobIdx , JobMngtRpcReply&& ) ;

	struct Backend {

		friend void send_reply( JobIdx , JobMngtRpcReply&& ) ;

		using SmallId     = uint32_t          ;
		using CoarseDelay = Time::CoarseDelay ;
		using Pdate       = Time::Pdate       ;
		using SigDate     = Disk::SigDate     ;

		struct StartEntry {
			friend ::ostream& operator<<( ::ostream& , StartEntry const& ) ;
			struct Conn {
				friend ::ostream& operator<<( ::ostream& , Conn const& ) ;
				// accesses
				bool operator+() const { return seq_id  ; }
				bool operator!() const { return !+*this ; }
				// data
				in_addr_t host     = NoSockAddr ;
				in_port_t port     = 0          ;
				SeqId     seq_id   = 0          ;
				SmallId   small_id = 0          ;
			} ;
			// cxtors & casts
			StartEntry(       ) = default ;
			StartEntry(NewType) { open() ; }
			// accesses
			bool operator+() const { return +conn   ; }
			bool operator!() const { return !+*this ; }
			bool useful   () const ;
			// services
			void open() {
				SWEAR(!*this) ;
				conn.seq_id = (*Engine::g_seq_id)++ ;
			}
			::pair<Pdate/*eta*/,bool/*keep_tmp*/> req_info() const ;
			// data
			Conn             conn         ;
			SigDate          start_date   ;
			::uset_s         washed       ;
			::vmap_ss        rsrcs        ;
			::vector<ReqIdx> reqs         ;
			SubmitAttrs      submit_attrs ;
			bool             old          = false        ; // becomes true the first time heartbeat passes (only old entries are checked by heartbeat, so first time is skipped for improved perf)
			Tag              tag          = Tag::Unknown ;
		} ;

		struct DeferredEntry {
			friend ::ostream& operator<<( ::ostream& , DeferredEntry const& ) ;
			DeferredEntry( SeqId si=0 , JobExec je={} ) : seq_id{si} , job_exec{je} {}
			// data
			SeqId   seq_id   = 0 ;
			JobExec job_exec ;
		} ;

		// statics
		static bool             s_is_local  (Tag) ;
		static bool             s_ready     (Tag) ;
		static ::string const&  s_config_err(Tag) ;
		static ::vmap_s<size_t> s_n_tokenss (Tag) ;
		//
		static void s_config( ::array<Config::Backend,N<Tag>> const& config , bool dynamic ) ;
		// sub-backend is responsible for job (i.e. answering to heart beat and kill) from submit to start
		// then it is top-backend that mangages it until end, at which point it is transfered back to engine
		// called from engine thread
		static void s_open_req    (                ReqIdx , JobIdx n_jobs                          ) ;
		static void s_close_req   (                ReqIdx                                          ) ;
		static void s_submit      ( Tag , JobIdx , ReqIdx , SubmitAttrs     && , ::vmap_ss&& rsrcs ) ;
		static void s_add_pressure( Tag , JobIdx , ReqIdx , SubmitAttrs const&                     ) ;
		static void s_set_pressure( Tag , JobIdx , ReqIdx , SubmitAttrs const&                     ) ;
		//
		static void s_kill_all   (          ) {              _s_kill_req(   ) ; }
		static void s_kill_req   (ReqIdx req) { SWEAR(req) ; _s_kill_req(req) ; }
		static void s_new_req_eta(ReqIdx    ) ;
		static void s_launch     (          ) ;
		// called by job_exec thread
		static ::string/*msg*/          s_start    ( Tag , JobIdx          ) ; // called by job_exec  thread, sub-backend lock must have been takend by caller
		static ::pair_s<bool/*retry*/>  s_end      ( Tag , JobIdx , Status ) ; // .
		static ::pair_s<HeartbeatState> s_heartbeat( Tag , JobIdx          ) ; // called by heartbeat thread, sub-backend lock must have been takend by caller
		//
	protected :
		static void s_register( Tag t , Backend& be ) {
			s_tab[+t] = &be ;
		}
	private :
		static void            _s_kill_req              ( ReqIdx=0                                                              ) ; // kill all if req==0
		static void            _s_wakeup_remote         ( JobIdx , StartEntry::Conn const& , SigDate const& start , JobMngtProc ) ;
		static void            _s_heartbeat_thread_func ( ::stop_token                                                          ) ;
		static bool/*keep_fd*/ _s_handle_job_start      ( JobRpcReq    && , SlaveSockFd const& ={}                              ) ;
		static bool/*keep_fd*/ _s_handle_job_mngt       ( JobMngtRpcReq&& , SlaveSockFd const& ={}                              ) ;
		static bool/*keep_fd*/ _s_handle_job_end        ( JobRpcReq    && , SlaveSockFd const& ={}                              ) ;
		static void            _s_handle_deferred_report( DeferredEntry&&                                                       ) ;
		static void            _s_handle_deferred_wakeup( DeferredEntry&&                                                       ) ;
		static Status          _s_release_start_entry   ( ::map<JobIdx,StartEntry>::iterator , Status                           ) ;
		//
		using JobThread      = ServerThread<JobRpcReq    > ;
		using JobMngtThread  = ServerThread<JobMngtRpcReq> ;
		using DeferredThread = QueueThread <DeferredEntry> ;
		// static data
	public :
		static ::string s_executable  ;
		static Backend* s_tab[N<Tag>] ;

	private :
		static JobThread     *           _s_job_start_thread       ;
		static JobMngtThread *           _s_job_mngt_thread        ;
		static JobThread     *           _s_job_end_thread         ;
		static DeferredThread*           _s_deferred_report_thread ;
		static DeferredThread*           _s_deferred_wakeup_thread ;
		static Mutex<MutexLvl::Backend>  _s_mutex                  ;
		static ::atomic<JobIdx>          _s_starting_job           ;                                      // this job is starting when _starting_job_mutex is locked
		static Mutex<MutexLvl::StartJob> _s_starting_job_mutex     ;
		static ::map<JobIdx,StartEntry>  _s_start_tab              ;                                      // use map instead of umap because heartbeat iterates over while tab is moving
		static SmallIds<SmallId>         _s_small_ids              ;
		static SmallId                   _s_max_small_id           ;
	public :
		// services
		// PER_BACKEND : these virtual functions must be implemented by sub-backend, some of them have default implementations that do nothing when meaningful
		virtual bool             is_local (                                     ) const { return true ; }
		virtual ::vmap_ss        descr    (                                     ) const { return {}   ; }
		virtual ::vmap_s<size_t> n_tokenss(                                     ) const { return {}   ; }
		virtual void             config   ( ::vmap_ss const& , bool /*dynamic*/ )       {               }
		//
		virtual void             open_req         ( ReqIdx   , JobIdx /*n_jobs*/ ) {}                     // called before any operation on req , n_jobs is the max number of jobs that can be launched
		virtual void             new_req_eta      ( ReqIdx                       ) {}                     // inform backend that req has a new eta, which may change job priorities
		virtual void             close_req        ( ReqIdx                       ) {}                     // called after all operations on req (only open is legal after that)
		virtual ::vector<JobIdx> kill_waiting_jobs( ReqIdx=0                     ) = 0 ;                  // kill all waiting jobs for this req (all if 0), return killed jobs
		virtual void             kill_job         ( JobIdx                       ) = 0 ;                  // job must be spawned
		//
		virtual void submit      ( JobIdx , ReqIdx , SubmitAttrs const& , ::vmap_ss&& /*rsrcs*/ ) = 0 ;   // submit a new job
		virtual void add_pressure( JobIdx , ReqIdx , SubmitAttrs const&                         ) {}      // add a new req for an already submitted job
		virtual void set_pressure( JobIdx , ReqIdx , SubmitAttrs const&                         ) {}      // set a new pressure for an existing req of a job
		//
		virtual void                     launch   (             ) = 0 ;                                   // called to trigger launch of waiting jobs
		virtual ::string/*msg*/          start    (JobIdx       ) = 0 ;                                   // tell sub-backend job started, return an informative message
		virtual ::pair_s<bool/*retry*/>  end      (JobIdx,Status) { return {}                         ; } // tell sub-backend job ended, return a message and whether to retry jobs with garbage status
		virtual ::pair_s<HeartbeatState> heartbeat(JobIdx       ) { return {{},HeartbeatState::Alive} ; } // regularly called between launch and start, initially with enough delay for job to connect
		//
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& /*rsrcs*/ , ::vmap_s<size_t> const& /*capacity*/ ) const { return {} ; }   // map resources for this backend to local resources knowing local capacity
		//
		virtual ::vmap_s<size_t> const& capacity() const { FAIL("only for local backend") ; }
	protected :
		::vector_s acquire_cmd_line( Tag , JobIdx , ::vector<ReqIdx> const& , ::vmap_ss&& rsrcs , SubmitAttrs const& ) ; // must be called once before job is launched, SubmitAttrs must be the ...
		/**/                                                                                                             // ... operator| of the submit/add_pressure corresponding values for the job
		// data
		in_addr_t addr       = NoSockAddr ;
		::string  config_err ;
	} ;

}

#endif
#ifdef IMPL

namespace Backends {

	inline bool             Backend::s_is_local  (Tag t) { return                     s_tab[+t]->is_local()  ; }
	inline bool             Backend::s_ready     (Tag t) { return +t && s_tab[+t] && !s_tab[+t]->config_err  ; }
	inline ::string const&  Backend::s_config_err(Tag t) { return                     s_tab[+t]->config_err  ; }
	inline ::vmap_s<size_t> Backend::s_n_tokenss (Tag t) { return                     s_tab[+t]->n_tokenss() ; }
	//
	// nj is the maximum number of job backend may run on behalf of this req
	#define LOCK Lock lock{_s_mutex}
	inline void Backend::s_open_req   (ReqIdx r,JobIdx nj) { LOCK ; Trace trace(BeChnl,"s_open_req"   ,r) ; for( Tag t : All<Tag> ) if (s_ready(t)) s_tab[+t]->open_req   (r,nj) ; }
	inline void Backend::s_close_req  (ReqIdx r          ) { LOCK ; Trace trace(BeChnl,"s_close_req"  ,r) ; for( Tag t : All<Tag> ) if (s_ready(t)) s_tab[+t]->close_req  (r   ) ; }
	inline void Backend::s_new_req_eta(ReqIdx r          ) { LOCK ; Trace trace(BeChnl,"s_new_req_eta",r) ; for( Tag t : All<Tag> ) if (s_ready(t)) s_tab[+t]->new_req_eta(r   ) ; }
	#undef LOCK
	//
	inline ::string/*msg*/          Backend::s_start    ( Tag t , JobIdx j            ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_start"    ,t,j) ; return s_tab[+t]->start    (j  ) ; }
	inline ::pair_s<bool/*retry*/>  Backend::s_end      ( Tag t , JobIdx j , Status s ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_end"      ,t,j) ; return s_tab[+t]->end      (j,s) ; }
	inline ::pair_s<HeartbeatState> Backend::s_heartbeat( Tag t , JobIdx j            ) { _s_mutex.swear_locked() ; Trace trace(BeChnl,"s_heartbeat",t,j) ; return s_tab[+t]->heartbeat(j  ) ; }

	inline bool Backend::StartEntry::useful() const {
		for( Req r : reqs ) if (!r.zombie()) return true ;
		return false ;
	}
}

#endif
