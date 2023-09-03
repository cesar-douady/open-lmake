// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/sysinfo.h>

#include "core.hh"

// PER_BACKEND : there must be a file describing each backend

// XXX : rework to maintain an ordered list of waiting_queues in ReqEntry to avoid walking through all rsrcs for each launched job

using namespace Engine ;

namespace Backends::Local {

	struct LocalBackend ;

	using Rsrc = uint32_t ;
	struct Rsrc2 {
		friend ::ostream& operator<<( ::ostream& os , Rsrc2 const& r2 ) {
			return os << r2.min <<'<'<< r2.max ;
		}
		bool operator==(Rsrc2 const&) const = default ;
		// data
		Rsrc min ;
		Rsrc max ;
	} ;

	struct RsrcsData : ::vector<Rsrc> {
		// cxtors & casts
		RsrcsData(                                        ) = default ;
		RsrcsData( LocalBackend const& , ::vmap_ss const& ) ;
		::vmap_ss vmap(LocalBackend const&) const ;
		// services
		RsrcsData& operator+=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size()) ; for( size_t i=0 ; i<size() ; i++ ) (*this)[i] += rsrcs[i] ; return *this ; }
		RsrcsData& operator-=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size()) ; for( size_t i=0 ; i<size() ; i++ ) (*this)[i] -= rsrcs[i] ; return *this ; }
	} ;

	struct RsrcsData2 : ::vector<Rsrc2> {
		// cxtors & casts
		RsrcsData2(                                        ) = default ;
		RsrcsData2( LocalBackend const& , ::vmap_ss const& ) ;
		// services
		bool fit_in(RsrcsData const& avail) const {                        // true if all resources fit within avail
			SWEAR(size()==avail.size()) ;
			for( size_t i=0 ; i<size() ; i++ ) if ((*this)[i].min>avail[i]) return false ;
			return true ;
		}
		RsrcsData within(RsrcsData const& avail) const {                       // what fits within avail
			RsrcsData res ; res.reserve(size()) ;
			for( size_t i=0 ; i<size() ; i++ ) res.push_back(::min( (*this)[i].max , avail[i] ) ) ;
			return res ;
		}
	} ;

}

namespace std {
	template<> struct hash<Backends::Local::RsrcsData> {
		using Rsrc = Backends::Local::Rsrc ;
		size_t operator()(Backends::Local::RsrcsData const& r) const {         // use FNV-32, easy, fast and good enough, use 32 bits as we are mostly interested by lsb's
			size_t res = 0x811c9dc5 ;
			for( Rsrc x : r ) res = (res^hash<Rsrc>()(x)) * 0x01000193 ;
			return res ;
		}
	} ;
	template<> struct hash<Backends::Local::RsrcsData2> {
		using Rsrc2 = Backends::Local::Rsrc2 ;
		using Rsrc  = Backends::Local::Rsrc  ;
		size_t operator()(Backends::Local::RsrcsData2 const& r2) const {       // use FNV-32, easy, fast and good enough, use 32 bits as we are mostly interested by lsb's
			size_t res = 0x811c9dc5 ;
			for( Rsrc2 x2 : r2 ) {
				res = (res^hash<Rsrc>()(x2.min)) * 0x01000193 ;
				res = (res^hash<Rsrc>()(x2.max)) * 0x01000193 ;
			}
			return res ;
		}
	} ;
}

namespace Backends::Local {
	// share actual resources data as we typically have a lot of jobs with the same resources
	template< class Data , ::unsigned_integral Cnt > struct Shared {
		friend ostream& operator<<( ostream& os , Shared const& s ) { return os<<*s ; }
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
		// data
		Data const* data = nullptr ;
	} ;
	template< class Data , ::unsigned_integral Cnt > ::umap<Data,Cnt> Shared<Data,Cnt>::_s_store ;

	using Rsrcs  = Shared<RsrcsData ,JobIdx> ;
	using Rsrcs2 = Shared<RsrcsData2,JobIdx> ;

}

namespace std {
	template<> struct hash<Backends::Local::Rsrcs > { size_t operator()(Backends::Local::Rsrcs  const& r ) const { return hash<Backends::Local::RsrcsData  const*>()(r .data) ; } } ;
	template<> struct hash<Backends::Local::Rsrcs2> { size_t operator()(Backends::Local::Rsrcs2 const& r2) const { return hash<Backends::Local::RsrcsData2 const*>()(r2.data) ; } } ;
}

