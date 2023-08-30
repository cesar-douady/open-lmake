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

	struct Backend {
		using SmallId     = uint32_t          ;
		using CoarseDelay = Time::CoarseDelay ;
		using Date        = Time::ProcessDate ;

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
			void open() {
				conn.seq_id = (*Engine::g_seq_id)++ ;
			}
			// data
			Conn           conn         ;
			Date           start        ;
			::uset_s       washed       ;
			::vmap_ss      rsrcs        ;
			::uset<ReqIdx> reqs         ;
			SubmitAttrs    submit_attrs ;
			bool           old          = false        ;   // if true <=> heartbeat has been seen
            Tag            tag          = Tag::Unknown ;
		} ;

		struct DeferredReportEntry {
			Date    date     ;         // date at which report must be displayed if job is not done yet
			SeqId   seq_id   ;
			JobExec job_exec ;
		} ;

		struct DeferredLostEntry {
			Date   date   ;            // date at which job must be declared lost if it has not completed by then
			SeqId  seq_id ;
			JobIdx job    ;
		} ;

		// statics
		static void s_config(Config::Backend const config[]) ;
		// sub-backend is responsible for job (i.e. answering to heart beat and kill) from submit to start
		// then it is top-backend that mangages it until end, at which point it is transfered back to engine
		// called from engine thread
		static void s_open_req    (                ReqIdx , JobIdx n_jobs                               ) ;
		static void s_close_req   (                ReqIdx                                               ) ;
		static void s_submit      ( Tag , JobIdx , ReqIdx , SubmitAttrs     && , ::vmap_ss const& rsrcs ) ;
		static void s_add_pressure( Tag , JobIdx , ReqIdx , SubmitAttrs const&                          ) ;
		static void s_set_pressure( Tag , JobIdx , ReqIdx , SubmitAttrs const&                          ) ;
		//
		static void s_kill_all   (          ) {              _s_kill_req(   ) ; }
		static void s_kill_req   (ReqIdx req) { SWEAR(req) ; _s_kill_req(req) ; }
		static void s_new_req_eta(ReqIdx    ) ;
		static void s_launch     (          ) ;
		// called by job_exec thread
		static ::uset<ReqIdx> s_start( Tag t , JobIdx j ) ;                    // sub-backend lock must have been takend by caller
		static void           s_end  ( Tag t , JobIdx j ) ;                    // .
		// called by heartbeat thread
		static ::vector<JobIdx> s_heartbeat() ;
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
		virtual void config(Config::Backend const&) {}
		//
		virtual void           open_req   ( ReqIdx   , JobIdx /*n_jobs*/ ) {}    // called before any operation on req , n_jobs is the maximum number of jobs that can be launched (lmake -j option)
		virtual void           new_req_eta( ReqIdx                       ) {}    // inform backend that req has a new eta, which may change job priorities
		virtual void           close_req  ( ReqIdx                       ) {}    // called after any operation on req
		virtual ::uset<JobIdx> kill_req   ( ReqIdx=0                     ) = 0 ; // kill all if req==0, return killed jobs
		//
		virtual void submit      ( JobIdx , ReqIdx , SubmitAttrs const& , ::vmap_ss const& /*rsrcs*/ ) = 0 ; // submit a new job
		virtual void add_pressure( JobIdx , ReqIdx , SubmitAttrs const&                              ) {}    // add a new req for an already submitted job
		virtual void set_pressure( JobIdx , ReqIdx , SubmitAttrs const&                              ) {}    // set a new pressure for an existing req of a job
		//
		virtual void           launch(      ) {}                               // called to trigger launch of waiting jobs
		virtual ::uset<ReqIdx> start (JobIdx) = 0 ;                            // inform job actually starts, return reqs for which job has been launched
		virtual void           end   (JobIdx) {}                               // inform job ended
		//
		virtual ::vector<JobIdx> heartbeat() { return {} ; }                   // regularly called (~ every minute) to give opportunity to backend to check jobs (typicallay between launch and start)
		/**/                                                                   // return lost jobs
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

	inline void Backend::s_open_req ( ReqIdx r , JobIdx n_jobs ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_open_req" ,r) ; for( Tag t : Tag::N ) s_tab[+t]->open_req (r,n_jobs) ; }
	inline void Backend::s_close_req( ReqIdx r                 ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_close_req",r) ; for( Tag t : Tag::N ) s_tab[+t]->close_req(r       ) ; }
	//
	inline void Backend::s_submit( Tag t , JobIdx j , ReqIdx r , SubmitAttrs&& sa , ::vmap_ss const& rs ) {
		::unique_lock lock{_s_mutex} ;
		//
		sa.tag = t ;
		Trace trace("s_submit",t,j,r,sa,rs) ;
		s_tab[+t]->submit(j,r,sa,rs) ;
	}
	//
	inline void Backend::s_add_pressure( Tag t , JobIdx j , ReqIdx r , SubmitAttrs const& sa ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_add_pressure",t,j,r,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) {
			s_tab[+t]->add_pressure(j,r,sa) ;                               // if job is not started, ask sub-backend to raise its priority
		} else {
			it->second.reqs.insert(r) ;                                        // else, job is already started, note the new Req as we maintain the list of Req's associated to each job
			it->second.submit_attrs |= sa ;                                    // and update submit_attrs in case job was not actually started
		}
	}
	inline void Backend::s_set_pressure( Tag t , JobIdx j , ReqIdx r , SubmitAttrs const& sa ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_set_pressure",t,j,r,sa) ;
		s_tab[+t]->set_pressure(j,r,sa) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->set_pressure(j,r,sa) ;       // if job is not started, ask sub-backend to raise its priority
		else                        it->second.submit_attrs |= sa ;            // and update submit_attrs in case job was not actually started
	}
	//
	inline void Backend::s_new_req_eta(ReqIdx req) { ::unique_lock lock{_s_mutex} ; Trace trace("s_new_req_eta",req) ; for( Tag t : Tag::N ) s_tab[+t]->new_req_eta(req) ; }
	//
	inline ::uset<ReqIdx> Backend::s_start( Tag t , JobIdx j ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_start",t,j) ; return s_tab[+t]->start(j) ; }
	inline void           Backend::s_end  ( Tag t , JobIdx j ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_end"  ,t,j) ;        s_tab[+t]->end  (j) ; }

}

#endif
