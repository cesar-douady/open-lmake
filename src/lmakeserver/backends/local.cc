// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/resource.h> // getrlimit

#include "generic.hh" // /!\ must be first because Python.h must be first

// PER_BACKEND : there must be a file describing each backend (providing the sub-backend class, deriving from GenericBackend if possible (simpler), else Backend)

namespace Backends::Local {

	struct LocalBackend ;

	//
	// resources
	//

	using Rsrc = uint32_t ;

	struct RsrcsData : ::vector<Rsrc> {
		// cxtors & casts
		RsrcsData(                                                 ) = default ;
		RsrcsData( size_t sz                                       ) : ::vector<Rsrc>(sz) {}
		RsrcsData( ::vmap_ss const& , ::umap_s<size_t> const& idxs ) ;
		//
		::vmap_ss mk_vmap(::vector_s const& keys) const ;
		// services
		RsrcsData& operator+=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size(),size(),rsrcs.size()) ; for( size_t i : iota(size()) ) self[i] += rsrcs[i] ; return self ; }
		RsrcsData& operator-=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size(),size(),rsrcs.size()) ; for( size_t i : iota(size()) ) self[i] -= rsrcs[i] ; return self ; }
		RsrcsData round(Backend const& be) const ;
	} ;

}

namespace std {
	template<> struct hash<Backends::Local::RsrcsData> {
		size_t operator()(Backends::Local::RsrcsData const& rd) const {
			Hash::Xxh h { rd.size() } ;
			for( auto r : rd ) h.update(r) ;
			return +h.digest() ;
		}
	} ;
}

//
// LocalBackend
//

namespace Backends::Local {

	constexpr Tag MyTag = Tag::Local ;

	struct LocalBackend : GenericBackend<MyTag,pid_t,RsrcsData,true/*IsLocal*/> {

		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			LocalBackend& self_ = *new LocalBackend ;
			s_register(MyTag,self_) ;
		}

		// statics
	private :
		static void _s_wait_job(pid_t pid) { // execute in a separate thread
			Trace trace(BeChnl,"wait",pid) ;
			::waitpid(pid,nullptr,0) ;
			trace("waited",pid) ;
		}

		// accesses
	public :
		virtual bool call_launch_after_end() const { return true ; }

		// services

