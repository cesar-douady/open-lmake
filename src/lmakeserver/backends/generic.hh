// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

// XXX : rework to maintain an ordered list of waiting_queues in ReqEntry to avoid walking through all rsrcs for each launched job

namespace Backends {

	//
	// Shared
	//

	// share actual resources data as we typically have a lot of jobs with the same resources
	template< class Data , ::unsigned_integral Cnt > struct Shared {
		friend ostream& operator<<( ostream& os , Shared const& s ) {
			/**/           os << "Shared" ;
			if (+s) return os << *s       ;
			else    return os << "()"     ;
		}
		// static data
	private :
		static ::umap<Data,Cnt> _s_store ;                                     // map rsrcs to count of pointers to it, always >0 (erased when reaching 0)
		// cxtors & casts
	public :
		Shared() = default ;
		//
		Shared(Shared const& r) : data{r.data} { if (data) _s_store.at(*data)++ ; }
		Shared(Shared      & r) : data{r.data} { if (data) _s_store.at(*data)++ ; }
		Shared(Shared     && r) : data{r.data} { r.data = nullptr               ; }
		//
		template<class... A> Shared(A&&... args) {
			Data d{::forward<A>(args)...} ;
			auto it = _s_store.find(d) ;
			if (it==_s_store.end()) it = _s_store.insert({d,1}).first ;
			else                    it->second++ ;
			data = &it->first ;
		}
		//
		~Shared() {
			if (!data) return ;
			auto it = _s_store.find(*data) ;
			SWEAR(it!=_s_store.end()) ;
			if (it->second==1) _s_store.erase(it) ;
			else               it->second--       ;
		}
		//
		Shared& operator=(Shared const& r) { this->~Shared() ; new(this) Shared(       r ) ; return *this ; }
		Shared& operator=(Shared     && r) { this->~Shared() ; new(this) Shared(::move(r)) ; return *this ; }
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
	template< class Data , ::unsigned_integral Cnt > ::umap<Data,Cnt> Shared<Data,Cnt>::_s_store ;

}

namespace std {
	template< class Data , ::unsigned_integral Cnt > struct hash<Backends::Shared<Data,Cnt>> {
		size_t operator()(Backends::Shared<Data,Cnt> const& s) const {
			return hash<Data const*>()(s.data) ;
		}
	} ;
}

namespace Backends {

	template<class I> static inline I from_string_rsrc( ::string const& k , ::string const& v ) {
		if ( k=="mem" || k=="tmp" ) return from_string_with_units<'M',I>(v) ;
		else                        return from_string_with_units<    I>(v) ;
	}

	template<class I> static inline ::string to_string_rsrc( ::string const& k , I v ) {
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
			ReqIdx      n_reqs       = 0     ;             // number of reqs waiting for this job
			SubmitAttrs submit_attrs ;
			bool        verbose      = false ;
		} ;

		struct SpawnedEntry {
			Rsrcs   rsrcs   ;
			SpawnId id      = -1    ;
			ReqIdx  n_reqs  =  0    ;  // number of reqs waiting for this job to start
			bool    started = false ;  // if true <=> start() has been called for this job
			bool    verbose = false ;
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
			::uset<JobIdx                     > queued_jobs    ;               // spawned jobs until start
			JobIdx                              n_jobs         = 0     ;       // manage -j option (if >0 no more than n_jobs can be launched on behalf of this req)
			bool                                verbose        = false ;
		} ;

		// specialization
		virtual Bool3 call_launch_after_start() const { return No ; }          // if Maybe, only launch jobs w/ same resources
		virtual Bool3 call_launch_after_end  () const { return No ; }          // .
		//
		virtual bool/*ok*/   fit_eventually( RsrcsDataAsk const&          ) const { return true ; } // true if job with such resources can be spawned eventually
		virtual bool/*ok*/   fit_now       ( RsrcsAsk     const&          ) const { return true ; } // true if job with such resources can be spawned now
		virtual RsrcsData    adapt         ( RsrcsDataAsk const&          ) const = 0 ;             // adapt asked resources to currently available ones
		virtual ::vmap_ss    export_       ( RsrcsData    const&          ) const = 0 ;             // export resources in   a publicly manageable form
		virtual RsrcsDataAsk import_       ( ::vmap_ss        && , ReqIdx ) const = 0 ;             // import resources from a publicly manageable form
		//
		virtual ::string                 start_job           ( JobIdx , SpawnedEntry const&          ) const { return  {}                        ; }
		virtual ::pair_s<bool/*retry*/>  end_job             ( JobIdx , SpawnedEntry const& , Status ) const { return {{},false/*retry*/}        ; }
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( JobIdx , SpawnedEntry const&          ) const { return {{},HeartbeatState::Alive} ; } // only called before start
		virtual void                     kill_queued_job     ( JobIdx , SpawnedEntry const&          ) const = 0 ;                                   // .
		//
		virtual SpawnId launch_job( JobIdx , Pdate prio , ::vector_s const& cmd_line   , Rsrcs const& , bool verbose ) const = 0 ;

