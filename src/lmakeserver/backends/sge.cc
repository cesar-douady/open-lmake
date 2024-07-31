// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

>>> file under construction <<<

#include "generic.hh"

namespace Backends::Sge {

	//
	// daemon info
	//

	struct Daemon {
		friend ::ostream& operator<<( ::ostream& , Daemon const& ) ;
		// data
		::map_s<size_t> licenses ;                           // licenses sampled from daemon
	} ;

	//
	// resources
	//

	struct RsrcsDataSingle {
		friend ::ostream& operator<<( ::ostream& , RsrcsDataSingle const& ) ;
		// accesses
		bool operator==(RsrcsDataSingle const&) const = default ;
		// data
		uint16_t cpu      = 0 ; // number of logical cpu (qsub    --cpus-per-task option)
		uint32_t mem      = 0 ; // memory   in MB        (qsub    --mem           option) default : illegal (memory reservation is compulsery)
		uint32_t tmp      = 0 ; // tmp disk in MB        (qsub    --tmp           option)
		::string licenses ;     // licenses              (qsub -L,--licenses      option)
	} ;

	struct RsrcsData : ::vector<RsrcsDataSingle> {
		using Base = ::vector<RsrcsDataSingle> ;
		// cxtors & casts
		RsrcsData(                               ) = default ;
		RsrcsData( ::vmap_ss&& , Daemon , JobIdx ) ;
		// services
		::vmap_ss mk_vmap(void) const ;
	} ;

	RsrcsData blend( RsrcsData&& rsrcs , RsrcsData const& force ) ;

}

namespace std {
	template<> struct hash<Backends::Sge::RsrcsData> {
		size_t operator()(Backends::Sge::RsrcsData const& rs) const {
			Hash::Xxh h{rs.size()} ;
			for( auto r : rs ) {
				h.update(r.cpu     ) ;
				h.update(r.mem     ) ;
				h.update(r.tmp     ) ;
				h.update(r.licenses) ;
			}
			return +h.digest() ;
		}
	} ;
}

//
// SgeBackend
//

namespace Backends::Sge {

	using SgeId = uint32_t ;

	Mutex<MutexLvl::Sge> _sge_mutex ; // ensure no more than a single outstanding request to daemon

	void sge_init() ;

	void                      sge_cancel      (SgeId  sge_id) ;
	::pair_s<Bool3/*job_ok*/> sge_job_state   (SgeId  sge_id) ;
	::string                  read_stderr     (JobIdx       ) ;
	Daemon                    sge_sense_daemon(             ) ;
	//
	SgeId sge_spawn_job( ::stop_token , ::string const& key , JobIdx , ::vector<ReqIdx> const& , int32_t nice , ::vector_s const& cmd_line , RsrcsData const& rsrcs , bool verbose ) ;

	constexpr Tag MyTag = Tag::Sge ;

	struct SgeBackend
	:	             GenericBackend<MyTag,SgeId,RsrcsData,RsrcsData,false/*IsLocal*/>
	{	using Base = GenericBackend<MyTag,SgeId,RsrcsData,RsrcsData,false/*IsLocal*/> ;

		struct SpawnedMap : ::umap<Rsrcs,JobIdx> {
			// count number of jobs spawned but not started yet
			// no entry is equivalent to entry with 0
			void inc(Rsrcs rs) {
				try_emplace(rs,0).first->second++ ; // create 0 entry if necessary
			}
			void dec(Rsrcs rs) {                    // entry must exist
				auto sit = find(rs) ;
				if(!--sit->second) erase(sit) ;     // no entry means 0, so collect when possible (questionable)
			}
			JobIdx n_spawned(Rsrcs rs) {
				auto it = find(rs) ;
				if (it==end()) return 0          ;  // no entry means 0
				else           return it->second ;
			}
		} ;

		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			s_register(MyTag,*new SgeBackend) ;
		}

		// static data
		static TimedDequeThread<SgeId> _s_sge_cancel_thread ; // when a req is killed, a lot of queued jobs may be canceled, better to do it in a separate thread

		// accesses

		virtual bool call_launch_after_start() const { return true ; }

		// services

