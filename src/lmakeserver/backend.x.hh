// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "trace.hh"
#include "time.hh"
#include "pycxx.hh"

#include "config.hh"
#include "rpc_job.hh"

#ifdef STRUCT_DECL
namespace Backends {
	struct Backend ;
	using ServerConfig = Engine::ServerConfig    ;
	using Tag          = Engine::ServerConfigTag ;
}
using Backends::Backend ;
#endif
#ifdef STRUCT_DEF
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
			Conn           conn   ;
			JobReason      reason ;
			bool           old    = false ;                                    // if true <=> heartbeat has been seen
			Date           start  ;
			::uset_s       washed ;
			::vmap_ss      rsrcs  ;
			::uset<ReqIdx> reqs   ;
		} ;

		struct DeferredEntry {
			DeferredEntry() = default ;
			DeferredEntry( Date d , JobIdx j , SeqId si ) : date{d} , job{j} , seq_id{si} {}
			// data
			Date   date   ;
			JobIdx job    ;
			SeqId  seq_id ;
		} ;

		// statics
		static void s_config(ServerConfig::Backend const config[]) ;
		// sub-backend is responsible for job (i.e. answering to heart beat and kill) from submit to start
		// then it is top-backend that mangages it until end, at which point it is transfered back to engine
		// called from engine thread
		static void s_open_req    (                    ReqIdx                                                     ) ;
		static void s_close_req   (                    ReqIdx                                                     ) ;
		static void s_submit      ( Tag t , JobIdx j , ReqIdx r , CoarseDelay p , ::vmap_ss const& rs , JobReason ) ;
		static void s_add_pressure( Tag t , JobIdx j , ReqIdx r , CoarseDelay p                                   ) ;
		static void s_set_pressure( Tag t , JobIdx j , ReqIdx r , CoarseDelay p                                   ) ;
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
		static void            _s_kill_req             ( ReqIdx=0                                             ) ; // kill all if req==0
		static void            _s_wakeup_remote        ( JobIdx , StartTabEntry::Conn const& , JobExecRpcProc ) ;
		static bool/*keep_fd*/ _s_handle_job_req       ( Fd , JobRpcReq &&                                    ) ;
		static void            _s_job_exec_thread_func ( ::stop_token                                         ) ;
		static void            _s_heartbeat_thread_func( ::stop_token                                         ) ;
		static void            _s_deferred_thread_func ( ::stop_token                                         ) ;
		// static data
	public :
		static ::latch      s_service_ready    ;
		static ::string     s_executable       ;
		static Backend*     s_tab[/*+Tag::N*/] ;
		static ServerSockFd s_server_fd        ;
	private :
		static ::mutex                      _s_mutex          ;
		static ::umap<JobIdx,StartTabEntry> _s_start_tab      ;
		static SmallIds<SmallId>            _s_small_ids      ;
		static SmallId                      _s_max_small_id   ;
		static ThreadQueue<DeferredEntry>   _s_deferred_queue ;
		// services
		// PER_BACKEND : these virtual functions must be implemented by sub-backend, some of them have default implementations that do nothing when meaningful
	public :
		virtual void config(ServerConfig::Backend const&) {}
		//
		virtual void             open_req    (          ReqIdx                                                                       ) {}
		virtual void             close_req   (          ReqIdx                                                                       ) {}
		virtual void             submit      ( JobIdx , ReqIdx   , CoarseDelay /*pressure*/ , ::vmap_ss const& /*rsrcs*/ , JobReason ) = 0 ;
		virtual void             add_pressure( JobIdx , ReqIdx   , CoarseDelay /*pressure*/                                          ) {}    // add a new req for a job
		virtual void             set_pressure( JobIdx , ReqIdx   , CoarseDelay /*pressure*/                                          ) {}    // set a new pressure for an existing req of a job
		virtual void             new_req_eta (          ReqIdx                                                                       ) {}
		virtual ::uset  <JobIdx> kill_req    (          ReqIdx=0                                                                     ) = 0 ; // kill all if req==0
		virtual void             launch      (                                                                                       ) {}
		virtual ::uset<ReqIdx>   start       ( JobIdx                                                                                ) = 0 ;
		virtual void             end         ( JobIdx                                                                                ) {}
		virtual ::vector<JobIdx> heartbeat   (                                                                                       ) { return {} ; }
	protected :
		::vector_s acquire_cmd_line( Tag , JobIdx , ::vmap_ss&& rsrcs , JobReason ) ;
		// data
	private :
		SmallIds<SmallId> _small_ids ;
	} ;

	inline void Backend::s_open_req (ReqIdx req) { ::unique_lock lock{_s_mutex} ; Trace trace("s_open_req" ,req) ; for( Tag t : Tag::N ) s_tab[+t]->open_req (req) ; }
	inline void Backend::s_close_req(ReqIdx req) { ::unique_lock lock{_s_mutex} ; Trace trace("s_close_req",req) ; for( Tag t : Tag::N ) s_tab[+t]->close_req(req) ; }
	//
	inline void Backend::s_submit( Tag t , JobIdx j , ReqIdx r , CoarseDelay p , ::vmap_ss const& rs , JobReason jr ) {
		::unique_lock lock{_s_mutex} ;
		//
		Trace trace("s_submit",t,j,r,p,rs,jr) ;
		s_tab[+t]->submit(j,r,p,rs,jr) ;
	}
	//
	inline void Backend::s_add_pressure( Tag t , JobIdx j , ReqIdx r , CoarseDelay p ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_add_pressure",t,j,r,p) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->add_pressure(j,r,p) ;           // if job is not started, ask sub-backend to raise its priority
		else                        it->second.reqs.insert(r) ;                // else, job is already started, note the new Req as we maintain the list of Req's associated to each job
	}
	inline void Backend::s_set_pressure( Tag t , JobIdx j , ReqIdx r , CoarseDelay p ) {
		::unique_lock lock{_s_mutex} ;
		Trace trace("s_set_pressure",t,j,r,p) ;
		s_tab[+t]->set_pressure(j,r,p) ;
		auto it = _s_start_tab.find(j) ;
		if (it==_s_start_tab.end()) s_tab[+t]->set_pressure(j,r,p) ;           // if job is not started, ask sub-backend to raise its priority
		/**/                                                                   // else, job is already started, nothing to do
	}
	//
	inline void Backend::s_new_req_eta(ReqIdx req) { ::unique_lock lock{_s_mutex} ; Trace trace("s_new_req_eta",req) ; for( Tag t : Tag::N ) s_tab[+t]->new_req_eta(req) ; }
	inline void Backend::s_launch     (          ) { ::unique_lock lock{_s_mutex} ; Trace trace("s_launch"         ) ; for( Tag t : Tag::N ) s_tab[+t]->launch     (   ) ; }
	//
	inline ::uset<ReqIdx> Backend::s_start( Tag t , JobIdx j ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_start",t,j) ; return s_tab[+t]->start(j) ; }
	inline void           Backend::s_end  ( Tag t , JobIdx j ) { SWEAR(!_s_mutex.try_lock()) ; Trace trace("s_end"  ,t,j) ;        s_tab[+t]->end  (j) ; }
	//
	inline ::vector<JobIdx> Backend::s_heartbeat() {
		::vector<JobIdx> res  ;
		::unique_lock    lock { _s_mutex } ;
		//
		Trace trace("s_heartbeat") ;
		for( Tag t : Tag::N ) {
			if (!s_tab[+t]) continue ;                                                    // if s_tab is not initialized yet (we are called from an async thread), no harm, just skip
			if (res.empty()) res =           s_tab[+t]->heartbeat() ;                     // fast path
			else             for( JobIdx j : s_tab[+t]->heartbeat() ) res.push_back(j) ;
		}
		trace("jobs",res) ;
		return res ;
	}

}

#endif