		// services
		virtual bool is_local() const {
			return IsLocal ;
		}
		virtual void open_req( ReqIdx req , JobIdx n_jobs ) {
			::unique_lock lock     { Req::s_reqs_mutex }                                                              ; // taking Req::s_reqs_mutex is compulsery to derefence req
			bool          inserted = reqs.insert({ req , {n_jobs,Req(req)->options.flags[ReqFlag::Verbose]} }).second ;
			SWEAR(inserted) ;
		}
		virtual void close_req(ReqIdx req) {
			auto it = reqs.find(req) ;
			if (it==reqs.end()) return ;                                       // req has been killed
			ReqEntry const& re = it->second ;
			SWEAR(re.waiting_jobs.empty()) ;
			SWEAR(re.queued_jobs .empty()) ;
			reqs.erase(it) ;
			if (reqs.empty()) {
				SWEAR(waiting_jobs.empty()) ;
				SWEAR(spawned_jobs.empty()) ;
			}
		}
		// do not launch immediately to have a better view of which job should be launched first
		virtual void submit( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs , ::vmap_ss&& rsrcs ) {
			RsrcsAsk rsa = import_(::move(rsrcs),req) ;                                                         // compile rsrcs
			if (!fit_eventually(*rsa)) throw to_string("not enough resources to launch job ",Job(job)->name()) ;
			ReqEntry& re = reqs.at(req) ;
			SWEAR(!waiting_jobs   .contains(job)) ;                            // job must be a new one
			SWEAR(!re.waiting_jobs.contains(job)) ;                            // in particular for this req
			CoarseDelay pressure = submit_attrs.pressure ;
			Trace trace("submit",rsa,pressure) ;
			//
			re.waiting_jobs[job] = pressure ;
			waiting_jobs.emplace( job , WaitingEntry(rsa,submit_attrs,re.verbose) ) ;
			re.waiting_queues[rsa].insert({pressure,job}) ;
		}
		virtual void add_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry& re  = reqs.at(req)           ;
			auto      wit = waiting_jobs.find(job) ;
			if (wit==waiting_jobs.end()) {                                     // job is not waiting anymore, mostly ignore
				if (re.verbose) {
					auto sit = spawned_jobs.find(job) ;
					if (sit!=spawned_jobs.end()) sit->second.verbose = true ;  // mark it verbose, though
				}
				return ;
			}
			WaitingEntry& we = wit->second ;
			SWEAR(!re.waiting_jobs.contains(job)) ;                            // job must be new for this req
			CoarseDelay pressure = submit_attrs.pressure ;
			Trace trace("add_pressure","adjusted_pressure",pressure) ;
			//
			re.waiting_jobs[job] = pressure ;
			re.waiting_queues[we.rsrcs_ask].insert({pressure,job}) ;           // job must be known
			we.submit_attrs |= submit_attrs ;
			we.verbose      |= re.verbose   ;
			we.n_reqs++ ;
		}
		virtual void set_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry& re = reqs.at(req)           ;                                              // req must be known to already know job
			auto      it = waiting_jobs.find(job) ;
			//
			if (it==waiting_jobs.end()) return ;                                      // job is not waiting anymore, ignore
			WaitingEntry        & we           = it->second                         ;
			CoarseDelay         & old_pressure = re.waiting_jobs  .at(job         ) ; // job must be known
			::set<PressureEntry>& q            = re.waiting_queues.at(we.rsrcs_ask) ; // including for this req
			CoarseDelay           pressure     = submit_attrs.pressure              ;
			Trace trace("set_pressure","pressure",pressure) ;
			we.submit_attrs |= submit_attrs ;
			q.erase ({old_pressure,job}) ;
			q.insert({pressure    ,job}) ;
			old_pressure = pressure ;
		}
	protected :
		virtual ::pair_s<uset<ReqIdx>> start(JobIdx job) {
			::uset<ReqIdx> res ;
			auto           it  = spawned_jobs.find(job) ;
			if (it==spawned_jobs.end()) return {nullptr,{}} ;                  // job was killed in the mean time
			SpawnedEntry& se = it->second ;
			//
			se.started = true ;
			se.n_reqs  = 0    ;
			for( auto& [r,re] : reqs ) if (re.queued_jobs.erase(job)) res.insert(r) ;
			::string msg = start_job(job,se) ;
			launch( call_launch_after_start() , se.rsrcs ) ;                   // not compulsery but improves reactivity
			return { msg , ::move(res) } ;
		}
		virtual ::pair_s<bool/*retry*/> end( JobIdx j , Status s ) {
			auto it = spawned_jobs.find(j) ;
			if (it==spawned_jobs.end()) return {} ;                            // job was killed in the mean time
			SpawnedEntry&           se     = it->second      ;
			::pair_s<bool/*retry*/> digest = end_job(j,se,s) ;
			Rsrcs                   rsrcs  = se.rsrcs        ;                 // copy resources before erasing job from spawned_jobs
			spawned_jobs.erase(it) ;                                           // erase before calling launch so job is freed w.r.t. n_jobs
			launch( call_launch_after_end() , rsrcs ) ;                        // not compulsery but improves reactivity
			return digest ;
		}
		virtual ::pair_s<HeartbeatState> heartbeat(JobIdx j) {                                             // called on jobs that did not start after at least newwork_delay time
			auto                     it     = spawned_jobs.find(j)       ; SWEAR(it!=spawned_jobs.end()) ;
			SpawnedEntry&            se     = it->second                 ; SWEAR(!se.started           ) ; // we should not be called on started jobs
			::pair_s<HeartbeatState> digest = heartbeat_queued_job(j,se) ;
			//
			if (digest.second!=HeartbeatState::Alive) {
				Trace trace("heartbeat",j,se.id) ;
				spawned_jobs.erase(it) ;
			}
			return digest ;
		}
		// kill all if req==0
		virtual ::uset<JobIdx> kill_req(ReqIdx req=0) {
			::uset<JobIdx> res ;
			Trace trace("kill_req",T,req) ;
			if ( !req || reqs.size()<=1 ) {
				// kill waiting jobs
				for( auto const& [j,_] : waiting_jobs ) res.insert(j) ;
				waiting_jobs.clear() ;
				// kill spawned jobs
				for( auto sjit=spawned_jobs.begin() ; sjit!=spawned_jobs.end() ;) // /!\ we delete entries during iteration
					if ( SpawnedEntry& se = sjit->second ; se.started ) {
						se.n_reqs = 0 ;                                        // no req cares about job as there are no more reqs at all
						sjit++ ;
					} else {
						JobIdx j = sjit->first ;
						kill_queued_job(j,se) ;
						spawned_jobs.erase(sjit++) ;
					}
				reqs.clear() ;
			} else {
				auto      rit = reqs.find(req) ; if (rit==reqs.end()) return {} ;
				ReqEntry& re  = rit->second    ;
				// kill waiting jobs
				for( auto const& [j,_] : re.waiting_jobs ) {
					WaitingEntry& we = waiting_jobs.at(j) ;
					if (we.n_reqs==1) { waiting_jobs.erase(j) ; res.insert(j) ; }
					else                we.n_reqs--           ;
				}
				// kill spawned jobs
				for( JobIdx j : re.queued_jobs ) {
					SpawnedEntry& se = spawned_jobs.at(j) ;
					SWEAR(!se.started) ;                                       // when job starts, it is not in spawned_jobs any more
					if (--se.n_reqs) continue ;                                // do not cancel jobs needed for other request
					kill_queued_job(j,se) ;
					trace("killed",j,se.id) ;
					spawned_jobs.erase(j) ;
				}
				reqs.erase(rit) ;
			}
			return res ;
		}
		virtual void launch(                        ) { launch(Yes,{}) ; }     // using default arguments is not recognized to override virtual methods
		virtual void launch( Bool3 go , Rsrcs rsrcs ) {
			Trace trace("launch",T,go,rsrcs) ;
			RsrcsAsk rsrcs_ask ;
			switch (go) {
				case No    : return ;
				case Yes   : break ;
				case Maybe :
					if constexpr (::is_same_v<RsrcsData,RsrcsDataAsk>) rsrcs_ask = rsrcs ;                // only process jobs with same resources if possible
					else                                               FAIL("cannot convert resources") ; // if possible
				break ;
				default : FAIL(go) ;
			}
			//
			::vmap<JobIdx,pair_s<vmap_ss/*rsrcs*/>> err_jobs ;
			for( auto [req,eta] : Req::s_etas() ) {                            // /!\ it is forbidden to dereference req without taking Req::s_reqs_mutex first
				auto rit = reqs.find(+req) ;
				if (rit==reqs.end()) continue ;
				JobIdx                               n_jobs = rit->second.n_jobs         ;
				::umap<RsrcsAsk,set<PressureEntry>>& queues = rit->second.waiting_queues ;
				for(;;) {
					if ( n_jobs && spawned_jobs.size()>=n_jobs ) break ;       // cannot have more than n_jobs running jobs because of this req, process next req
					auto candidate = queues.end() ;
					if (+rsrcs_ask) {
						if (fit_now(rsrcs_ask)) candidate = queues.find(rsrcs_ask) ; // if we have resources, only consider jobs with same resources
					} else {
						for( auto it=queues.begin() ; it!=queues.end() ; it++ ) {
							if ( candidate!=queues.end() && it->second.begin()->pressure<=candidate->second.begin()->pressure ) continue ;
							if ( fit_now(it->first)                                                                           ) candidate = it ; // continue to find a better candidate
						}
					}
					if (candidate==queues.end()) break ;                       // nothing for this req, process next req
					//
					::set<PressureEntry>& pressure_set   = candidate->second            ;
					auto                  pressure_first = pressure_set.begin()         ; SWEAR(pressure_first!=pressure_set.end(),candidate->first) ; // what is this candiate with no pressure ?
					Pdate                 prio           = eta-pressure_first->pressure ;
					JobIdx                j              = pressure_first->job          ;
					auto                  wit            = waiting_jobs.find(j)         ;
					RsrcsAsk const&       rsrcs_ask      = candidate->first             ;
					Rsrcs                 rsrcs          = adapt(*rsrcs_ask)            ;
					::vmap_ss             rsrcs_map      = export_(*rsrcs)              ;
					//
					try {
						::vector_s cmd_line = acquire_cmd_line( T , j , ::move(rsrcs_map) , wit->second.submit_attrs ) ;
						SpawnId    id       = launch_job( j , prio , cmd_line , rsrcs , wit->second.verbose )          ;
						trace("child",req,j,prio,id,cmd_line) ;
						spawned_jobs[j] = { .rsrcs=rsrcs , .id=id , .n_reqs=wit->second.n_reqs , .verbose=wit->second.verbose } ;
						waiting_jobs.erase(wit) ;
					} catch (::string const& e) {
						err_jobs.push_back({j,{e,rsrcs_map}}) ;
					}
					//
					for( auto& [r,re] : reqs ) {
						if (r!=+req) {
							auto wit1 = re.waiting_jobs.find(j) ;
							if (wit1==re.waiting_jobs.end()) continue ;        // ignore req if it did not ask for this job
							auto wit2 = re.waiting_queues.find(rsrcs_ask) ;
							::set<PressureEntry>& pes = wit2->second ;
							PressureEntry         pe  { wit1->second , j } ;   // /!\ pressure is job pressure for r, not for req
							SWEAR(pes.contains(pe)) ;
							if (pes.size()==1) re.waiting_queues.erase(wit2) ; // last entry for this rsrcs, erase the entire queue
							else               pes              .erase(pe  ) ;
						}
						re.waiting_jobs.erase (j) ;
						re.queued_jobs .insert(j) ;
					}
					if (pressure_set.size()==1) queues      .erase(candidate     ) ; // last entry for this rsrcs, erase the entire queue
					else                        pressure_set.erase(pressure_first) ;
				}
			}
			if(err_jobs.size()>0) throw err_jobs ;
		}

		// data
		::umap<ReqIdx,ReqEntry    > reqs         ;         // all open Req's
		::umap<JobIdx,WaitingEntry> waiting_jobs ;         // jobs retained here
		::umap<JobIdx,SpawnedEntry> spawned_jobs ;         // jobs spawned until end

	} ;

}