		virtual void sub_config( vmap_ss const& dct , bool dynamic ) {
			Trace trace(BeChnl,"Sge::config",STR(dynamic),dct) ;
			if (!dynamic) sge_init() ;
			_s_sge_cancel_thread.open('C',sge_cancel) ;
			//
			repo_key = base_name(no_slash(*g_root_dir_s))+':' ; // cannot put this code directly as init value as g_root_dir_s is not available early enough
			for( auto const& [k,v] : dct ) {
				try {
					switch (k[0]) {
						case 'n' : if(k=="n_max_queued_jobs") { n_max_queued_jobs = from_string<uint32_t>(v) ; continue ; } break ;
						case 'r' : if(k=="repo_key"         ) { repo_key          =                       v  ; continue ; } break ;
						case 'u' : if(k=="use_nice"         ) { use_nice          = from_string<bool    >(v) ; continue ; } break ;
						default : ;
					}
				} catch (::string const& e) { trace("bad_val",k,v) ; throw "wrong value for entry "   +k+": "+v ; }
				/**/                        { trace("bad_key",k  ) ; throw "unexpected config entry: "+k        ; }
			}
			if (!dynamic) daemon = sge_sense_daemon() ;
			trace("done") ;
		}

		virtual ::vmap_ss descr() const {
			return {} ;
		}

