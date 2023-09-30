// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

		struct StartTabEntry {
			friend ::ostream& operator<<( ::ostream& , StartTabEntry const& ) ;
			struct Conn {
				friend ::ostream& operator<<( ::ostream& , Conn const& ) ;
				in_addr_t job_addr = 0 ;
				in_port_t job_port = 0 ;
				SeqId     seq_id   = 0 ;
				SmallId   small_id = 0 ;
			} ;
			// cxtors & casts
			StartTabEntry(       ) = default ;
			StartTabEntry(NewType) { open() ; }
			//
			// services
			void clear() {                                                     // retain n_retries so counting down number of retries goes on
				uint8_t nr = submit_attrs.n_retries ;
				*this = StartTabEntry() ;
				submit_attrs.n_retries = nr ;
			}
			void open() {
				conn.seq_id = (*Engine::g_seq_id)++ ;
			}
			Status lost() {
				if (submit_attrs.n_retries==0) return Status::Err ;
				submit_attrs.n_retries-- ;
				return Status::Lost ;
			}
			::pair<Pdate/*eta*/,bool/*keep_tmp*/> req_info() const ;
			// data
			Conn           conn         ;
			Pdate          start        ;
			::uset_s       washed       ;
			::vmap_ss      rsrcs        ;
			::uset<ReqIdx> reqs         ;
			SubmitAttrs    submit_attrs ;
			ConnState      state        = ConnState::New ; // if true <=> heartbeat has been seen
            Tag            tag          = Tag::Unknown   ;
		} ;

		struct DeferredReportEntry {
			friend ::ostream& operator<<( ::ostream& , DeferredReportEntry const& ) ;
			// data
			Pdate   date     ;         // date at which report must be displayed if job is not done yet
			SeqId   seq_id   ;
			JobExec job_exec ;
		} ;

		struct DeferredLostEntry {
			friend ::ostream& operator<<( ::ostream& , DeferredLostEntry const& ) ;
			// data
			Pdate  date   ;            // date at which job must be declared lost if it has not completed by then
			SeqId  seq_id ;
			JobIdx job    ;
		} ;

		// statics
		static bool s_is_local(Tag                                  ) ;
		static void s_config  (Config::Backend const config[+Tag::N]) ;
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
		static ::pair_s<uset<ReqIdx>> s_start( Tag t , JobIdx j            ) ; // sub-backend lock must have been takend by caller
		static ::string/*msg*/        s_end  ( Tag t , JobIdx j , Status s ) ; // .
		// called by heartbeat thread
		static ::vmap<JobIdx,pair_s<bool/*err*/>> s_heartbeat() ;
		//
	protected :
		static void s_register( Tag t , Backend& be ) {
			s_tab[+t] = &be ;
		}
	private :
		static void            _s_kill_req                   ( ReqIdx=0                                             ) ; // kill all if req==0
		static void            _s_wakeup_remote              ( JobIdx , StartTabEntry::Conn const& , JobExecRpcProc ) ;
		static bool/*keep_fd*/ _s_handle_job_req             ( JobRpcReq && , Fd={}                                 ) ;
		static void            _s_job_exec_thread_func       ( ::stop_token                                         ) ;
		static void            _s_heartbeat_thread_func      ( ::stop_token                                         ) ;
		static void            _s_deferred_report_thread_func( ::stop_token                                         ) ;
		static void            _s_deferred_lost_thread_func  ( ::stop_token                                         ) ;
		// static data
	public :
		static ::latch      s_service_ready    ;
		static ::string     s_executable       ;
		static Backend*     s_tab[/*+Tag::N*/] ;
		static ServerSockFd s_server_fd        ;
	private :
		static ::mutex                          _s_mutex                 ;
		static ::umap<JobIdx,StartTabEntry>     _s_start_tab             ;
		static SmallIds<SmallId>                _s_small_ids             ;
		static SmallId                          _s_max_small_id          ;
		static ThreadQueue<DeferredReportEntry> _s_deferred_report_queue ;
		static ThreadQueue<DeferredLostEntry  > _s_deferred_lost_queue   ;
	public :
		// services
		// PER_BACKEND : these virtual functions must be implemented by sub-backend, some of them have default implementations that do nothing when meaningful
		virtual bool is_local(                      ) const { return true ; }
		virtual void config  (Config::Backend const&)       {               }
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
		virtual void                   launch(              ) {}               // called to trigger launch of waiting jobs
		virtual ::pair_s<uset<ReqIdx>> start (JobIdx        ) = 0 ;            // inform job actually starts, return an informative message and reqs for which job has been launched
		virtual ::string               end   (JobIdx,Status) { return {} ; }   // inform job ended, return a message such as the stdout/stderr from slurm
		//
		virtual ::vmap<JobIdx,pair_s<bool/*err*/>> heartbeat() { return {} ; } // regularly called (~ every minute) to give opportunity to backend to check jobs...
		/**/                                                                   // (typically between launch and start), return lost jobs with error indications (message,is_error)
		//
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& /*rsrcs*/ , ::vmap_s<size_t> const& /*capacity*/ ) const { return {} ; } // map resources for this backend to local resources knowing local capacity
		//
		virtual ::vmap_s<size_t> const& capacity() const { FAIL("only for local backend") ; }
	protected :
		::vector_s acquire_cmd_line( Tag , JobIdx , ::vmap_ss&& rsrcs , SubmitAttrs const& ) ; // must be called once before job is launched, return job command line
		/**/                                                                                   // SubmitAttrs must be the operator| of the submit/add_pressure corresponding values for the job
		// data
	private :
		SmallIds<SmallId> _small_ids ;
	} ;

}
#endif
#ifdef IMPL
namespace Backends {

	inline bool Backend::s_is_local(Tag t) { return s_tab[+t]->is_local() ; }
	//
	// n_jobs is the maximum number of job backend may run on behalf of this req
	inline void Backend::s_open_req ( ReqIdx r , JobIdx n_jobs ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_open_req" ,r) ; for( Tag t : Tag::N ) if (s_tab[+t]) s_tab[+t]->open_req (r,n_jobs) ; }
	inline void Backend::s_close_req( ReqIdx r                 ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_close_req",r) ; for( Tag t : Tag::N ) if (s_tab[+t]) s_tab[+t]->close_req(r       ) ; }
	//
	inline void Backend::s_new_req_eta(ReqIdx req) { ::unique_lock lock{_s_mutex} ; Trace trace("s_new_req_eta",req) ; for( Tag t : Tag::N ) if (s_tab[+t]) s_tab[+t]->new_req_eta(req) ; }
	//
	inline ::pair_s<uset<ReqIdx>> Backend::s_start( Tag t , JobIdx j            ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_start",t,j) ; return s_tab[+t]->start(j  ) ; }
	inline ::string/*msg*/        Backend::s_end  ( Tag t , JobIdx j , Status s ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_end"  ,t,j) ; return s_tab[+t]->end  (j,s) ; }

}

#endif