		virtual void sub_config( ::vmap_ss const& dct , bool dynamic ) {
			// add an implicit resource <single> to manage jobs localized from remote backends
			Trace trace(BeChnl,"Local::config",STR(dynamic),dct) ;
			if (dynamic) {
				for( size_t i : iota(rsrc_keys.size()) ) {
					if ( i==rsrc_keys.size()-1 && rsrc_keys[i]=="<single>" && i>=dct.size() ) continue ; // skip implicit <single> key
					throw_unless( i<dct.size() && rsrc_keys[i]==dct[i].first , "cannot change resource names while lmake is running" ) ;
				}
			} else {
				rsrc_keys.reserve(dct.size()+1/*<single>*/) ;
				bool seen_single = false ;
				for( auto const& [k,v] : dct ) {
					seen_single |= k=="<single>" ;
					rsrc_idxs[k] = rsrc_keys.size() ;
					rsrc_keys.push_back(k) ;
				}
				if (!seen_single) {
					rsrc_idxs["<single>"] = rsrc_keys.size() ;
					rsrc_keys.push_back("<single>") ;
				}
			}
			capacity_ = RsrcsData( dct , rsrc_idxs  ) ; if (capacity_.size()>dct.size()) capacity_.back()/*<single>*/ = 1 ;
			occupied  = RsrcsData( rsrc_keys.size() ) ;
			//
			SWEAR( rsrc_keys.size()==capacity_.size() , rsrc_keys.size() , capacity_.size() ) ;
			for( size_t i : iota(capacity_.size()) ) public_capacity.emplace_back( rsrc_keys[i] , capacity_[i] ) ;
			trace("capacity",capacity()) ;
			_wait_queue.open( 'T' , _s_wait_job ) ;
			//
			if ( !dynamic && rsrc_idxs.contains("cpu") ) {                                                          // ensure each job can compute CRC on all cpu's in parallel
				struct rlimit rl ;
				::getrlimit(RLIMIT_NPROC,&rl) ;
				if ( rl.rlim_cur!=RLIM_INFINITY && rl.rlim_cur<rl.rlim_max ) {
					::rlim_t new_limit = rl.rlim_cur + capacity_[rsrc_idxs["cpu"]]*thread::hardware_concurrency() ;
					if ( rl.rlim_max!=RLIM_INFINITY && new_limit>rl.rlim_max ) new_limit = rl.rlim_max ;            // hard limit overflow
					rl.rlim_cur = new_limit ;
					::setrlimit(RLIMIT_NPROC,&rl) ;
				}
			}
			trace("done") ;
		}
		virtual ::vmap_s<size_t> const& capacity() const {
			return public_capacity ;
		}
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& /*capacity*/ , JobIdx ) const {
			return ::move(rsrcs) ;
		}
		//
		virtual ::vmap_ss  export_   ( RsrcsData const& rs             ) const { return rs.mk_vmap(rsrc_keys)           ; }
		virtual RsrcsData  import_   ( ::vmap_ss     && rs , Req , Job ) const { return RsrcsData(::move(rs),rsrc_idxs) ; }
		virtual ::string lacking_rsrc( RsrcsData const& rs             ) const {
			for( size_t i : iota(rs.size()) ) if (rs[i]>capacity_[i]) return "not enough resource "+rsrc_keys[i]+" (asked "+rs[i]+" but only "+capacity_[i]+" available)" ;
			return {} ;
		}
		virtual bool/*ok*/ fit_now(Rsrcs const& rs) const {
			RsrcsData const& rds = *rs ;
			for( size_t i : iota(rds.size()) ) if ( occupied[i]+rds[i] > capacity_[i] ) return false ;
			return true ;
		}
		virtual void acquire_rsrcs(Rsrcs const& rs) const {
			occupied += *rs ;
			Trace trace(BeChnl,"occupied_rsrcs",rs,'+',occupied) ;
		}
		virtual void end_rsrcs(Rsrcs const& rs) const {
			occupied -= *rs ;
			Trace trace(BeChnl,"occupied_rsrcs",rs,'-',occupied) ;
		}
		//
		virtual ::string start_job( Job , SpawnedEntry const& e ) const {
			return "pid:"s+e.id.load() ;
		}
		virtual ::pair_s<bool/*retry*/> end_job( Job , SpawnedEntry const& se , Status ) const {
			_wait_queue.push(se.id) ;                                                                               // defer wait in case job_exec process does some time consuming book-keeping
			return {{},true/*retry*/} ;                                                                             // retry if garbage
		}
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( Job , SpawnedEntry const& se ) const {               // called after job_exec has had time to start
			SWEAR(se.id) ;
			int wstatus = 0 ;
			if      (::waitpid(se.id,&wstatus,WNOHANG)==0) return {{}/*msg*/,HeartbeatState::Alive} ;               // process is still alive
			else if (!wstatus_ok(wstatus)                ) return {{}/*msg*/,HeartbeatState::Err  } ;               // process just died with an error
			else                                           return {{}/*msg*/,HeartbeatState::Lost } ;               // process died long before (already waited) or just died with no error
		}
		virtual void kill_queued_job(SpawnedEntry const& se) const {
			if (!se.live) return ;
			kill_process(se.id,SIGHUP) ;                                                                            // jobs killed here have not started yet, so we just want to kill job_exec
			_wait_queue.push(se.id) ;                                                                               // defer wait in case job_exec process does some time consuming book-keeping
		}
		virtual pid_t launch_job( ::stop_token , Job , ::vector<ReqIdx> const& , Pdate /*prio*/ , ::vector_s const& cmd_line , Rsrcs const& , bool /*verbose*/ ) const {
			Child child { .as_session=true , .cmd_line=cmd_line , .stdin_fd=Child::NoneFd , .stdout_fd=Child::NoneFd } ;
			child.spawn() ;
			pid_t pid = child.pid ;
			child.mk_daemon() ;                                                                                     // we have recorded the pid to wait and there is no fd to close
			return pid ;
		}

		// data
		::umap_s<size_t>  rsrc_idxs       ;
		::vector_s        rsrc_keys       ;
		RsrcsData         capacity_       ;
		RsrcsData mutable occupied        ;
		::vmap_s<size_t>  public_capacity ;
	private :
		DequeThread<pid_t> mutable _wait_queue ;

	} ;

	bool _inited = (LocalBackend::s_init(),true) ;

	inline RsrcsData::RsrcsData( ::vmap_ss const& m , ::umap_s<size_t> const& idxs ) {
		resize(idxs.size()) ;
		for( auto const& [k,v] : m ) {
			auto it = idxs.find(k) ;
			throw_unless( it!=idxs.end() , "no resource ",k," for backend ",MyTag ) ;
			SWEAR( it->second<size() , it->second , size() ) ;
			try        { self[it->second] = from_string_rsrc<Rsrc>(k,v) ;            }
			catch(...) { throw "cannot convert resource "+k+" from "+v+" to a int" ; }
		}
	}

	inline ::vmap_ss RsrcsData::mk_vmap(::vector_s const& keys) const {
		::vmap_ss res ; res.reserve(keys.size()) ;
		for( size_t i : iota(keys.size()) ) if (self[i]) res.emplace_back( keys[i] , to_string_rsrc(keys[i],self[i]) ) ;
		return res ;
	}

	inline RsrcsData RsrcsData::round(Backend const& be) const {
		LocalBackend const& lbe = dynamic_cast<LocalBackend const&>(be) ;
		RsrcsData const&    c   = lbe.capacity_                         ;
		//
		RsrcsData res ; res.reserve(size()) ;
		for( size_t i : iota(size()) ) {
			SWEAR( self[i]<=c[i] , lbe.rsrc_keys[i] , self[i] , c[i] ) ; // self must have been checked to fit within capacity
			res.push_back( ::min(round_rsrc(self[i]),c[i]) ) ;           // round up, but not above capacity or job will never be launched
		}
		return res ;
	}

}
