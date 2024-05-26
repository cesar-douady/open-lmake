// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

// XXX : rework to maintain an ordered list of waiting_queues in ReqEntry to avoid walking through all rsrcs for each launched job

// a job may have 3 states :
// - waiting : job has been submitted and is retained here until we can spawn it
// - queued  : job has been spawned but has not yet started
// - started : job has started
// spawned means queued or started

namespace Backends {

	//
	// Shared
	//

	// share actual resources data as we typically have a lot of jobs with the same resources
	template< class Data , ::unsigned_integral RefCnt > struct Shared {
		friend ostream& operator<<( ostream& os , Shared const& s ) {
			/**/           os << "Shared" ;
			if (+s) return os << *s       ;
			else    return os << "()"     ;
		}
		// static data
	private :
		static ::umap<Data,RefCnt> _s_store ;                                   // map rsrcs to refcount, always >0 (erased when reaching 0)
		// cxtors & casts
	public :
		Shared() = default ;
		//
		Shared(Shared const& r) : data{r.data} { if (data) _s_store.at(*data)++ ; }
		Shared(Shared     && r) : data{r.data} { r.data = nullptr               ; }
		//
		template<class... A> Shared( NewType , A&&... args) {
			Data d  { ::forward<A>(args)... } ;
			auto it = _s_store.find(d)        ;
			if (it==_s_store.end()) it = _s_store.insert({::move(d),1}).first ; // data is not known, create it
			else                    it->second++ ;                              // data is known, share and increment refcount
			data = &it->first ;
		}
		//
		~Shared() {
			if (!data) return ;
			auto it = _s_store.find(*data) ;
			SWEAR(it!=_s_store.end()) ;
			if (it->second==1) _s_store.erase(it) ;                             // last pointer, destroy data
			else               it->second--       ;                             // data is shared, just decrement refcount
		}
		//
		Shared& operator=(Shared s) { swap(*this,s) ; return *this ; }
		//
		bool operator==(Shared const&) const = default ;
		// access
		Data const& operator* () const { return *data   ; }
		Data const* operator->() const { return &**this ; }
		bool        operator+ () const { return data    ; }
		bool        operator! () const { return !+*this ; }
		// data
		Data const* data = nullptr ;
	} ;
	template< class Data , ::unsigned_integral RefCnt > ::umap<Data,RefCnt> Shared<Data,RefCnt>::_s_store ;
	template< class Data , ::unsigned_integral RefCnt > void swap( Shared<Data,RefCnt>& s1 , Shared<Data,RefCnt>& s2 ) {
		::swap(s1.data,s2.data) ;
	}

}

namespace std {
	template< class Data , ::unsigned_integral RefCnt > struct hash<Backends::Shared<Data,RefCnt>> {
		size_t operator()(Backends::Shared<Data,RefCnt> const& s) const {
			return hash<Data const*>()(s.data) ;
		}
	} ;
}

namespace Backends {

	template<class I> I from_string_rsrc( ::string const& k , ::string const& v ) {
		if ( k=="mem" || k=="tmp" ) return from_string_with_units<'M',I>(v) ;
		else                        return from_string_with_units<    I>(v) ;
	}

	template<class I> ::string to_string_rsrc( ::string const& k , I v ) {
		if ( k=="mem" || k=="tmp" ) return to_string_with_units<'M'>(v) ;
		else                        return to_string_with_units     (v) ;
	}

	//
	// GenericBackend
	//