		virtual ::vmap_s<size_t> n_tokenss() const {
			return {} ;
		}

		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& capacity ) const {
			bool             single = false             ;
			::umap_s<size_t> capa   = mk_umap(capacity) ;
			::umap_s<size_t> rs     ;
			for( auto&& [k,v] : rsrcs ) {
				if (capa.contains(k)) { size_t s = from_string_rsrc<size_t>(k,v) ; rs[::move(k)] = s ; } // capacities of local backend are only integer information
			}
			::vmap_ss res ;
			if (single) for( auto&& [k,v] : rs ) { ::string s = to_string_rsrc(k,        capa[k] ) ; res.emplace_back( ::move(k) , ::move(s) ) ; }
			else        for( auto&& [k,v] : rs ) { ::string s = to_string_rsrc(k,::min(v,capa[k])) ; res.emplace_back( ::move(k) , ::move(s) ) ; }
			return res ;
		}

		virtual void close_req(ReqIdx req) {
			Base::close_req(req) ;
			if(!reqs) SWEAR(!spawned_rsrcs,spawned_rsrcs) ;
		}

		virtual ::vmap_ss export_( RsrcsData const& rs                           ) const { return rs.mk_vmap()            ; }
		virtual RsrcsData import_( ::vmap_ss     && rsa , ReqIdx req , JobIdx ji ) const { return {::move(rsa),daemon,ji} ; }
		//
		virtual bool/*ok*/ fit_now(RsrcsAsk const& rsa) const {
			bool res = spawned_rsrcs.n_spawned(rsa) < n_max_queued_jobs ;
			return res ;
		}
		virtual Rsrcs acquire_rsrcs(RsrcsAsk const& rsa) const {
			spawned_rsrcs.inc(rsa) ;
			return rsa ;
		}
		virtual void start_rsrcs(Rsrcs const& rs) const {
			spawned_rsrcs.dec(rs) ;
		}
		virtual ::string start_job( JobIdx , SpawnedEntry const& se ) const {
			SWEAR(+se.rsrcs) ;
			return "sge_id:"s+se.id.load() ;
		}
		virtual ::pair_s<bool/*retry*/> end_job( JobIdx j , SpawnedEntry const& se , Status s ) const {
			...
		}
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( JobIdx j , SpawnedEntry const& se ) const {
			...
		}
		virtual void kill_queued_job(SpawnedEntry const& se) const {
			if (!se.zombie) _s_sge_cancel_thread.push(se.id) ;                                           // asynchronous (as faster and no return value) cancel
		}
		virtual SgeId launch_job( ::stop_token st , JobIdx j , ::vector<ReqIdx> const& reqs , Pdate prio , ::vector_s const& cmd_line , Rsrcs const& rs , bool verbose ) const {
			SgeId id = sge_spawn_job( st , repo_key , j , reqs , nice , cmd_line , *rs , verbose ) ;
			Trace trace(BeChnl,"Sge::launch_job",repo_key,j,id,cmd_line,rs,STR(verbose)) ;
			return id ;
		}

		// data
		SpawnedMap mutable  spawned_rsrcs     ;         // number of spawned jobs queued in sge queue
		::vector<RsrcsData> req_forces        ;         // indexed by req, resources forced by req
		uint32_t            n_max_queued_jobs = -1    ; // no limit by default
		bool                use_nice          = false ;
		::string            repo_key          ;         // a short identifier of the repository
	} ;

	TimedDequeThread<SgeId> SgeBackend::_s_sge_cancel_thread ;

	//
	// init
	//

	bool _inited = (SgeBackend::s_init(),true) ;

	//
	// RsrcsData
	//

	::ostream& operator<<( ::ostream& os , RsrcsDataSingle const& rsds ) {
		/**/                os <<'('<< rsds.cpu       ;
		if ( rsds.mem     ) os <<','<< rsds.mem<<"MB" ;
		if ( rsds.tmp     ) os <<','<< rsds.tmp<<"MB" ;
		if (+rsds.licenses) os <<','<< rsds.licenses  ;
		return              os <<')'                  ;
	}

	static void _sort_entry(::string& s) {
		if (s.find(',')==Npos) return ;
		::vector_s v = split(s,',') ;
		SWEAR(v.size()>1) ;
		sort(v) ;
		s = v[0] ;
		for( size_t i=1 ; i<v.size() ; i++ ) s<<','<<v[i] ;
	}
	inline RsrcsData::RsrcsData( ::vmap_ss&& m , Daemon d , JobIdx ji ) : Base{1} { // ensure we have at least 1 entry as we sometimes access element 0
		sort(m) ;
		for( auto&& [kn,v] : ::move(m) ) {
			size_t           p    = kn.find(':')                                           ;
			::string         k    = p==Npos ? ::move(kn) :               kn.substr(0  ,p)  ;
			uint32_t         n    = p==Npos ? 0  : from_string<uint32_t>(kn.substr(p+1  )) ;
			RsrcsDataSingle& rsds = grow(*this,n)                                          ;
			//
			auto chk_first = [&]()->void {
				if (n) throw k+" is only for 1st component of job, not component "+n ;
			} ;
			switch (k[0]) {
				case 'c' : if (k=="cpu"     ) {                                rsds.cpu      = from_string_with_units<    uint32_t>(v) ; continue ; } break ;
				case 'm' : if (k=="mem"     ) {                                rsds.mem      = from_string_with_units<'M',uint32_t>(v) ; continue ; } break ; // dont ask mem if not managed
				case 't' : if (k=="tmp"     ) {                                rsds.tmp      = from_string_with_units<'M',uint32_t>(v) ; continue ; } break ;
				case 'l' : if (k=="licenses") { chk_first() ; _sort_entry(v) ; rsds.licenses = ::move                              (v) ; continue ; } break ; // normalize to favor resources sharing
				default : ;
			}
			if ( auto it = d.licenses.find(k) ; it!=d.licenses.end() ) {
				chk_first() ;
				if (+rsds.licenses) rsds.licenses += ',' ;
				rsds.licenses += k+':'+v ;
				continue ;
			}
			//
			throw "no resource "+k+" for backend "+snake(MyTag) ;

		}
		if ( !(*this)[0].mem ) throw "must reserve memory when managed by sge daemon, consider "s+Job(ji)->rule->name+".resources={'mem':'1M'}" ;
	}
	::vmap_ss RsrcsData::mk_vmap(void) const {
		::vmap_ss res ;
		// It may be interesting to know the number of cpu reserved to know how many thread to launch in some situation
		res.emplace_back( "cpu" , to_string_with_units     ((*this)[0].cpu) ) ;
		res.emplace_back( "mem" , to_string_with_units<'M'>((*this)[0].mem) ) ;
		res.emplace_back( "tmp" , to_string_with_units<'M'>((*this)[0].tmp) ) ;
		return res ;
	}

	RsrcsData blend( RsrcsData&& rsrcs , RsrcsData const& force ) {
		if (+force)
			for( size_t i=0 ; i<::min(rsrcs.size(),force.size()) ; i++ ) {
				RsrcsDataSingle const& force1 = force[i] ;
				if ( force1.cpu     ) rsrcs[i].cpu      = force1.cpu      ;
				if ( force1.mem     ) rsrcs[i].mem      = force1.mem      ;
				if ( force1.tmp     ) rsrcs[i].tmp      = force1.tmp      ;
				if (+force1.licenses) rsrcs[i].licenses = force1.licenses ;
			}
		return ::move(rsrcs) ;
	}

}
