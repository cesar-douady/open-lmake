// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_job.hh"

#ifdef STRUCT_DECL
namespace Backends {

	struct Backend ;

	using namespace Engine ;

	using Tag = BackendTag ;

}
using Backends::Backend ;
#endif
#ifdef DATA_DEF
namespace Backends {

	ENUM(ConnState
	,	New
	,	Old
	,	Lost
	)

	struct Backend {
		using SmallId     = uint32_t          ;
		using CoarseDelay = Time::CoarseDelay ;
		using Pdate       = Time::Pdate       ;

		struct StartEntry {
			friend ::ostream& operator<<( ::ostream& , StartEntry const& ) ;
			struct Conn {
				friend ::ostream& operator<<( ::ostream& , Conn const& ) ;
				in_addr_t host     = NoSockAddr ;
				in_port_t port     = 0          ;
				SeqId     seq_id   = 0          ;
				SmallId   small_id = 0          ;
			} ;
			// cxtors & casts
			StartEntry(       ) = default ;
			StartEntry(NewType) { open() ; }
			// accesses
			bool operator+() const { return conn.seq_id ; }
			bool operator!() const { return !+*this     ; }
			// services
			void open() {
				SWEAR(!*this) ;
				conn.seq_id = (*Engine::g_seq_id)++ ;
			}
			::pair<Pdate/*eta*/,bool/*keep_tmp*/> req_info() const ;
			// data
			Conn           conn         ;
			Pdate          start        ;
			::uset_s       washed       ;
			::vmap_ss      rsrcs        ;
			::uset<ReqIdx> reqs         ;
			SubmitAttrs    submit_attrs ;
			bool           old          = false        ;   // becomes true the first time heartbeat passes (only old entries are checked by heartbeat, so first time is skipped for improved perf)
            Tag            tag          = Tag::Unknown ;
		} ;

		struct DeferredReportEntry {
			friend ::ostream& operator<<( ::ostream& , DeferredReportEntry const& ) ;
			// data
			SeqId   seq_id   = 0 ;
			JobExec job_exec ;
		} ;

		// statics
		static bool s_is_local(Tag                                  ) ;
		static void s_config  (Config::Backend const config[+Tag::N]) ;
		// sub-backend is responsible for job (i.e. answering to heart beat and kill) from submit to start
		// then it is top-backend that mangages it until end, at which point it is transfered back to engine
		// called from engine thread
		static void                    s_open_req    (                    ReqIdx , JobIdx n_jobs                          ) ;
		static void                    s_close_req   (                    ReqIdx                                          ) ;
		static void                    s_submit      ( Tag   , JobIdx   , ReqIdx , SubmitAttrs     && , ::vmap_ss&& rsrcs ) ;
		static void                    s_add_pressure( Tag   , JobIdx   , ReqIdx , SubmitAttrs const&                     ) ;
		static void                    s_set_pressure( Tag   , JobIdx   , ReqIdx , SubmitAttrs const&                     ) ;
		static ::pair_s<bool/*retry*/> s_lost_info   ( Tag t , JobIdx j                                                   ) { ::unique_lock lock{_s_mutex} ; return s_tab[+t]->lost_info(j) ; }
		//
		static void s_kill_all   (          ) {              _s_kill_req(   ) ; }
		static void s_kill_req   (ReqIdx req) { SWEAR(req) ; _s_kill_req(req) ; }
		static void s_new_req_eta(ReqIdx    ) ;
		static void s_launch     (          ) ;
		// called by job_exec thread
		static ::pair_s<uset<ReqIdx>> s_start    ( Tag , JobIdx          ) ; // called by job_exec  thread, sub-backend lock must have been takend by caller
		static ::string               s_end      ( Tag , JobIdx , Status ) ; // .
		static ::pair_s<Bool3/*ok*/>  s_heartbeat( Tag , JobIdx          ) ; // called by heartbeat thread, sub-backend lock must have been takend by caller
		//
	protected :
		static void s_register( Tag t , Backend& be ) {
			s_tab[+t] = &be ;
		}
	private :
		static void            _s_kill_req              ( ReqIdx=0                                                  ) ; // kill all if req==0
		static void            _s_wakeup_remote         ( Tag , JobIdx , StartEntry::Conn const& , JobServerRpcProc ) ;
		static void            _s_heartbeat_thread_func ( ::stop_token                                              ) ;
		static bool/*keep_fd*/ _s_handle_job_req        ( JobRpcReq && , Fd={}                                      ) ;
		static void            _s_handle_deferred_report( DeferredReportEntry&&                                     ) ;
		static Status          _s_release_start_entry   ( ::map<JobIdx,StartEntry>::iterator , Status               ) ;
		//
		using JobExecThread        = ServerThread<JobRpcReq          > ;
		using DeferredReportThread = QueueThread <DeferredReportEntry> ;
		// static data
	public :
		static ::string       s_executable     ;
		static Backend*       s_tab  [+Tag::N] ;
		static ::atomic<bool> s_ready[+Tag::N] ;