	// we could maintain a list of reqs sorted by eta as we have open_req to create entries, close_req to erase them and new_req_eta to reorder them upon need
	// but this is too heavy to code and because there are few reqs and probably most of them have local jobs if there are local jobs at all, the perf gain would be marginal, if at all
	template< Tag T , class SpawnId , class RsrcsData , class RsrcsDataAsk , bool IsLocal > struct GenericBackend : Backend {

		using Rsrcs    = Shared<RsrcsData   ,JobIdx> ;
		using RsrcsAsk = Shared<RsrcsDataAsk,JobIdx> ;

		struct WaitingEntry {
			WaitingEntry() = default ;
			WaitingEntry( RsrcsAsk const& rsa , SubmitAttrs const& sa , bool v ) : rsrcs_ask{rsa} , n_reqs{1} , submit_attrs{sa} , verbose{v} {}
			// data
			RsrcsAsk    rsrcs_ask    ;
			ReqIdx      n_reqs       = 0     ; // number of reqs waiting for this job
			SubmitAttrs submit_attrs ;
			bool        verbose      = false ;
		} ;

		struct SpawnedEntry {
			SpawnedEntry( Rsrcs rsrcs_ , bool v  ) : rsrcs{rsrcs_  } ,                                          verbose{v         }                     {}
			SpawnedEntry( SpawnedEntry const& se ) : rsrcs{se.rsrcs} , id{se.id.load()} , started{se.started} , verbose{se.verbose} , zombie{se.zombie} {}
			Rsrcs             rsrcs   ;
			::atomic<SpawnId> id      = 0     ;
			bool              started = false ; // if true <=> start() has been called for this job, for assert only
			bool              verbose = false ;
			bool              zombie  = false ; // entry waiting for suppression
		} ;
		struct SpawnedTab : ::umap<JobIdx,SpawnedEntry> {
			using Base = ::umap<JobIdx,SpawnedEntry> ;
			using typename Base::iterator ;
			void erase(iterator it) {
				if (it->second.id)   Base::erase(it) ;
				else               { it->second.zombie = true ; it->second.rsrcs = {} ; }
			} ;
			void flush(iterator it) {
				if (it->second.zombie) Base::erase(it) ;
			}
		} ;

		struct PressureEntry {
			// services
			bool              operator== (PressureEntry const&      ) const = default ;
			::strong_ordering operator<=>(PressureEntry const& other) const {
				if (pressure!=other.pressure) return other.pressure<=>pressure  ; // higher pressure first
				else                          return job           <=>other.job ;
			}
			// data
			CoarseDelay pressure ;
			JobIdx      job      ;
		} ;

		struct ReqEntry {
			ReqEntry() = default ;
			ReqEntry( JobIdx nj , bool v ) : n_jobs{nj} , verbose{v} {}
			// service
			void clear() {
				waiting_queues.clear() ;
				waiting_jobs  .clear() ;
				queued_jobs   .clear() ;
			}
			// data
			::umap<RsrcsAsk,set<PressureEntry>> waiting_queues ;
			::umap<JobIdx,CoarseDelay         > waiting_jobs   ;
			::uset<JobIdx                     > queued_jobs    ;         // spawned jobs until start
			JobIdx                              n_jobs         = 0     ; // manage -j option (if >0 no more than n_jobs can be launched on behalf of this req)
			bool                                verbose        = false ;
		} ;

		// specialization
		virtual void sub_config( vmap_ss const& , bool /*dynamic*/ ) {}
		//
		virtual bool call_launch_after_start() const { return false ; }
		virtual bool call_launch_after_end  () const { return false ; }
		//
		virtual bool/*ok*/   fit_eventually( RsrcsDataAsk const&          ) const { return true ; } // true if job with such resources can be spawned eventually
		virtual bool/*ok*/   fit_now       ( RsrcsAsk     const&          ) const = 0 ;             // true if job with such resources can be spawned now
		virtual Rsrcs        acquire_rsrcs ( RsrcsAsk     const&          ) const = 0 ;             // acquire maximum possible asked resources
		virtual ::vmap_ss    export_       ( RsrcsData    const&          ) const = 0 ;             // export resources in   a publicly manageable form
		virtual RsrcsDataAsk import_       ( ::vmap_ss        && , ReqIdx ) const = 0 ;             // import resources from a publicly manageable form
		//
		virtual ::string                 start_job           ( JobIdx , SpawnedEntry const&          ) const { return  {}                        ; }
		virtual ::pair_s<bool/*retry*/>  end_job             ( JobIdx , SpawnedEntry const& , Status ) const { return {{},false/*retry*/       } ; }
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( JobIdx , SpawnedEntry const&          ) const { return {{},HeartbeatState::Alive} ; } // only called before start
		virtual void                     kill_queued_job     (          SpawnedEntry const&          ) const = 0 ;                                   // .
		//
		virtual SpawnId launch_job( ::stop_token , JobIdx , ::vector<ReqIdx> const& , Pdate prio , ::vector_s const& cmd_line   , Rsrcs const& , bool verbose ) const = 0 ;

		// services
		virtual void config( vmap_ss const& dct , bool dynamic ) {
			sub_config(dct,dynamic) ;
			_launch_queue.open( 'L' , [&](::stop_token st)->void { _launch(st) ; } ) ;
		}
		virtual bool is_local() const {
			return IsLocal ;
		}
		virtual void open_req( ReqIdx req , JobIdx n_jobs ) {
			Trace trace(BeChnl,"open_req",req,n_jobs) ;
			Lock lock     { Req::s_reqs_mutex }                                                              ;   // taking Req::s_reqs_mutex is compulsery to derefence req
			bool inserted = reqs.insert({ req , {n_jobs,Req(req)->options.flags[ReqFlag::Verbose]} }).second ;
			SWEAR(inserted) ;
		}
		virtual void close_req(ReqIdx req) {
			auto it = reqs.find(req) ;
			Trace trace(BeChnl,"close_req",req,STR(it==reqs.end())) ;
			if (it==reqs.end()) return ;                                                                         // req has been killed
			ReqEntry const& re = it->second ;
			SWEAR(!re.waiting_jobs) ;
			SWEAR(!re.queued_jobs ) ;
			reqs.erase(it) ;
			if (!reqs) {
				SWEAR(!waiting_jobs) ;
				SWEAR(!spawned_jobs) ;
			}
		}
		// do not launch immediately to have a better view of which job should be launched first
		virtual void submit( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs , ::vmap_ss&& rsrcs ) {
			RsrcsAsk rsa { New , import_(::move(rsrcs),req) } ;                                                  // compile rsrcs
			if (!fit_eventually(*rsa)) throw to_string("not enough resources to launch job ",Job(job)->name()) ;
			ReqEntry& re = reqs.at(req) ;
			SWEAR(!waiting_jobs   .contains(job)) ;                                                              // job must be a new one
			SWEAR(!re.waiting_jobs.contains(job)) ;                                                              // in particular for this req
			CoarseDelay pressure = submit_attrs.pressure ;
			Trace trace(BeChnl,"submit",rsa,pressure) ;
			//
			re.waiting_jobs[job] = pressure ;
			waiting_jobs.emplace( job , WaitingEntry(rsa,submit_attrs,re.verbose) ) ;
			re.waiting_queues[rsa].insert({pressure,job}) ;
			_new_submitted_jobs = true ;                                                                         // called from main thread, as launch
		}
		virtual void add_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			Trace trace(BeChnl,"add_pressure",job,req,submit_attrs) ;
			ReqEntry& re  = reqs.at(req)           ;
			auto      wit = waiting_jobs.find(job) ;
			if (wit==waiting_jobs.end()) {                                                                       // job is not waiting anymore, mostly ignore
				auto sit = spawned_jobs.find(job) ;
				if (sit==spawned_jobs.end()) {                                                                   // job is already ended
					trace("ended") ;
				} else {
					SpawnedEntry& se = sit->second ;                                                             // if not waiting, it must be spawned if add_pressure is called
					if (re.verbose ) se.verbose = true ;                                                         // mark it verbose, though
					trace("queued") ;
				}
				return ;
			}
			WaitingEntry& we = wit->second ;
			SWEAR(!re.waiting_jobs.contains(job)) ;                                                              // job must be new for this req
			CoarseDelay pressure = submit_attrs.pressure ;
			trace("adjusted_pressure",pressure) ;
			//
			re.waiting_jobs[job] = pressure ;
			re.waiting_queues[we.rsrcs_ask].insert({pressure,job}) ;                                             // job must be known
			we.submit_attrs |= submit_attrs ;
			we.verbose      |= re.verbose   ;
			we.n_reqs++ ;
		}
		virtual void set_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry& re = reqs.at(req)           ;                                                              // req must be known to already know job
			auto      it = waiting_jobs.find(job) ;
			//
			if (it==waiting_jobs.end()) return ;                                                                 // job is not waiting anymore, ignore
			WaitingEntry        & we           = it->second                         ;
			CoarseDelay         & old_pressure = re.waiting_jobs  .at(job         ) ;                            // job must be known
			::set<PressureEntry>& q            = re.waiting_queues.at(we.rsrcs_ask) ;                            // including for this req
			CoarseDelay           pressure     = submit_attrs.pressure              ;
			Trace trace("set_pressure","pressure",pressure) ;
			we.submit_attrs |= submit_attrs ;
			q.erase ({old_pressure,job}) ;
			q.insert({pressure    ,job}) ;
			old_pressure = pressure ;
		}
	protected :
		virtual ::string start(JobIdx job) {
			auto it = spawned_jobs.find(job) ;
			if (it==spawned_jobs.end()) return {} ;                                                              // job was killed in the mean time
			SpawnedEntry& se = it->second ;
			//
			se.started = true ;
			for( auto& [r,re] : reqs ) re.queued_jobs.erase(job) ;
			::string msg = start_job(job,se) ;
			if (call_launch_after_start()) _launch_queue.wakeup() ;                                              // not compulsery but improves reactivity
			return msg ;
		}
		virtual ::pair_s<bool/*retry*/> end( JobIdx j , Status s ) {
			auto it = spawned_jobs.find(j) ;
			if (it==spawned_jobs.end()) return {{},false/*retry*/} ;                                             // job was killed in the mean time
			SpawnedEntry&           se     = it->second      ; SWEAR(se.started) ;
			::pair_s<bool/*retry*/> digest = end_job(j,se,s) ;
			spawned_jobs.erase(it) ;                                                                             // erase before calling launch so job is freed w.r.t. n_jobs
			if (call_launch_after_end()) _launch_queue.wakeup() ;                                                // not compulsery but improves reactivity
			return digest ;
		}
		virtual ::pair_s<HeartbeatState> heartbeat(JobIdx j) {                                                   // called on jobs that did not start after at least newwork_delay time
			auto          it = spawned_jobs.find(j) ; SWEAR(it!=spawned_jobs.end(),j) ;
			SpawnedEntry& se = it->second           ; SWEAR(!se.started           ,j) ;                          // we should not be called on started jobs
			if (!se.id) return {{},HeartbeatState::Alive} ;                                                      // job is being launched
			::pair_s<HeartbeatState> digest = heartbeat_queued_job(j,se) ;
			//
			if (digest.second!=HeartbeatState::Alive) {
				Trace trace(BeChnl,"heartbeat",j,se.id) ;
				se.id = 0 ;
				kill_queued_job(se) ;                                                                            // inform sub-backend rsrcs are released
				spawned_jobs.erase(it) ;
				for( auto& [r,re] : reqs ) re.queued_jobs.erase(j ) ;
			}
			return digest ;
		}
		// kill all if req==0
		virtual ::vector<JobIdx> kill_waiting_jobs(ReqIdx req=0) {
			::vector<JobIdx> res ;
			Trace trace(BeChnl,"kill_req",T,req,reqs.size()) ;
			if ( !req || reqs.size()<=1 ) {
				if (req) SWEAR( reqs.size()==1 && req==reqs.begin()->first ) ;                                   // ensure the last req is the right one
				// kill waiting jobs
				for( auto const& [j,_] : waiting_jobs ) res.push_back(j) ;
				waiting_jobs.clear() ;
				for( auto& [_,re] : reqs ) re.clear() ;
			} else {
				auto      rit = reqs.find(req) ; SWEAR(rit!=reqs.end()) ;                                        // we should not kill a non-existent req
				ReqEntry& re  = rit->second    ;
				// kill waiting jobs
				for( auto const& [j,_] : re.waiting_jobs ) {
					WaitingEntry& we = waiting_jobs.at(j) ;
					if (we.n_reqs==1) waiting_jobs.erase(j) ;
					else              we.n_reqs--           ;
					res.push_back(j) ;
				}
				re.clear() ;
			}
			return res ;
		}
		virtual void kill_job(JobIdx j) {
			Trace trace(BeChnl,"kill_job",j) ;
			auto it = spawned_jobs.find(j) ;
			SWEAR(it!=spawned_jobs.end()) ;
			SWEAR(!it->second.started) ;                                                                         // if job is started, it is not our responsibility any more
			kill_queued_job(it->second) ;
			spawned_jobs.erase(it) ;
		}
		virtual void launch() {
			if (!_new_submitted_jobs) return ;
			_new_submitted_jobs = false ;                                                                        // called from main thread, as submit
			_launch_queue.wakeup() ;
		}
		void _launch(::stop_token st) {
			struct LaunchDescr {
				::vector<ReqIdx>   reqs     ;
				Rsrcs              rsrcs    ;
				::vector_s         cmd_line ;
				Pdate              prio     ;
				bool               verbose  = false   ;
				::atomic<SpawnId>* id       = nullptr ;
			} ;
			for( auto [req,eta] : Req::s_etas() ) {                                                              // /!\ it is forbidden to dereference req without taking Req::s_reqs_mutex first
				Trace trace(BeChnl,"launch",req) ;
				::vmap<JobIdx,LaunchDescr> launch_descrs ;
				{	Lock lock { _s_mutex } ;
					auto rit = reqs.find(+req) ;
					if (rit==reqs.end()) continue ;
					JobIdx                               n_jobs = rit->second.n_jobs         ;
					::umap<RsrcsAsk,set<PressureEntry>>& queues = rit->second.waiting_queues ;
					for(;;) {
						if ( n_jobs && spawned_jobs.size()>=n_jobs ) break ;                                     // cannot have more than n_jobs running jobs because of this req, process next req
						auto candidate = queues.end() ;
						for( auto it=queues.begin() ; it!=queues.end() ; it++ ) {
							if ( candidate!=queues.end() && it->second.begin()->pressure<=candidate->second.begin()->pressure ) continue ;
							if (fit_now(it->first)) candidate = it ;                                                                                          // continue to find a better candidate
						}
						if (candidate==queues.end()) break ;                                                                                                  // nothing for this req, process next req
						//
						::set<PressureEntry>& pressure_set   = candidate->second               ;
						auto                  pressure_first = pressure_set.begin()            ; SWEAR(pressure_first!=pressure_set.end(),candidate->first) ; // what is this candidate ...
						Pdate                 prio           = eta-pressure_first->pressure    ;                                                              // ... with no pressure ?
						JobIdx                j              = pressure_first->job             ;
						auto                  wit            = waiting_jobs.find(j)            ;
						bool                  verbose        = wit->second.verbose             ;
						Rsrcs                 rsrcs          = acquire_rsrcs(candidate->first) ;
						//
						::vector<ReqIdx> rs { +req } ;
						for( auto const& [r,re] : reqs )
							if      (!re.waiting_jobs.contains(j)) SWEAR(r!=+req,r) ;
							else if (r!=+req                     ) rs.push_back(r)  ;
						auto [sjit,inserted] = spawned_jobs.emplace( j , SpawnedEntry(rsrcs,verbose) ) ;
						SWEAR(inserted) ;
						launch_descrs.emplace_back( j , LaunchDescr{ rs , rsrcs , acquire_cmd_line(T,j,rs,export_(*rsrcs),wit->second.submit_attrs) , prio , verbose , &sjit->second.id } ) ;
						waiting_jobs.erase(wit) ;
						//
						for( ReqIdx r : rs ) {
							ReqEntry& re   = reqs.at(r)              ;
							auto      wit1 = re.waiting_jobs.find(j) ;
							if (r!=+req) {
								auto                  wit2 = re.waiting_queues.find(candidate->first) ;
								::set<PressureEntry>& pes  = wit2->second                             ;
								PressureEntry         pe   { wit1->second , j }                       ;                // /!\ pressure is job pressure for r, not for req
								SWEAR(pes.contains(pe)) ;
								if (pes.size()==1) re.waiting_queues.erase(wit2) ;                                     // last entry for this rsrcs, erase the entire queue
								else               pes              .erase(pe  ) ;
							}
							re.waiting_jobs.erase (wit1) ;
							re.queued_jobs .insert(j   ) ;
						}
						if (pressure_set.size()==1) queues      .erase(candidate     ) ;                               // last entry for this rsrcs, erase the entire queue
						else                        pressure_set.erase(pressure_first) ;
					}
				}
				{	Lock lock { id_mutex } ;
					for( auto& [ji,ld] : launch_descrs ) {
						try {
							*ld.id = launch_job( st , ji , ld.reqs , ld.prio , ld.cmd_line , ld.rsrcs , ld.verbose ) ; // XXX : manage errors, for now rely on heartbeat
							trace("child",ji,ld.prio,ld.id,ld.cmd_line) ;
						} catch (::string const& e) {
							trace("fail",ji,ld.prio,e) ;
						}
					}
				}
				{	Lock lock { _s_mutex } ;
					for( auto const& [ji,ld] : launch_descrs ) {
						auto it=spawned_jobs.find(ji) ;
						if (it==spawned_jobs.end()) continue ;                                                         // job has gone (killed or ended) since it was launched, rsrcs are already freed
						if (!it->second.id) {
							kill_queued_job(it->second) ;                                                              // job could not be launched, inform sub-backend rsrcs are released
							it->second.rsrcs = {} ;
						}
						spawned_jobs.flush(it) ;                                                                       // collect unused entries
					}
					launch_descrs.clear() ;                                                                            // destroy entries while holding the lock
				}
				trace("done") ;
			}
		}

		// data
		::umap<ReqIdx,ReqEntry    > reqs         ;                         // all open Req's
		::umap<JobIdx,WaitingEntry> waiting_jobs ;                         // jobs retained here
		SpawnedTab                  spawned_jobs ;                         // jobs spawned until end
	protected :
		Mutex<MutexLvl::BackendId> mutable id_mutex ;
	private :
		WakeupThread<false/*Flush*/> mutable _launch_queue       ;
		bool                                 _new_submitted_jobs = false ; // submit and launch are both called from main thread, so no need for protection

	} ;

}
