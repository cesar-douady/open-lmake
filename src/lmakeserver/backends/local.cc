// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/sysinfo.h>
#include <sys/resource.h>

#include "generic.hh"

// PER_BACKEND : there must be a file describing each backend (providing the sub-backend class, deriving from GenericBackend if possible (simpler), else Backend)

namespace Backends::Local {

	//
	// resources
	//

	using Rsrc = uint32_t ;
	struct RsrcAsk {
		friend ::ostream& operator<<( ::ostream& , RsrcAsk const& ) ;
		bool operator==(RsrcAsk const&) const = default ;                      // XXX : why is this necessary ?
		// data
		Rsrc min = 0/*garbage*/ ;
		Rsrc max = 0/*garbage*/ ;
	} ;

	struct RsrcsData : ::vector<Rsrc> {
		// cxtors & casts
		RsrcsData(                                                 ) = default ;
		RsrcsData( size_t sz                                       ) : ::vector<Rsrc>(sz) {}
		RsrcsData( ::vmap_ss const& , ::umap_s<size_t> const& idxs ) ;
		::vmap_ss mk_vmap(::vector_s const& keys) const ;
		// services
		RsrcsData& operator+=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size(),size(),rsrcs.size()) ; for( size_t i=0 ; i<size() ; i++ ) (*this)[i] += rsrcs[i] ; return *this ; }
		RsrcsData& operator-=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size(),size(),rsrcs.size()) ; for( size_t i=0 ; i<size() ; i++ ) (*this)[i] -= rsrcs[i] ; return *this ; }
	} ;

	struct RsrcsDataAsk : ::vector<RsrcAsk> {
		// cxtors & casts
		RsrcsDataAsk(                                             ) = default ;
		RsrcsDataAsk( ::vmap_ss && , ::umap_s<size_t> const& idxs ) ;
		// services
		bool fit_in( RsrcsData const& occupied , RsrcsData const& capacity ) const {                          // true if all resources fit within capacity on top of occupied
			for( size_t i=0 ; i<size() ; i++ ) if ( occupied[i]+(*this)[i].min > capacity[i] ) return false ;
			return true ;
		}
		bool fit_in(RsrcsData const& capacity) const {                                            // true if all resources fit within capacity
			for( size_t i=0 ; i<size() ; i++ ) if ( (*this)[i].min > capacity[i] ) return false ;
			return true ;
		}
		RsrcsData within( RsrcsData const& occupied , RsrcsData const& capacity ) const { // what fits within capacity on top of occupied
			RsrcsData res ; res.reserve(size()) ;
			for( size_t i=0 ; i<size() ; i++ ) {
				SWEAR( occupied[i]+(*this)[i].min <= capacity[i] , *this , occupied , capacity ) ;
				res.push_back(::min( (*this)[i].max , capacity[i]-occupied[i] )) ;
			}
			return res ;
		}
	} ;

}

namespace std {
	template<> struct hash<Backends::Local::RsrcsData> {
		size_t operator()(Backends::Local::RsrcsData const& rs) const {
			Hash::Xxh h ;
			h.update(rs.size()) ;
			for( auto r : rs ) h.update(r) ;
			return +::move(h).digest() ;
		}
	} ;
	template<> struct hash<Backends::Local::RsrcsDataAsk> {
		size_t operator()(Backends::Local::RsrcsDataAsk const& rsa) const {
			Hash::Xxh h ;
			h.update(rsa.size()) ;
			for( auto ra : rsa ) {
				h.update(ra.min) ;
				h.update(ra.max) ;
			}
			return +::move(h).digest() ;
		}
	} ;
}

//
// LocalBackend
//

namespace Backends::Local {

	constexpr Tag MyTag = Tag::Local ;

	struct LocalBackend : GenericBackend<MyTag,pid_t,RsrcsData,RsrcsDataAsk,true/*IsLocal*/> {

		static void _wait_thread_func( ::stop_token stop , LocalBackend* self ) {
			t_thread_key = 'L' ;
			self->_wait_jobs(stop) ;
		}

		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			LocalBackend& self = *new LocalBackend ;
			s_register(MyTag,self) ;
		}