	private :
		static JobExecThread       *    _s_job_exec_thread        ;
		static DeferredReportThread*    _s_deferred_report_thread ;
		static ::mutex                  _s_mutex                  ;
		static ::map<JobIdx,StartEntry> _s_start_tab              ;            // use map instead of umap because heartbeat iterates over while tab is moving
		static SmallIds<SmallId>        _s_small_ids              ;
		static SmallId                  _s_max_small_id           ;
	public :
		// services
		// PER_BACKEND : these virtual functions must be implemented by sub-backend, some of them have default implementations that do nothing when meaningful
		virtual bool       is_local(                      ) const { return true ; }
		virtual bool/*ok*/ config  (Config::Backend const&)       { return true ; }
		//
		virtual void           open_req   ( ReqIdx   , JobIdx /*n_jobs*/ ) {}    // called before any operation on req , n_jobs is the maximum number of jobs that can be launched (lmake -j option)
		virtual void           new_req_eta( ReqIdx                       ) {}    // inform backend that req has a new eta, which may change job priorities
		virtual void           close_req  ( ReqIdx                       ) {}    // called after any operation on req
		virtual ::uset<JobIdx> kill_req   ( ReqIdx=0                     ) = 0 ; // kill all if req==0, return killed jobs
		//
		virtual void submit      ( JobIdx , ReqIdx , SubmitAttrs const& , ::vmap_ss&& /*rsrcs*/ ) = 0 ; // submit a new job
		virtual void add_pressure( JobIdx , ReqIdx , SubmitAttrs const&                         ) {}    // add a new req for an already submitted job
		virtual void set_pressure( JobIdx , ReqIdx , SubmitAttrs const&                         ) {}    // set a new pressure for an existing req of a job
		//
		virtual void                   launch   (             ) = 0 ;                 // called to trigger launch of waiting jobs
		virtual ::pair_s<uset<ReqIdx>> start    (JobIdx       ) = 0 ;                 // tell sub-backend job started, return an informative message and reqs associated with job
		virtual ::string               end      (JobIdx,Status) { return {}       ; } // tell sub-backend job ended  , return a message such as the stdout/stderr from slurm
		virtual ::pair_s<Bool3/*ok*/>  heartbeat(JobIdx       ) { return {{},Yes} ; } // regularly called between launch and start, Yes => ok, Maybe => job lost, No => job error
		//
		virtual ::pair_s<bool/*retry*/> lost_info(JobIdx) { return {"vanished after start",true/*retry*/} ; } // ask sub-backend for an error message explaining why job was lost and whether to retry
		//
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& /*rsrcs*/ , ::vmap_s<size_t> const& /*capacity*/ ) const { return {} ; } // map resources for this backend to local resources knowing local capacity
		//
		virtual ::vmap_s<size_t> const& capacity() const { FAIL("only for local backend") ; }
	protected :
		::vector_s acquire_cmd_line( Tag , JobIdx , ::vmap_ss&& rsrcs , SubmitAttrs const& ) ; // must be called once before job is launched, return job command line
		/**/                                                                                   // SubmitAttrs must be the operator| of the submit/add_pressure corresponding values for the job
	} ;

}
#endif
#ifdef IMPL
namespace Backends {

	inline bool Backend::s_is_local(Tag t) { return s_tab[+t]->is_local() ; }
	//
	// nj is the maximum number of job backend may run on behalf of this req
	inline void Backend::s_open_req   (ReqIdx r,JobIdx nj) { ::unique_lock lock{_s_mutex} ; Trace trace("s_open_req"   ,r) ; for( Tag t : Tag::N ) if (s_ready[+t]) s_tab[+t]->open_req   (r,nj) ; }
	inline void Backend::s_close_req  (ReqIdx r          ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_close_req"  ,r) ; for( Tag t : Tag::N ) if (s_ready[+t]) s_tab[+t]->close_req  (r   ) ; }
	inline void Backend::s_new_req_eta(ReqIdx r          ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_new_req_eta",r) ; for( Tag t : Tag::N ) if (s_ready[+t]) s_tab[+t]->new_req_eta(r   ) ; }
	//
	inline ::pair_s<uset<ReqIdx>> Backend::s_start    ( Tag t , JobIdx j            ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_start"    ,t,j) ; return s_tab[+t]->start    (j  ) ; }
	inline ::string               Backend::s_end      ( Tag t , JobIdx j , Status s ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_end"      ,t,j) ; return s_tab[+t]->end      (j,s) ; }
	inline ::pair_s<Bool3/*ok*/>  Backend::s_heartbeat( Tag t , JobIdx j            ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_heartbeat",t,j) ; return s_tab[+t]->heartbeat(j  ) ; }

}

#endif