namespace Backends::Local {

	constexpr Tag MyTag = Tag::Local ;

	// we could maintain a list of reqs sorted by eta as we have open_req to create entries, close_req to erase them and new_req_eta to reorder them upon need
	// but this is too heavy to code and because there are few reqs and probably most of them have local jobs if there are local jobs at all, the perf gain would be marginal, if at all
	struct LocalBackend : Backend {

		struct WaitingEntry {
			WaitingEntry() = default ;
			WaitingEntry( Rsrcs2 const& rs2 , SubmitAttrs const& sa ) : rsrcs2{rs2} , n_reqs{1} , submit_attrs{sa} {}
			// data
			Rsrcs2      rsrcs2       ;
			ReqIdx      n_reqs       = 0 ;                 // number of reqs waiting for this job
			SubmitAttrs submit_attrs ;
		} ;

		struct RunningEntry {
			Rsrcs  rsrcs   ;
			pid_t  pid     = -1    ;
			ReqIdx n_reqs  =  0    ;                       // number of reqs waiting for this job to start
			bool   old     = false ;                       // if true <=> heartbeat has been seen
			bool   started = false ;                       // if true <=> start() has been called for this job
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
			ReqEntry(         ) = default ;
			ReqEntry(JobIdx nj) : n_jobs{nj} {}
			// service
			void clear() {
				waiting_queues.clear() ;
				waiting_jobs  .clear() ;
				starting_jobs .clear() ;
			}
			// data
			::umap<Rsrcs2,set<PressureEntry>> waiting_queues ;
			::umap<JobIdx,CoarseDelay       > waiting_jobs   ;
			::uset<JobIdx                   > starting_jobs  ;
			JobIdx                            n_jobs         = 0 ;
		} ;

		// init
		static void _s_wait_thread_func( ::stop_token stop , LocalBackend* self ) {
			Trace::t_key = 'W' ;
			self->_wait_jobs(stop) ;
		}
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			LocalBackend& self = *new LocalBackend ;
			s_register(MyTag,self) ;
		}

