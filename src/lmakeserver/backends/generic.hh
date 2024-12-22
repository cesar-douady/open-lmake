// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

// XXX! : rework to maintain an ordered list of waiting_queues in ReqEntry to avoid walking through all rsrcs for each launched job

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
		friend string& operator+=( string& os , Shared const& s ) {
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
		Shared& operator=(Shared s) { swap(self,s) ; return self ; }
		//
		bool operator==(Shared const&) const = default ;
		// access
		Data const& operator* () const { return *data  ; }
		Data const* operator->() const { return &*self ; }
		bool        operator+ () const { return data   ; }
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

	template<class I=size_t> I from_string_rsrc( ::string const& k , ::string const& v ) {
		if ( k=="mem" || k=="tmp" ) return from_string_with_units<'M',I>(v) ;
		else                        return from_string_with_units<    I>(v) ;
	}

	template<class I> ::string to_string_rsrc( ::string const& k , I v ) {
		if ( k=="mem" || k=="tmp" ) return to_string_with_units<'M'>(v) ;
		else                        return to_string_with_units     (v) ;
	}

	template<::unsigned_integral I> I round_rsrc(I i) {
		static constexpr uint8_t NMsb = 3 ;
		if (i<=(1<<NMsb)) return i ;
		uint8_t sw = ::bit_width(i)-NMsb ; // compute necessary shift for rounding.
		return (((i-1)>>sw)+1)<<sw ;       // quantify by rounding up
	}

	//
	// GenericBackend
	//

	template<class Rsrcs> struct _WaitingEntry {
		_WaitingEntry() = default ;
		_WaitingEntry( Rsrcs const& rs , SubmitAttrs const& sa , bool v ) : rsrcs{rs} , n_reqs{1} , submit_attrs{sa} , verbose{v} {}
		// data
		Rsrcs       rsrcs        ;
		ReqIdx      n_reqs       = 0     ; // number of reqs waiting for this job
		SubmitAttrs submit_attrs ;
		bool        verbose      = false ;
	} ;
	template<class Rsrcs> ::string& operator+=( ::string& os , _WaitingEntry<Rsrcs> const& we ) {
		/**/            os << "WaitingEntry(" << we.rsrcs <<','<< we.n_reqs <<','<< we.submit_attrs ;
		if (we.verbose) os << ",verbose"                                                            ;
		return          os << ')'                                                                   ;
	}

	template< class SpawnId , class Rsrcs > struct _SpawnedEntry {
		// ctors & casts
		void create(Rsrcs const& rs) {
			SWEAR(!live) ;
			rsrcs   = rs    ;
			id      = 0     ;
			started = false ;
			verbose = false ;
			live    = true  ;
		}
		// data
		Rsrcs             rsrcs   ;
		::string          msg     ;         // message in case of failure
		::atomic<SpawnId> id      = 0     ;
		::atomic<bool   > failed  = false ; // if true <=> job could not ben launched
		bool              started = false ; // if true <=> start() has been called for this job, for assert only
		bool              verbose = false ;
		::atomic<bool   > live    = false ; // if false <=> entry waiting for suppression
	} ;
	template< class SpawnId , class Rsrcs > ::string& operator+=( ::string& os , _SpawnedEntry<SpawnId,Rsrcs> const& se ) {
		os << "SpawnedEntry(" ;
		if (se.live) {
			/**/            os <<      se.rsrcs ;
			if (se.id     ) os <<','<< se.id    ;
			if (se.started) os <<",started"     ;
			if (se.verbose) os <<",verbose"     ;
		}
		return os <<')' ;
	}

	// we could maintain a list of reqs sorted by eta as we have open_req to create entries, close_req to erase them and new_req_etas to reorder them upon need
	// but this is too heavy to code and because there are few reqs and probably most of them have local jobs if there are local jobs at all, the perf gain would be marginal, if at all
	template< Tag T , class SpawnId , class RsrcsData , bool IsLocal > struct GenericBackend : Backend {

		using Rsrcs = Shared<RsrcsData,JobIdx> ;

		using WaitingEntry = _WaitingEntry<Rsrcs>         ;
		using SpawnedEntry = _SpawnedEntry<SpawnId,Rsrcs> ;

		struct SpawnedTab : ::umap<Job,SpawnedEntry> {
			using Base = ::umap<Job,SpawnedEntry> ;
			using typename Base::iterator       ;
			using typename Base::const_iterator ;
			//
			const_iterator find(Job j) const { const_iterator res = Base::find(j) ; if ( res==Base::end() || !res->second.live ) return Base::end() ; else return res ; }
			iterator       find(Job j)       { iterator       res = Base::find(j) ; if ( res==Base::end() || !res->second.live ) return Base::end() ; else return res ; }
			//
			bool   operator+() const { return _sz ; }                        // cannot inherit from Base as zombies would be counted
			size_t size     () const { return _sz ; }                        // .
			//
			iterator create( GenericBackend const& be , Job j , Rsrcs const& rsrcs ) {
				be.acquire_rsrcs(rsrcs) ;
				iterator res = Base::try_emplace(j).first  ;
				SWEAR(!res->second.live) ;
				res->second.create(rsrcs) ;
				_sz++ ;
				return res ;
			}
			void start( GenericBackend const& be , iterator it ) {
				SWEAR(!it->second.started) ;
				be.start_rsrcs(it->second.rsrcs) ;
				it->second.started = true ;
			}
			void erase( GenericBackend const& be , iterator it ) {
				SWEAR(it->second.live) ;
				if (!it->second.started) be.start_rsrcs(it->second.rsrcs) ;
				/**/                     be.end_rsrcs  (it->second.rsrcs) ;
				if (it->second.id      ) Base::erase(it) ;
				else                     it->second.live = false ;           // if no id, we may not have the necesary lock to erase the entry, defer
				_sz-- ;
			} ;
			void flush(iterator it) {
				if ( it!=Base::end() && !it->second.live ) Base::erase(it) ; // solve deferred action
			}
		private :
			size_t _sz = 0 ;                                                 // dont trust Base::size() as zombies would be counted
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
			Job         job      ;
		} ;

		struct ReqEntry {
			ReqEntry() = default ;
			ReqEntry( JobIdx nj , bool v ) : n_jobs{nj} , verbose{v} {}
			// service
			void clear() {
				waiting_queues.clear() ;
				waiting_jobs  .clear() ;
			}
			// data
			::umap<Rsrcs,set<PressureEntry>> waiting_queues ;
			::umap<Job,CoarseDelay         > waiting_jobs   ;
			JobIdx                           n_jobs         = 0     ; // manage -j option (if >0 no more than n_jobs can be launched on behalf of this req)
			bool                             verbose        = false ;
		} ;

		// specialization
		virtual void sub_config( vmap_ss const& , bool /*dynamic*/ ) {}
		//
		virtual bool call_launch_after_start() const { return false ; }
		virtual bool call_launch_after_end  () const { return false ; }
		//
		virtual void       acquire_rsrcs( Rsrcs     const&             ) const = 0 ;           // acquire asked resources
		virtual ::string   lacking_rsrc ( RsrcsData const&             ) const { return {} ; } // true if job with such resources can be spawned eventually
		virtual bool/*ok*/ fit_now      ( Rsrcs     const&             ) const = 0 ;           // true if job with such resources can be spawned now
		virtual void       start_rsrcs  ( Rsrcs     const&             ) const {}              // handle resources at start of job
		virtual void       end_rsrcs    ( Rsrcs     const&             ) const {}              // handle resources at end   of job
		virtual ::vmap_ss  export_      ( RsrcsData const&             ) const = 0 ;           // export resources in   a publicly manageable form
		virtual RsrcsData  import_      ( ::vmap_ss     && , Req , Job ) const = 0 ;           // import resources from a publicly manageable form
		//
		virtual ::string                 start_job           ( Job , SpawnedEntry const&          ) const { return  {}                        ; }
		virtual ::pair_s<bool/*retry*/>  end_job             ( Job , SpawnedEntry const& , Status ) const { return {{},false/*retry*/       } ; }
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( Job , SpawnedEntry const&          ) const { return {{},HeartbeatState::Alive} ; } // only called before start
		virtual void                     kill_queued_job     (       SpawnedEntry const&          ) const = 0 ;                                   // .
		//
		virtual SpawnId launch_job( ::stop_token , Job , ::vector<ReqIdx> const& , Pdate prio , ::vector_s const& cmd_line , Rsrcs const& , bool verbose ) const = 0 ;
		//
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& capacity , JobIdx ) const { // transform remote resources into local resources
			::umap_s<size_t> capa   = mk_umap(capacity) ;
			::vmap_ss        res    ;
			bool             single = false             ;
			for( auto&& [k,v] : rsrcs ) {
				auto it = capa.find(k) ;
				if (it==capa.end() ) { single = true ; continue ; }                                       // unrecognized resource : fall back to single job by reserving the full capacity
				size_t v1 = from_string_rsrc<size_t>(k,v) ;
				if (v1>it->second) { v1 = it->second ; single = true ; }
				res.emplace_back( ::move(k) , to_string_rsrc(k,v1) ) ;                                    // recognized resource : allow local execution by limiting resource to capacity
			}
			if (single) res.emplace_back( "<single>" , "1" ) ;
			return res ;
		}

		// services
		virtual void config( vmap_ss const& dct , bool dynamic ) {
			sub_config(dct,dynamic) ;
			_launch_queue.open( 'L' , [&](::stop_token st)->void { _launch(st) ; } ) ;
		}
		virtual bool is_local() const {
			return IsLocal ;
		}
		virtual void open_req( Req req , JobIdx n_jobs ) {
			Trace trace(BeChnl,"open_req",req,n_jobs) ;
			Lock lock     { Req::s_reqs_mutex }                                                              ;     // taking Req::s_reqs_mutex is compulsery to derefence req
			bool inserted = reqs.insert({ req , {n_jobs,Req(req)->options.flags[ReqFlag::Verbose]} }).second ;
			if (n_jobs) { n_n_jobs++ ; SWEAR(n_n_jobs) ; }                                                         // check no overflow
			SWEAR(inserted) ;
		}
		virtual void close_req(Req req) {
			auto it = reqs.find(req) ;
			Trace trace(BeChnl,"close_req",req,STR(it==reqs.end())) ;
			if (it==reqs.end()) return ;                                                                           // req has been killed
			ReqEntry const& re = it->second ;
			SWEAR(!re.waiting_jobs,re.waiting_jobs) ;
			if (re.n_jobs) { SWEAR(n_n_jobs) ; n_n_jobs-- ; }                                                      // check no underflow
			reqs.erase(it) ;
			if (!reqs) {
				SWEAR(!waiting_jobs,waiting_jobs) ;
				SWEAR(!spawned_jobs,spawned_jobs) ;                                                                // there may be !live entries waiting for destruction
			}
		}
		// do not launch immediately to have a better view of which job should be launched first
		virtual void submit( Job job , Req req , SubmitAttrs const& submit_attrs , ::vmap_ss&& rsrcs ) {
			// Round required resources to ensure number of queues is limited even when there is a large variability in resources.
			// The important point is to be in log, so only the 4 msb of the resources are considered to choose a queue.
			RsrcsData rd         = import_(::move(rsrcs),req,job) ;
			Rsrcs     rs         { New , rd             }         ; if ( ::string msg=lacking_rsrc(*rs) ; +msg ) throw msg+" to launch job "+Job(job)->name() ;
			Rsrcs     rs_rounded { New , rd.round(self) }         ;
			ReqEntry& re = reqs.at(req) ;
			SWEAR(!waiting_jobs   .contains(job)) ;                                                                // job must be a new one
			SWEAR(!re.waiting_jobs.contains(job)) ;                                                                // in particular for this req
			CoarseDelay pressure = submit_attrs.pressure ;
			Trace trace(BeChnl,"submit",rs,pressure) ;
			//
			re.waiting_jobs[job] = pressure ;
			waiting_jobs.emplace( job , WaitingEntry(rs,submit_attrs,re.verbose) ) ;
			re.waiting_queues[rs_rounded].insert({pressure,job}) ;
			if (!_oldest_submitted_job.load()) _oldest_submitted_job = New ;
		}
		virtual void add_pressure( Job job , Req req , SubmitAttrs const& submit_attrs ) {
			Trace trace(BeChnl,"add_pressure",job,req,submit_attrs) ;
			ReqEntry& re  = reqs.at(req)           ;
			auto      wit = waiting_jobs.find(job) ;
			if (wit==waiting_jobs.end()) {                                                                         // job is not waiting anymore, mostly ignore
				auto sit = spawned_jobs.find(job) ;
				if (sit==spawned_jobs.end()) {                                                                     // job is already ended
					trace("ended") ;
				} else {
					SpawnedEntry& se = sit->second ;                                                               // if not waiting, it must be spawned if add_pressure is called
					if (re.verbose ) se.verbose = true ;                                                           // mark it verbose, though
					trace("queued") ;
				}
				return ;
			}
			WaitingEntry& we = wit->second ;
			SWEAR(!re.waiting_jobs.contains(job)) ;                                                                // job must be new for this req
			CoarseDelay pressure = submit_attrs.pressure ;
			trace("adjusted_pressure",pressure) ;
			//
			re.waiting_jobs[job] = pressure ;
			re.waiting_queues[{New,we.rsrcs->round(self)}].insert({pressure,job}) ;
			we.submit_attrs |= submit_attrs ;
			we.verbose      |= re.verbose   ;
			we.n_reqs++ ;
		}
		virtual void set_pressure( Job job , Req req , SubmitAttrs const& submit_attrs ) {
			ReqEntry& re = reqs.at(req)           ;                                                                // req must be known to already know job
			auto      it = waiting_jobs.find(job) ;
			//
			if (it==waiting_jobs.end()) return ;                                                                   // job is not waiting anymore, ignore
			WaitingEntry        & we           = it->second                                        ;
			CoarseDelay         & old_pressure = re.waiting_jobs  .at(job                        ) ;               // job must be known
			::set<PressureEntry>& q            = re.waiting_queues.at({New,we.rsrcs->round(self)}) ;               // including for this req
			CoarseDelay           pressure     = submit_attrs.pressure                             ;
			Trace trace("set_pressure","pressure",pressure) ;
			we.submit_attrs |= submit_attrs ;
			q.erase ({old_pressure,job}) ;
			q.insert({pressure    ,job}) ;
			old_pressure = pressure ;
		}
	protected :
		virtual ::string start(Job job) {
			auto          it = spawned_jobs.find(job) ; if (it==spawned_jobs.end()) return {} ;                    // job was killed in the mean time
			SpawnedEntry& se = it->second             ;
			if (!se.id) {
				Lock lock{id_mutex} ;                                                                              // ensure se.id has been updated
				SWEAR(se.id,job) ;
			}
			//
			spawned_jobs.start(self,it) ;
			::string msg = start_job(job,se) ;
			if (call_launch_after_start()) _launch_queue.wakeup() ;
			return msg ;
		}
		virtual ::pair_s<bool/*retry*/> end( Job j , Status s ) {
			auto          it = spawned_jobs.find(j) ; if (it==spawned_jobs.end()) return {{},false/*retry*/} ;     // job was killed in the mean time
			SpawnedEntry& se = it->second           ; SWEAR(se.started) ;
			SWEAR(se.id,j) ;                                                                                       // occurs after start, then se.id has been updated
			::pair_s<bool/*retry*/> digest = end_job(j,se,s) ;
			spawned_jobs.erase(self,it) ;                                                                          // erase before calling launch so job is freed w.r.t. n_jobs
			if ( n_n_jobs || call_launch_after_end() ) _launch_queue.wakeup() ;                                    // if we have a Req limited by n_jobs, we may have to launch a job
			return digest ;
		}
		virtual void heartbeat() {
			if (_oldest_submitted_job.load()+g_config->heartbeat<Pdate(New)) launch() ;                            // prevent jobs from being accumulated for too long
		}
		virtual ::pair_s<HeartbeatState> heartbeat(Job j) {                                                        // called on jobs that did not start after at least newwork_delay time
			auto          it = spawned_jobs.find(j) ; SWEAR(it!=spawned_jobs.end()  ) ;
			SpawnedEntry& se = it->second           ; SWEAR(!se.started           ,j) ;                            // we should not be called on started jobs
			if (!se.id) {
				Lock lock { id_mutex } ;                                                                           // ensure _launch is no more processing entry
				if (!se.id) {                                                                                      // repeat test so test and decision are atomic
					Trace trace(BeChnl,"heartbeat","no_id",j) ;
					if (se.failed) { spawned_jobs.erase(self,it) ; return {se.msg,HeartbeatState::Err  } ; }
					else                                           return {{}    ,HeartbeatState::Alive} ;         // book keeping is not updated yet
				}
			}
			::pair_s<HeartbeatState> digest = heartbeat_queued_job(j,se) ;
			if (digest.second!=HeartbeatState::Alive) {
				Trace trace(BeChnl,"heartbeat",j,se.id,digest.second) ;
				spawned_jobs.erase(self,it) ;
			}
			return digest ;
		}
		// kill all if req==0
		virtual ::vector<Job> kill_waiting_jobs(Req req={}) {
			::vector<Job> res ;
			Trace trace(BeChnl,"kill_req",T,req,reqs.size()) ;
			if ( !req || reqs.size()<=1 ) {
				if (+req) SWEAR( reqs.size()==1 && req==reqs.begin()->first , req , reqs.size() ) ;                // ensure the last req is the right one
				// kill waiting jobs
				res.reserve(waiting_jobs.size()) ;
				for( auto const& [j,_] : waiting_jobs ) res.push_back(j) ;
				waiting_jobs.clear() ;
				for( auto& [_,re] : reqs ) re.clear() ;
			} else {
				auto      rit = reqs.find(req) ; SWEAR(rit!=reqs.end()) ;                                          // we should not kill a non-existent req
				ReqEntry& re  = rit->second    ;
				// kill waiting jobs
				res.reserve(re.waiting_jobs.size()) ;
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
		virtual void kill_job(Job j) {
			Trace trace(BeChnl,"kill_job",j) ;
			auto          it = spawned_jobs.find(j) ; if (it==spawned_jobs.end()) return ;                         // job was not actually spawned
			SpawnedEntry& se = it->second           ; SWEAR(!se.started) ;                                         // if job is started, it is not our responsibility any more
			if (se.id) {                                     kill_queued_job(se) ; spawned_jobs.erase(self,it) ; }
			else       { Lock lock { id_mutex } ; if (se.id) kill_queued_job(se) ; spawned_jobs.erase(self,it) ; } // lock to ensure se.id is up to date and do same actions (erase while holding lock)
		}
		virtual void launch() {
			if (!_oldest_submitted_job.load()) return ;
			_oldest_submitted_job = Pdate() ;
			_launch_queue.wakeup() ;
		}
		void _launch(::stop_token st) {
			struct LaunchDescr {
				::vector<ReqIdx> reqs     ;
				::vector_s       cmd_line ;
				Pdate            prio     ;
				SpawnedEntry*    entry    = nullptr ;
			} ;
			for( auto [req,eta] : Req::s_etas() ) {                                                                // /!\ it is forbidden to dereference req without taking Req::s_reqs_mutex first
				Trace trace(BeChnl,"launch",req) ;
				::vmap<Job,LaunchDescr> launch_descrs ;
				{	Lock lock { _s_mutex } ;
					auto rit = reqs.find(+req) ;
					if (rit==reqs.end()) continue ;
					JobIdx                            n_jobs = rit->second.n_jobs         ;
					::umap<Rsrcs,set<PressureEntry>>& queues = rit->second.waiting_queues ;
					while (!( n_jobs && spawned_jobs.size()>=n_jobs )) {                                           // cannot have more than n_jobs running jobs because of this req, process next req
						auto candidate = queues.end() ;
						for( auto it=queues.begin() ; it!=queues.end() ; it++ ) {
							if ( candidate!=queues.end() && it->second.begin()->pressure<=candidate->second.begin()->pressure ) continue ;
							if (fit_now(it->first)) candidate = it ;                                                                       // continue to find a better candidate
						}
						if (candidate==queues.end()) break ;                                                                               // nothing for this req, process next req
						//
						::set<PressureEntry>& pressure_set = candidate->second                                     ;
						auto                  pressure1    = pressure_set.begin()                                  ; SWEAR(pressure1!=pressure_set.end(),candidate->first) ; // a candidate ...
						Pdate                 prio         = eta-pressure1->pressure                               ;                                                         // ... with no pressure ?
						Job                   j            = pressure1->job                                        ;
						auto                  wit          = waiting_jobs.find(j)                                  ;
						SpawnedEntry&         se           = spawned_jobs.create(self,j,wit->second.rsrcs)->second ;
						//
						se.verbose = wit->second.verbose ;
						::vector<ReqIdx> rs { +req } ;
						for( auto const& [r,re] : reqs )
							if      (!re.waiting_jobs.contains(j)) SWEAR(r!=req,r)  ;
							else if (r!=req                      ) rs.push_back(+r) ;
						launch_descrs.emplace_back( j , LaunchDescr{ rs , acquire_cmd_line(T,j,rs,export_(*se.rsrcs),wit->second.submit_attrs) , prio , &se } ) ;
						waiting_jobs.erase(wit) ;
						//
						for( Req r : rs ) {
							ReqEntry& re   = reqs.at(r)              ;
							auto      wit1 = re.waiting_jobs.find(j) ;
							if (r!=req) {
								auto                  wit2 = re.waiting_queues.find(candidate->first) ;
								::set<PressureEntry>& pes  = wit2->second                             ;
								PressureEntry         pe   { wit1->second , j }                       ;               // /!\ pressure is job pressure for r, not for req
								SWEAR(pes.contains(pe)) ;
								if (pes.size()==1) re.waiting_queues.erase(wit2) ;                                    // last entry for this rsrcs, erase the entire queue
								else               pes              .erase(pe  ) ;
							}
							re.waiting_jobs.erase(wit1) ;
						}
						if (pressure_set.size()==1) queues      .erase(candidate) ;                                   // last entry for this rsrcs, erase the entire queue
						else                        pressure_set.erase(pressure1) ;
					}
				}
				for( auto& [j,ld] : launch_descrs ) {
					Lock lock { id_mutex } ;
					SpawnedEntry& se = *ld.entry ;
					if (!se.live) continue ;                                                                          // job was cancelled before being launched
					try {
						SpawnId id = launch_job( st , j , ld.reqs , ld.prio , ld.cmd_line , se.rsrcs , se.verbose ) ; // XXX! : manage errors, for now rely on heartbeat
						SWEAR(id,j) ;                                                                                 // null id is used to mark absence of id
						se.id = id ;
						trace("child",j,ld.prio,id,ld.cmd_line) ;
					} catch (::string const& e) {
						trace("fail",j,ld.prio,e) ;
						se.msg    = e    ;
						se.failed = true ;
						_launch_queue.wakeup() ; // we may have new jobs to launch as we did not launch all jobs we were supposed to
					}
				}
				trace("done") ;
			}
		}

		// data
		::umap<Req,ReqEntry    > reqs         ;                      // all open Req's
		ReqIdx                   n_n_jobs     ;                      // number of Req's that has a non-null n_jobs
		::umap<Job,WaitingEntry> waiting_jobs ;                      // jobs retained here
		SpawnedTab               spawned_jobs ;                      // jobs spawned until end
	protected :
		Mutex<MutexLvl::BackendId> mutable id_mutex ;
	private :
		WakeupThread<false/*Flush*/> mutable _launch_queue         ;
		::atomic<Pdate>                      _oldest_submitted_job ; // if no date, no new job

	} ;

}