		// accesses

		virtual Bool3 call_launch_after_start() const { return No  ; }         // if Maybe, only launch jobs w/ same resources
		virtual Bool3 call_launch_after_end  () const { return Yes ; }         // .

		// services

		::vmap_s<size_t> n_tokenss() const {
			return capacity() ;
		}

		virtual void config( ::vmap_ss const& dct , bool dynamic ) {
			if (dynamic) {
				/**/                                         if (rsrc_keys.size()!=dct.size()) throw "cannot change resource names while lmake is running"s ;
				for( size_t i=0 ; i<rsrc_keys.size() ; i++ ) if (rsrc_keys[i]!=dct[i].first  ) throw "cannot change resource names while lmake is running"s ;
			} else {
				for( auto const& [k,v] : dct ) {
					rsrc_idxs[k] = rsrc_keys.size() ;
					rsrc_keys.push_back(k) ;
				}
			}
			capacity_ = RsrcsData( dct , rsrc_idxs ) ;
			occupied  = RsrcsData(rsrc_keys.size() ) ;
			//
			SWEAR( rsrc_keys.size()==capacity_.size() , rsrc_keys.size() , capacity_.size() ) ;
			for( size_t i=0 ; i<capacity_.size() ; i++ ) public_capacity.emplace_back( rsrc_keys[i] , capacity_[i] ) ;
			Trace("config",MyTag,"capacity",'=',capacity_) ;
			static ::jthread wait_jt{_wait_thread_func,this} ;
			//
			if ( !dynamic && rsrc_idxs.contains("cpu") ) {                     // ensure each job can compute CRC on all cpu's in parallel
				struct rlimit rl ;
				::getrlimit(RLIMIT_NPROC,&rl) ;
				if ( rl.rlim_cur!=RLIM_INFINITY && rl.rlim_cur<rl.rlim_max ) {
					::rlim_t new_limit = rl.rlim_cur + capacity_[rsrc_idxs["cpu"]]*thread::hardware_concurrency() ;
					if ( rl.rlim_max!=RLIM_INFINITY && new_limit>rl.rlim_max ) new_limit = rl.rlim_max ;            // hard limit overflow
					rl.rlim_cur = new_limit ;
					::setrlimit(RLIMIT_NPROC,&rl) ;
				}
			}
		}
		virtual ::vmap_s<size_t> const& capacity() const {
			return public_capacity ;
		}
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& /*capacity*/ ) const {
			return ::move(rsrcs) ;
		}
		//
		virtual bool/*ok*/   fit_eventually( RsrcsDataAsk const& rsa          ) const { return rsa. fit_in(         capacity_)     ; }
		virtual bool/*ok*/   fit_now       ( RsrcsAsk     const& rsa          ) const { return rsa->fit_in(occupied,capacity_)     ; }
		virtual RsrcsData    adapt         ( RsrcsDataAsk const& rsa          ) const { return rsa. within(occupied,capacity_)     ; }
		virtual ::vmap_ss    export_       ( RsrcsData    const& rs           ) const { return rs.mk_vmap(rsrc_keys)               ; }
		virtual RsrcsDataAsk import_       ( ::vmap_ss        && rsa , ReqIdx ) const { return RsrcsDataAsk(::move(rsa),rsrc_idxs) ; }
		//
		virtual ::string start_job( JobIdx , SpawnedEntry const& e ) const {
			return to_string("pid:",e.id) ;
		}
		virtual ::pair_s<bool/*retry*/> end_job( JobIdx , SpawnedEntry const& se , Status ) const {
			occupied -= *se.rsrcs ;
			Trace trace("end","occupied_rsrcs",'-',occupied) ;
			_wait_queue.push(se.id) ;                                          // defer wait in case job_exec process does some time consuming book-keeping
			return {{},true/*retry*/} ;                                        // retry if garbage
		}
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( JobIdx j , SpawnedEntry const& se ) const { // called after job_exec has had time to start
			kill_queued_job(j,se) ;                                                                        // ensure job_exec is dead or will die shortly
			return {{}/*msg*/,HeartbeatState::Lost} ;
		}
		virtual void kill_queued_job( JobIdx , SpawnedEntry const& se ) const {
			kill_process(se.id,SIGHUP) ;                                        // jobs killed here have not started yet, so we just want to kill job_exec
			_wait_queue.push(se.id) ;                                           // defer wait in case job_exec process does some time consuming book-keeping
		}
		virtual pid_t launch_job( JobIdx , Pdate /*prio*/ , ::vector_s const& cmd_line , Rsrcs const& rsrcs , bool /*verbose*/ ) const {
			Child child { true/*as_group*/ , cmd_line , Child::None , Child::None } ;
			pid_t pid = child.pid ;
			child.mk_daemon() ;                                                // we have recorded the pid to wait and there is no fd to close
			if (pid<0) throw "cannot spawn process"s ;
			occupied += *rsrcs ;
			Trace trace("occupied_rsrcs",'+',occupied) ;
			return pid ;
		}

	private :
		void _wait_jobs(::stop_token stop) {                                   // execute in a separate thread
			Trace trace("_wait_jobs",MyTag) ;
			for(;;) {
				auto [popped,pid] = _wait_queue.pop(stop) ;
				if (!popped) return ;
				trace("wait",pid) ;
				::waitpid(pid,nullptr,0) ;
				trace("waited",pid) ;
			}
		}

		// data
	public :
		::umap_s<size_t>  rsrc_idxs       ;
		::vector_s        rsrc_keys       ;
		RsrcsData         capacity_       ;
		RsrcsData mutable occupied        ;
		::vmap_s<size_t>  public_capacity ;
	private :
		ThreadQueue<pid_t> mutable _wait_queue ;

	} ;

	bool _inited = (LocalBackend::s_init(),true) ;

	::ostream& operator<<( ::ostream& os , RsrcAsk const& ra ) {
		return os << ra.min <<'<'<< ra.max ;
	}

	inline RsrcsData::RsrcsData( ::vmap_ss const& m , ::umap_s<size_t> const& idxs ) {
		resize(idxs.size()) ;
		for( auto const& [k,v] : m ) {
			auto it = idxs.find(k) ;
			if (it==idxs.end()) throw to_string("no resource ",k," for backend ",mk_snake(MyTag)) ;
			SWEAR( it->second<size() , it->second , size() ) ;
			try        { (*this)[it->second] = from_string_rsrc<Rsrc>(k,v) ;                                     }
			catch(...) { throw to_string("cannot convert resource ",k," from ",v," to a ",typeid(Rsrc).name()) ; }
		}
	}

	inline RsrcsDataAsk::RsrcsDataAsk( ::vmap_ss && m , ::umap_s<size_t> const& idxs ) {
		resize(idxs.size()) ;
		for( auto && [k,v] : ::move(m) ) {
			auto it = idxs.find(k) ;
			if (it==idxs.end()) throw to_string("no resource ",k," for backend ",mk_snake(MyTag)) ;
			SWEAR( it->second<size() , it->second , size() ) ;
			RsrcAsk& entry = (*this)[it->second] ;
			try {
				size_t pos = v.find('<') ;
				if (pos==Npos) { entry.min = from_string_rsrc<Rsrc>(k,::move(v)      ) ; entry.max = entry.min                                 ; }
				else           { entry.min = from_string_rsrc<Rsrc>(k,v.substr(0,pos)) ; entry.max = from_string_rsrc<Rsrc>(k,v.substr(pos+1)) ; }
			} catch(...) {
				throw to_string("cannot convert ",v," to a ",typeid(Rsrc).name()," nor a min/max pair separated by <") ;
			}
		}
	}

	::vmap_ss RsrcsData::mk_vmap(::vector_s const& keys) const {
		::vmap_ss res ; res.reserve(keys.size()) ;
		for( size_t i=0 ; i<keys.size() ; i++ ) {
			if (!(*this)[i]) continue ;
			::string const& key = keys[i] ;
			if ( key=="mem" || key=="tmp" ) res.emplace_back( key , to_string((*this)[i],'M') ) ;
			else                            res.emplace_back( key , to_string((*this)[i]    ) ) ;
		}
		return res ;
	}

}