		// services
		virtual void config(Config::Backend const& config) {
			for( auto const& [k,v] : config.dct ) {
				rsrc_idxs[k] = rsrc_keys.size() ;
				rsrc_keys.push_back(k) ;
			}
			capacity = RsrcsData( *this , config.dct ) ;
			avail    = capacity                        ;
			Trace("config",MyTag,"avail_rsrcs",'=',capacity) ;
			static ::jthread wait_jt{_s_wait_thread_func,this} ;
		}
		virtual void open_req( ReqIdx req , JobIdx n_jobs ) {
			SWEAR(!req_map.contains(req)) ;
			req_map.insert({req,n_jobs}) ;
		}
		virtual void close_req(ReqIdx req) {
			SWEAR(req_map.at(req).waiting_jobs .empty()) ;
			SWEAR(req_map.at(req).starting_jobs.empty()) ;
			req_map.erase(req) ;
		}
	private :
		// adjust pressure to give a small boost to jobs that require more resources : use a logarithmic approach to ensure impact is limited
		static CoarseDelay _adjust_pressure( CoarseDelay pressure , Rsrcs2 const& rsrcs2 ) {
			static constexpr CoarseDelay::Val MaxVal = ::numeric_limits<CoarseDelay::Val>::max() ;
			CoarseDelay::Val val = +pressure ;
			for( Rsrc2 r2 : *rsrcs2 ) {
				for( Rsrc r : {r2.min,r2.max} ) {
					uint8_t  nb  = n_bits(r) ;                                 // convert r to a floating point format : 4 bits exponents, 4 bits mantissa
					uint32_t inc = n_bits(r)<<4 ;
					if (inc<256) inc |= nb>4 ? r>>(nb-4) : r<<(4-nb) ;         // manage overflow : infinity if it does not fit on 16 bits (should not occur in practice)
					else         inc  = 256                          ;
					if (val<=MaxVal-inc) val += inc    ;                       // manage overflow (should not occur in practice)
					else                 val  = MaxVal ;
				}
			}
			return CoarseDelay(val) ;
		}
	public :
		// do not launch immediately to have a better view of which job should be launched first
		virtual void submit( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs , ::vmap_ss const& rsrcs ) {
			Rsrcs2 rs2 { *this , rsrcs } ;                                                                          // compile rsrcs
			if (!rs2->fit_in(capacity)) throw to_string("not enough resources to launch job ",Job(job).name()) ;
			ReqEntry& entry = req_map.at(req) ;
			SWEAR(!waiting_map       .contains(job)) ;                           // job must be a new one
			SWEAR(!entry.waiting_jobs.contains(job)) ;                           // in particular for this req
			CoarseDelay pressure = _adjust_pressure(submit_attrs.pressure,rs2) ;
			Trace trace("submit","rsrs",rs2,"adjusted_pressure",pressure) ;
			//
			waiting_map.emplace( job , WaitingEntry(rs2,submit_attrs) ) ;
			entry.waiting_jobs[job] = pressure             ;
			entry.waiting_queues[rs2].insert({pressure,job}) ;
		}
		virtual void add_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry    & entry = req_map.at(req)       ;
			auto          it    = waiting_map.find(job) ;
			if (it==waiting_map.end()) return ;                                // job is not waiting anymore, ignore
			WaitingEntry& we = it->second ;
			SWEAR(!entry.waiting_jobs.contains(job)) ;                                 // job must be new for this req
			CoarseDelay pressure = _adjust_pressure(submit_attrs.pressure,we.rsrcs2) ;
			Trace trace("add_pressure","adjusted_pressure",pressure) ;
			//
			entry.waiting_jobs[job] = pressure ;
			entry.waiting_queues[we.rsrcs2].insert({pressure,job}) ;           // job must be known
			we.submit_attrs |= submit_attrs ;
			we.n_reqs++ ;
		}
		virtual void set_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry& entry = req_map.at(req)       ;                                            // req must be known to already know job
			auto      it    = waiting_map.find(job) ;
			//
			if (it==waiting_map.end()) return ;                                                      // job is not waiting anymore, ignore
			WaitingEntry        & we           = it->second                                        ;
			CoarseDelay         & old_pressure = entry.waiting_jobs  .at(job      )                ; // job must be known
			::set<PressureEntry>& q            = entry.waiting_queues.at(we.rsrcs2)                ; // including for this req
			CoarseDelay           pressure     = _adjust_pressure(submit_attrs.pressure,we.rsrcs2) ;
			Trace trace("set_pressure","adjusted_pressure",pressure) ;
			we.submit_attrs |= submit_attrs ;
			q.erase ({old_pressure,job}) ;
			q.insert({pressure    ,job}) ;
			old_pressure = pressure ;
		}
	private :
		void _wait_job(RunningEntry const& re) {
			avail += *re.rsrcs ;
			_wait_queue.push(re.pid) ;                                         // defer wait in case job_exec process does some time consuming book-keeping
		}
	public :
		virtual ::uset<ReqIdx> start(JobIdx job) {
			::uset<ReqIdx> res ;
			auto           it  = running_map.find(job) ;
			if (it==running_map.end()) return {} ;                             // job was killed in the mean time
			RunningEntry&  entry = it->second ;
			for( auto& [r,re] : req_map ) if (re.starting_jobs.erase(job)) res.insert(r) ;
			entry.started = true ;
			entry.n_reqs  = 0    ;
			return res ;
		}
		virtual void end(JobIdx job) {
			auto it = running_map.find(job) ;
			if (it==running_map.end()) return ;                                // job was killed in the mean time
			//vvvvvvvvvvvvvvvvvvv
			_wait_job(it->second) ;
			//^^^^^^^^^^^^^^^^^^^
			Trace trace("end","avail_rsrcs",'+',avail) ;
			running_map.erase(it) ;
			launch() ;
		}
		virtual ::vector<JobIdx> heartbeat() {
			// as soon as jobs are started, top-backend handles heart beat
			::vector<JobIdx> res ;
			Trace trace("Local::hearbeat") ;
			for( auto it = running_map.begin() ; it!=running_map.end() ; ) {   // /!\ we delete entries in running_map, beware of iterator management
				RunningEntry& entry = it->second ;
				if (entry.started) { it++ ; continue ; }                       // we do not manage started jobs
				if (entry.old) {
					trace("kill_old",entry.pid) ;
					kill_process(entry.pid,SIGKILL) ;                          // kill job in case it is still alive (it has not started yet, so just kill job_exec)
				} else if (::waitpid(entry.pid,nullptr,WNOHANG)==0) {          // job still exists but should start soon, mark it for next turn
					trace("exists",entry.pid) ;
					entry.old = true ;
					it++ ;
					continue ;
				} else {
					trace("vanished",entry.pid) ;
				}
				res.push_back(it->first) ;
				_wait_job(entry) ;
				running_map.erase(it++) ;                                      // /!\ erase must be called with it before ++, but it must be incremented before erase
			}
			if (!res.empty()) launch() ;
			return res ;
		}
		// kill all if req==0
		virtual ::uset<JobIdx> kill_req(ReqIdx req=0) {
			::uset<JobIdx> res ;
			auto           it  = req_map.find(req) ;
			if ( req && it==req_map.end() ) return {} ;
			Trace trace("kill_req",MyTag,req) ;
			if ( !req || req_map.size()==1 ) {                                 // fast path if req_map.size()==1
				for( auto const& [j,we] : waiting_map ) res.insert(j) ;
				for( auto const& [j,re] : running_map ) {
					//                                 vvvvvvvvvvvvvvvvvvvvvvvvv
					if (!re.started) { res.insert(j) ; kill_group(re.pid,SIGHUP) ; } // if started, not our responsibility to kill it
					/**/                               _wait_job(re) ;               // but we still must wait for process, defer in case job_exec process does some time consuming book-keeping
					//                                 ^^^^^^^^^^^^^
					if (!re.started) trace("killed",re.pid,"avail_rsrcs",'+',avail) ;
					else             trace(                "avail_rsrcs",'+',avail) ;
				}
				for( auto& [r,re] : req_map ) re.clear() ;
				waiting_map.clear() ;
				running_map.clear() ;
			} else {
				ReqEntry& req_entry = it->second ;
				for( auto const& [j,je] : req_entry.waiting_jobs ) {
					WaitingEntry& we = waiting_map.at(j) ;
					if (we.n_reqs==1) { waiting_map.erase(j) ; res.insert(j) ; }
					else              { we.n_reqs--          ;                 }
				}
				for( JobIdx j : req_entry.starting_jobs ) {
					RunningEntry& re = running_map.at(j) ;
					SWEAR(!re.started) ;                                       // when job starts, it is not in starting_jobs any more
					if (--re.n_reqs) continue ;
					res.insert(j) ;
					SWEAR(re.pid>1) ;                                          // values -1, or 1 are unexpected
					//vvvvvvvvvvvvvvvvvvvvvvv
					kill_group(re.pid,SIGHUP) ;
					_wait_job(re) ;
					//^^^^^^^^^^^^^
					trace("killed",re.pid,"avail_rsrcs",'+',avail) ;
					running_map.erase(j) ;
				}
				req_entry.clear() ;
			}
			return res ;
		}
		void launch() {
			Trace trace("launch",MyTag) ;
			for( Req req : Req::s_reqs_by_eta() ) {
				auto req_it = req_map.find(+req) ;
				if (req_it==req_map.end()) continue ;
				JobIdx n_jobs = req_it->second.n_jobs ;
				::umap<Rsrcs2,set<PressureEntry>>& queues = req_it->second.waiting_queues ;
				for(;;) {
					if ( n_jobs && running_map.size()>=n_jobs ) break ;        // cannot have more than n_jobs running jobs because of this req
					auto candidate = queues.end() ;
					for( auto it=queues.begin() ; it!=queues.end() ; it++ ) {
						SWEAR(!it->second.empty()) ;
						if ( candidate!=queues.end() && it->second.begin()->pressure<=candidate->second.begin()->pressure ) continue ;
						if ( !it->first->fit_in(avail)                                                                    ) continue ;
						candidate = it ;                                       // found a candidate, continue to see if we can find one with a higher pressure
					}
					if (candidate==queues.end()) break ;                       // nothing for this req, process next req
					//
					::set<PressureEntry>& pressure_set   = candidate->second     ;
					auto                  pressure_first = pressure_set.begin()  ;
					JobIdx                job            = pressure_first->job   ;
					auto                  wit            = waiting_map.find(job) ;
					Rsrcs2 const&         rsrcs2         = candidate->first      ;
					Rsrcs                 rsrcs          = rsrcs2->within(avail) ;
					//
					::vector_s cmd_line = acquire_cmd_line( MyTag , job , rsrcs->vmap(*this) , wit->second.submit_attrs ) ;
					//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					Child child { false/*as_group*/ , cmd_line , Child::None , Child::None } ;
					//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("child",req,job,pressure_first->pressure,child.pid,cmd_line) ;
					avail -= *rsrcs ;
					trace("avail_rsrcs",'-',avail) ;
					{	auto wit = waiting_map.find(job) ;
						running_map[job] = { rsrcs , child.pid , wit->second.n_reqs } ;
						waiting_map.erase(wit) ;
					}
					child.mk_daemon() ;                                        // we have recorded the pid to wait and there is no fd to close
					//
					for( auto& [r,re] : req_map ) {
						if (r!=+req) {
							auto wit1 = re.waiting_jobs.find(job) ;
							if (wit1==re.waiting_jobs.end()) continue ;
							auto wit2 = re.waiting_queues.find(rsrcs2) ;
							::set<PressureEntry>& pes = wit2->second ;
							PressureEntry         pe  { wit1->second , job } ; // /!\ pressure is job's pressure for r, not for req
							SWEAR(pes.contains(pe)) ;
							if (pes.size()==1) re.waiting_queues.erase(wit2) ; // last entry for this rsrcs, erase the entire queue
							else               pes              .erase(pe  ) ;
						}
						re.waiting_jobs .erase (job) ;
						re.starting_jobs.insert(job) ;
					}
					if (pressure_set.size()==1) queues      .erase(candidate     ) ; // last entry for this rsrcs, erase the entire queue
					else                        pressure_set.erase(pressure_first) ;
				}
			}
		}

	private :
		void _wait_jobs(::stop_token stop) {                                   // execute in a separate thread
			Trace trace("_wait_jobs",MyTag) ;
			for(;;) {
				auto [popped,pid] = _wait_queue.pop(stop) ;
				DiskDate::s_refresh_now() ;                                    // we have waited, refresh now
				if (!popped) return ;
				trace("wait",pid) ;
				::waitpid(pid,nullptr,0) ;
				trace("waited",pid) ;
			}
		}

		// data
	public :
		::umap<ReqIdx,ReqEntry    > req_map     ;
		::umap<JobIdx,WaitingEntry> waiting_map ;
		::umap<JobIdx,RunningEntry> running_map ;
		::umap_s<size_t>            rsrc_idxs   ;
		::vector_s                  rsrc_keys   ;
		RsrcsData                   capacity    ;
		RsrcsData                   avail       ;
	private :
		ThreadQueue<pid_t> _wait_queue ;

	} ;

	bool _inited = (LocalBackend::s_init(),true) ;

	inline RsrcsData::RsrcsData( LocalBackend const& self , ::vmap_ss const& m ) {
		resize(self.rsrc_keys.size()) ;
		for( auto const& [k,v] : m ) {
			auto it = self.rsrc_idxs.find(k) ;
			if (it==self.rsrc_idxs.end()) throw to_string("no resource ",k," for backend ",mk_snake(MyTag)) ;
			SWEAR(it->second<size()) ;
			try        { ::istringstream(v)>>(*this)[it->second] ;                           }
			catch(...) { throw to_string("cannot convert ",v," to a ",typeid(Rsrc).name()) ; }
		}
	}
	inline RsrcsData2::RsrcsData2( LocalBackend const& self , ::vmap_ss const& m ) {
		resize(self.rsrc_keys.size()) ;
		for( auto const& [k,v] : m ) {
			auto it = self.rsrc_idxs.find(k) ;
			if (it==self.rsrc_idxs.end()) throw to_string("no resource ",k," for backend ",MyTag) ;
			SWEAR(it->second<size()) ;
			try {
				size_t pos = v.find('<') ;
				if (pos==Npos) {
					Rsrc x ; ::istringstream(v)>>x ;
					(*this)[it->second].min = x ;
					(*this)[it->second].max = x ;
				} else {
					::istringstream(v.substr(0    ,pos))>>(*this)[it->second].min ;
					::istringstream(v.substr(pos+1    ))>>(*this)[it->second].max ;
				}
			} catch(...) {
				throw to_string("cannot convert ",v," to a ",typeid(Rsrc).name()," nor a min/max pair separated by <") ;
			}
		}
	}

	::vmap_ss RsrcsData::vmap(LocalBackend const& self) const {
		::vmap_ss res ; res.reserve(self.rsrc_keys.size()) ;
		for( size_t i=0 ; i<self.rsrc_keys.size() ; i++ ) res.emplace_back( self.rsrc_keys[i] , to_string((*this)[i]) ) ;
		return res ;
	}

}
