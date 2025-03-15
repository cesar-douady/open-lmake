// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>

#include <filesystem>

#include <slurm/slurm.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized" // seems to be necessary for compiling with -fsanitize
#include "ext/cxxopts.hpp"
#pragma GCC diagnostic pop

#include "generic.hh" // /!\ must be first because Python.h must be first

using namespace Disk ;

namespace Backends::Slurm {

	static constexpr int SlurmSpawnTrials  = 15 ;
	static constexpr int SlurmCancelTrials = 10 ;

	//
	// daemon info
	//

	struct Daemon {
		friend ::string& operator+=( ::string& , Daemon const& ) ;
		// data
		Pdate           time_origin { "2023-01-01 00:00:00" } ; // this leaves room til 2091
		float           nice_factor { 1                     } ; // conversion factor in the form of number of nice points per second
		::map_s<size_t> licenses    ;                           // licenses sampled from daemon
		bool            manage_mem  = false/*garbage*/        ;
	} ;

	//
	// resources
	//

	struct RsrcsDataSingle {
		friend ::string& operator+=( ::string& , RsrcsDataSingle const& ) ;
		// accesses
		bool operator==(RsrcsDataSingle const&) const = default ;
		// services
		RsrcsDataSingle round() const {
			RsrcsDataSingle res = self ;
			res.cpu = round_rsrc(res.cpu) ;
			res.mem = round_rsrc(res.mem) ;
			res.tmp = round_rsrc(res.tmp) ;
			return res ;
		}
		// data
		uint16_t cpu      = 0 ; // number of logical cpu  (sbatch    --cpus-per-task option)
		uint32_t mem      = 0 ; // memory   in MB         (sbatch    --mem           option) default : illegal (memory reservation is compulsery)
		uint32_t tmp      = 0 ; // tmp disk in MB         (sbatch    --tmp           option) default : dont manage tmp size (provide infinite storage, reserv none)
		::string excludes ;     // list of excludes nodes (sbatch -x,--exclude       option)
		::string features ;     // features/contraint     (sbatch -C,--constraint    option)
		::string gres     ;     // generic resources      (sbatch    --gres          option)
		::string licenses ;     // licenses               (sbatch -L,--licenses      option)
		::string nodes    ;     // list of required nodes (sbatch -w,--nodelist      option)
		::string part     ;     // partition name         (sbatch -p,--partition     option)
		::string qos      ;     // quality of service     (sbatch -q,--qos           option)
		::string reserv   ;     // reservation            (sbatch -r,--reservation   option)
	} ;

	struct RsrcsData : ::vector<RsrcsDataSingle> {
		using Base = ::vector<RsrcsDataSingle> ;
		// cxtors & casts
		RsrcsData(                               ) = default ;
		RsrcsData( ::vmap_ss&& , Daemon , JobIdx ) ;
		// services
		::vmap_ss mk_vmap(void) const ;
		RsrcsData round(Backend const&) const {
			RsrcsData res ;
			for( RsrcsDataSingle rds : self ) res.push_back(rds.round()) ;
			return res ;
		}
	} ;

	RsrcsData blend( RsrcsData&& rsrcs , RsrcsData const& force ) ;

}

namespace std {
	template<> struct hash<Backends::Slurm::RsrcsData> {
		size_t operator()(Backends::Slurm::RsrcsData const& rs) const {
			Hash::Xxh h{rs.size()} ;
			for( auto r : rs ) {
				h.update(r.cpu     ) ;
				h.update(r.mem     ) ;
				h.update(r.tmp     ) ;
				h.update(r.features) ;
				h.update(r.gres    ) ;
				h.update(r.licenses) ;
				h.update(r.part    ) ;
			}
			return +h.digest() ;
		}
	} ;
}

//
// SlurmBackend
//

namespace Backends::Slurm {

	using SlurmId = uint32_t ;

	Mutex<MutexLvl::Slurm> _slurm_mutex ; // ensure no more than a single outstanding request to daemon

	void slurm_init(const char* config_file) ;

	RsrcsData                 parse_args        (::string const& args    ) ;
	void                      slurm_cancel      (SlurmId         slurm_id) ;
	::pair_s<Bool3/*job_ok*/> slurm_job_state   (SlurmId         slurm_id) ;
	::string                  read_stderr       (Job                     ) ;
	Daemon                    slurm_sense_daemon(                        ) ;
	//
	SlurmId slurm_spawn_job(
		::stop_token
	,	::string         const& key
	,	Job
	,	::vector<ReqIdx> const&
	,	int32_t                 nice
	,	::vector_s       const& cmd_line
	,	const char**            env
	,	RsrcsData        const&
	,	bool                    verbose
	) ;

	constexpr Tag MyTag = Tag::Slurm ;

	struct SlurmBackend
	:	             GenericBackend<MyTag,'U'/*LaunchThreadKey*/,RsrcsData,false/*IsLocal*/>
	{	using Base = GenericBackend<MyTag,'U'/*LaunchThreadKey*/,RsrcsData,false/*IsLocal*/> ;

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
			s_register(MyTag,*new SlurmBackend) ;
		}

		// static data
		static DequeThread<SlurmId> _s_slurm_cancel_thread ; // when a req is killed, a lot of queued jobs may be canceled, better to do it in a separate thread

		// accesses

		virtual bool call_launch_after_start() const { return true ; }

		// services

		virtual void sub_config( ::vmap_ss const& dct , ::vmap_ss const& env_ , bool dynamic ) {
			Trace trace(BeChnl,"Slurm::config",STR(dynamic),dct) ;
			//
			const char* config_file = nullptr ;
			repo_key = base_name(no_slash(*g_repo_root_s))+':' ; // cannot put this code directly as init value as g_repo_root_s is not available early enough
			for( auto const& [k,v] : dct ) {
				try {
					switch (k[0]) {
						case 'c' : if(k=="config"           ) { config_file       = v.c_str()                ; continue ; } break ;
						case 'n' : if(k=="n_max_queued_jobs") { n_max_queued_jobs = from_string<uint32_t>(v) ; continue ; } break ;
						case 'r' : if(k=="repo_key"         ) { repo_key          =                       v  ; continue ; } break ;
						case 'u' : if(k=="use_nice"         ) { use_nice          = from_string<bool    >(v) ; continue ; } break ;
					DN}
				} catch (::string const& e) { trace("bad_val",k,v) ; throw "wrong value for entry "   +k+": "+v ; }
				/**/                        { trace("bad_key",k  ) ; throw "unexpected config entry: "+k        ; }
			}
			if (!dynamic) {
				slurm_init(config_file) ;
				daemon = slurm_sense_daemon() ;
				_s_slurm_cancel_thread.open('K',slurm_cancel) ;
			}
			//
			_slurm_env.reset(new const char*[env_.size()+1]) ;
			{	size_t i = 0 ;
				_slurm_env_vec.clear() ;
				for( auto const& [k,v] : env_ ) {
					_slurm_env_vec.push_back(k+'='+v) ;
					_slurm_env[i++] = _slurm_env_vec.back().c_str() ;
				}
				_slurm_env[i] = "" ;                             // slurm env is terminated with an empty string, not a nullptr
			}
			trace("done") ;
		}

		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& capacity , JobIdx ji ) const { // transform remote resources into local resources
			Trace trace(BeChnl,"mk_lcl",rsrcs,ji) ;
			::umap_s<size_t> capa   = mk_umap(capacity)             ;
			RsrcsData        rd     { ::move(rsrcs) , daemon , ji } ;
			::umap_s<size_t> lr     ;
			::vmap_ss        res    ;
			bool             single = false                         ;
			for( RsrcsDataSingle const& rds : rd ) {
				lr["cpu"] += rds.cpu ;
				lr["mem"] += rds.mem ;
				lr["tmp"] += rds.tmp ;
				if (+rds.features) single = true ;
				if (+rds.part    ) single = true ;
				for( ::string const& r : {rds.gres,rds.licenses} )
					if (+r)
						for( ::string const& x : split(r,',') ) {
							size_t   pos = x.rfind(':')    ;
							::string k   = x.substr(0,pos) ;
							if (pos==Npos)       lr[k] += 1                                   ;
							else           try { lr[k] += from_string_rsrc(k,x.substr(pos+1)) ; } catch (::string const&) { single = true ; }
						}
			}
			for( auto& [k,v] : lr ) {
				auto it = capa.find(k) ;
				if      (it==capa.end())   single = true ;
				else if (v>it->second  ) { single = true ; res.emplace_back( ::move(k) , to_string_rsrc(k,it->second) ) ; }
				else                                       res.emplace_back( ::move(k) , to_string_rsrc(k,v         ) ) ;
			}
			if (single) res.emplace_back( "<single>" , "1" ) ;
			trace("done",res) ;
			return res ;
		}

		virtual ::vmap_ss descr() const {
			::vmap_ss res {
				{ "manage memory" , daemon.manage_mem?"true":"false" }
			} ;
			for( auto const& [k,v] : daemon.licenses ) res.emplace_back(k,::to_string(v)) ;
			return res ;
		}

		virtual void open_req( Req req , JobIdx n_jobs ) {
			Base::open_req(req,n_jobs) ;
			grow(req_forces,+req) = parse_args(Req(req)->options.flag_args[+ReqFlag::Backend]) ;
		}

		virtual void close_req(Req req) {
			Base::close_req(req) ;
			if(!reqs) SWEAR(!spawned_rsrcs,spawned_rsrcs) ;
		}

		virtual ::vmap_ss export_( RsrcsData const& rs                    ) const { return rs.mk_vmap()                                       ; }
		virtual RsrcsData import_( ::vmap_ss     && rsa , Req req , Job j ) const { return blend( {::move(rsa),daemon,+j} ,req_forces[+req] ) ; }
		//
		virtual bool/*ok*/ fit_now(Rsrcs const& rs) const {
			bool res = spawned_rsrcs.n_spawned(rs) < n_max_queued_jobs ;
			return res ;
		}
		virtual void acquire_rsrcs(Rsrcs const& rs) const {
			spawned_rsrcs.inc(rs) ;
		}
		virtual void start_rsrcs(Rsrcs const& rs) const {
			spawned_rsrcs.dec(rs) ;
		}
		virtual ::string start_job( Job , SpawnedEntry const& se ) const {
			SWEAR(+se.rsrcs) ;
			return "slurm_id:"s+se.id.load() ;
		}
		virtual ::pair_s<bool/*retry*/> end_job( Job j , SpawnedEntry const& se , Status s ) const {
			if ( !se.verbose && s==Status::Ok ) return {{},true/*retry*/} ;                          // common case, must be fast, if job is in error, better to ask slurm why, e.g. could be OOM
			::pair_s<Bool3/*job_ok*/> info ;
			for( int c : iota(2) ) {
				Delay d { 0.01 }                                               ;
				Pdate e = Pdate(New) + ::max(g_config->network_delay,Delay(1)) ; // ensure a reasonable minimum
				for( Pdate pd = New ;; pd+=d ) {
					info = slurm_job_state(se.id) ;
					if (info.second!=Maybe) goto JobDead ;
					if (pd>=e             ) break        ;
					d.sleep_for() ;                                              // wait, hoping job is dying, double delay every loop until hearbeat tick
					d = ::min( d+d , g_config->heartbeat_tick ) ;
				}
				if (c==0) _s_slurm_cancel_thread.push(se.id) ;                   // if still alive after network delay, (asynchronously as faster and no return value) cancel job and retry
			}
			info.first = "job is still alive" ;
		JobDead :
			if ( se.verbose && +info.first ) {     // /!\ only read stderr when something to say as what appears to be a filesystem bug (seen with ceph) sometimes blocks !
				::string stderr = read_stderr(j) ;
				if (+stderr) info.first <<set_nl<< stderr ;
			}
			return { info.first , info.second!=No } ;
		}
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( Job j , SpawnedEntry const& se ) const {
			::pair_s<Bool3/*job_ok*/> info = slurm_job_state(se.id) ;
			if (info.second==Maybe) return {{}/*msg*/,HeartbeatState::Alive} ;
			//
			if ( se.verbose && +info.first ) {     // /!\ only read stderr when something to say as what appears to be a filesystem bug (seen with ceph) sometimes blocks !
				::string stderr = read_stderr(j) ;
				if (+stderr) info.first <<set_nl<< stderr ;
			}
			if (info.second==Yes) return { info.first , HeartbeatState::Lost } ;
			else                  return { info.first , HeartbeatState::Err  } ;
		}
		virtual void kill_queued_job(SpawnedEntry const& se) const {
			if (!se.zombie) _s_slurm_cancel_thread.push(se.id) ;                                        // asynchronous (as faster and no return value) cancel
		}
		virtual SpawnId launch_job( ::stop_token st , Job j , ::vector<ReqIdx> const& reqs , Pdate prio , ::vector_s const& cmd_line , SpawnedEntry const& se ) const {
			int32_t nice = use_nice ? int32_t((prio-daemon.time_origin).sec()*daemon.nice_factor) : 0 ;
			nice &= 0x7fffffff ;                                                                        // slurm will not accept negative values, default values overflow in ... 2091
			SlurmId id = slurm_spawn_job( st , repo_key , j , reqs , nice , cmd_line , _slurm_env.get() , *se.rsrcs , se.verbose ) ;
			Trace trace(BeChnl,"Slurm::launch_job",repo_key,j,id,nice,cmd_line,se.rsrcs,STR(se.verbose)) ;
			return id ;
		}

		// data
		SpawnedMap mutable  spawned_rsrcs     ;         // number of spawned jobs queued in slurm queue
		::vector<RsrcsData> req_forces        ;         // indexed by req, resources forced by req
		uint32_t            n_max_queued_jobs = 10    ; // by default, limit to 10 the number of jobs waiting for a given set of resources
		bool                use_nice          = false ;
		::string            repo_key          ;         // a short identifier of the repository
		Daemon              daemon            ;         // info sensed from slurm daemon
	private :
		::unique_ptr<const char*[]> _slurm_env     ;
		::vector_s                  _slurm_env_vec ;
	} ;

	DequeThread<SlurmId> SlurmBackend::_s_slurm_cancel_thread ;

	//
	// init
	//

	bool _inited = (SlurmBackend::s_init(),true) ;

	//
	// Daemon
	//

	::string& operator+=( ::string& os , Daemon const& d ) {
		return os << "Daemon(" << d.time_origin <<','<< d.nice_factor <<','<< d.licenses <<')' ;
	}

	//
	// RsrcsData
	//

	::string& operator+=( ::string& os , RsrcsDataSingle const& rsds ) {
		/**/                os <<'('<< rsds.cpu       ;
		if ( rsds.mem     ) os <<','<< rsds.mem<<"MB" ;
		if ( rsds.tmp     ) os <<','<< rsds.tmp<<"MB" ;
		if (+rsds.part    ) os <<','<< rsds.part      ;
		if (+rsds.gres    ) os <<','<< rsds.gres      ;
		if (+rsds.licenses) os <<','<< rsds.licenses  ;
		if (+rsds.features) os <<','<< rsds.features  ;
		if (+rsds.qos     ) os <<','<< rsds.qos       ;
		if (+rsds.reserv  ) os <<','<< rsds.reserv    ;
		if (+rsds.excludes) os <<','<< rsds.excludes  ;
		if (+rsds.nodes   ) os <<','<< rsds.nodes     ;
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
		for( auto& [kn,v] : ::move(m) ) {
			size_t           p    = kn.find(':')                                                   ;
			::string         k    = p==Npos ? ::move(kn) :                       kn.substr(0  ,p)  ;
			uint32_t         n    = p==Npos ? 0          : from_string<uint32_t>(kn.substr(p+1  )) ;
			RsrcsDataSingle& rsds = grow(self,n)                                                   ;
			//
			auto chk_first = [&]()->void {
				throw_unless( n==0 , k," is only for 1st component of job, not component ",n ) ;
			} ;
			switch (k[0]) {
				case 'c' : if (k=="cpu"     ) {                                rsds.cpu      = from_string_with_unit<    uint32_t              >(v) ; continue ; } break ;
				case 'm' : if (k=="mem"     ) { if (d.manage_mem)              rsds.mem      = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(v) ; continue ; } break ; // no mem if not managed
				case 't' : if (k=="tmp"     ) {                                rsds.tmp      = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(v) ; continue ; } break ;
				case 'e' : if (k=="excludes") {                                rsds.excludes = ::move                                           (v) ; continue ; } break ;
				case 'f' : if (k=="features") {                                rsds.features = ::move                                           (v) ; continue ; }
				/**/       if (k=="feature" ) {                                rsds.features = ::move                                           (v) ; continue ; } break ; // for backward compatibility
				case 'g' : if (k=="gres"    ) {               _sort_entry(v) ; rsds.gres     = ::move                                           (v) ; continue ; } break ; // normalize to favor ...
				case 'l' : if (k=="licenses") { chk_first() ; _sort_entry(v) ; rsds.licenses = ::move                                           (v) ; continue ; } break ; // ... resources sharing
				case 'n' : if (k=="nodes"   ) {                                rsds.nodes    = ::move                                           (v) ; continue ; } break ;
				case 'p' : if (k=="part"    ) {                                rsds.part     = ::move                                           (v) ; continue ; } break ;
				case 'q' : if (k=="qos"     ) {                                rsds.qos      = ::move                                           (v) ; continue ; } break ;
				case 'r' : if (k=="reserv"  ) {                                rsds.reserv   = ::move                                           (v) ; continue ; } break ;
			DN}
			if ( auto it = d.licenses.find(k) ; it==d.licenses.end() ) {               { if ( +rsds.gres     && rsds.gres    .back()!=',' ) rsds.gres     += ',' ; } rsds.gres     += k+':'+v+',' ; }
			else                                                       { chk_first() ; { if ( +rsds.licenses && rsds.licenses.back()!=',' ) rsds.licenses += ',' ; } rsds.licenses += k+':'+v+',' ; }
		}
		for( RsrcsDataSingle& rsds : self )    if ( +rsds.gres     && rsds.gres    .back()==',' ) rsds.gres    .pop_back() ;
		/**/ RsrcsDataSingle& rsds = self[0] ; if ( +rsds.licenses && rsds.licenses.back()==',' ) rsds.licenses.pop_back() ; // licenses are only for first job step
		//
		if ( d.manage_mem && !self[0].mem ) throw "must reserve memory when managed by slurm daemon, consider "s+Job(ji)->rule()->full_name()+".resources={'mem':'1M'}" ;
	}
	::vmap_ss RsrcsData::mk_vmap(void) const {
		::vmap_ss res ;
		// It may be interesting to know the number of cpu reserved to know how many thread to launch in some situation
		/**/                           res.emplace_back( "cpu" , to_string_with_unit     (self[0].cpu) ) ;
		/**/                           res.emplace_back( "mem" , to_string_with_unit<'M'>(self[0].mem) ) ;
		if (self[0].tmp!=uint32_t(-1)) res.emplace_back( "tmp" , to_string_with_unit<'M'>(self[0].tmp) ) ;
		return res ;
	}

	RsrcsData blend( RsrcsData&& rsrcs , RsrcsData const& force ) {
		if (+force)
			for( size_t i=0 ; i<::min(rsrcs.size(),force.size()) ; i++ ) {
				RsrcsDataSingle const& force1 = force[i] ;
				if ( force1.cpu              ) rsrcs[i].cpu      = force1.cpu      ;
				if ( force1.mem              ) rsrcs[i].mem      = force1.mem      ;
				if ( force1.tmp!=uint32_t(-1)) rsrcs[i].tmp      = force1.tmp      ;
				if (+force1.excludes         ) rsrcs[i].excludes = force1.excludes ;
				if (+force1.features         ) rsrcs[i].features = force1.features ;
				if (+force1.gres             ) rsrcs[i].gres     = force1.gres     ;
				if (+force1.licenses         ) rsrcs[i].licenses = force1.licenses ;
				if (+force1.nodes            ) rsrcs[i].nodes    = force1.nodes    ;
				if (+force1.part             ) rsrcs[i].part     = force1.part     ;
				if (+force1.qos              ) rsrcs[i].qos      = force1.qos      ;
				if (+force1.reserv           ) rsrcs[i].reserv   = force1.reserv   ;
			}
		return ::move(rsrcs) ;
	}

	//
	// slurm API
	//

	namespace SlurmApi {
		decltype(::slurm_free_ctl_conf                    )* free_ctl_conf                     = nullptr/*garbage*/ ;
		decltype(::slurm_free_job_info_msg                )* free_job_info_msg                 = nullptr/*garbage*/ ;
		decltype(::slurm_free_submit_response_response_msg)* free_submit_response_response_msg = nullptr/*garbage*/ ;
		decltype(::slurm_init                             )* init                              = nullptr/*garbage*/ ;
		decltype(::slurm_init_job_desc_msg                )* init_job_desc_msg                 = nullptr/*garbage*/ ;
		decltype(::slurm_kill_job                         )* kill_job                          = nullptr/*garbage*/ ;
		decltype(::slurm_load_ctl_conf                    )* load_ctl_conf                     = nullptr/*garbage*/ ;
		decltype(::slurm_list_append                      )* list_append                       = nullptr/*garbage*/ ;
		decltype(::slurm_list_create                      )* list_create                       = nullptr/*garbage*/ ;
		decltype(::slurm_list_destroy                     )* list_destroy                      = nullptr/*garbage*/ ;
		decltype(::slurm_load_job                         )* load_job                          = nullptr/*garbage*/ ;
		decltype(::slurm_strerror                         )* strerror                          = nullptr/*garbage*/ ;
		decltype(::slurm_submit_batch_het_job             )* submit_batch_het_job              = nullptr/*garbage*/ ;
		decltype(::slurm_submit_batch_job                 )* submit_batch_job                  = nullptr/*garbage*/ ;
	}

	static constexpr char LibSlurm[] = SLURM_SO ;
	template<class T> void _load_func( void* handler , T*& dst , const char* name ) {
		dst = reinterpret_cast<T*>(::dlsym(handler,name)) ;
		if (!dst) throw "cannot find "s+name+" in "+LibSlurm ;
	}
	static void _exit1() { ::_exit(1) ; }
	void slurm_init(const char* config_file) {
		Trace trace(BeChnl,"slurm_init",LibSlurm,config_file?config_file:"<no config_file>") ;
		void* handler = ::dlopen(LibSlurm,RTLD_NOW|RTLD_GLOBAL) ;
		if (!handler) throw "cannot find "s+LibSlurm ;
		//
		_load_func( handler , SlurmApi::free_ctl_conf                     , "slurm_free_ctl_conf"                     ) ;
		_load_func( handler , SlurmApi::free_job_info_msg                 , "slurm_free_job_info_msg"                 ) ;
		_load_func( handler , SlurmApi::free_submit_response_response_msg , "slurm_free_submit_response_response_msg" ) ;
		_load_func( handler , SlurmApi::init                              , "slurm_init"                              ) ;
		_load_func( handler , SlurmApi::init_job_desc_msg                 , "slurm_init_job_desc_msg"                 ) ;
		_load_func( handler , SlurmApi::kill_job                          , "slurm_kill_job"                          ) ;
		_load_func( handler , SlurmApi::load_ctl_conf                     , "slurm_load_ctl_conf"                     ) ;
		_load_func( handler , SlurmApi::list_append                       , "slurm_list_append"                       ) ;
		_load_func( handler , SlurmApi::list_create                       , "slurm_list_create"                       ) ;
		_load_func( handler , SlurmApi::list_destroy                      , "slurm_list_destroy"                      ) ;
		_load_func( handler , SlurmApi::load_job                          , "slurm_load_job"                          ) ;
		_load_func( handler , SlurmApi::strerror                          , "slurm_strerror"                          ) ;
		_load_func( handler , SlurmApi::submit_batch_het_job              , "slurm_submit_batch_het_job"              ) ;
		_load_func( handler , SlurmApi::submit_batch_job                  , "slurm_submit_batch_job"                  ) ;
		//
		// /!\ stupid SlurmApi::init function calls exit(1) in case of error !
		// so the idea here is to fork a process to probe SlurmApi::init
		if ( pid_t child_pid=::fork() ; !child_pid ) {
			// in child
			::atexit(_exit1) ;                                                // we are unable to call the exit handlers from here, so we add an additional one which exits immediately
			Fd dev_null_fd { "/dev/null" , Fd::Write } ;                      // this is just a probe, we want nothing on stderr
			::dup2(dev_null_fd,2) ;                                           // so redirect to /dev/null
			SlurmApi::init(config_file) ;                                     // in case of error, SlurmApi::init calls exit(1), which in turn calls _exit1 as the first handler (last registered)
			::_exit(0) ;                                                      // if we are here, everything went smoothly
		} else {
			// in parent
			int wstatus ;
			pid_t rc = ::waitpid(child_pid,&wstatus,0) ;                      // gather status to know if we were able to call SlurmApi::init
			if ( rc<=0 || !wstatus_ok(wstatus) ) throw "cannot init slurm"s ; // no, report error
		}
		SlurmApi::init(config_file) ;                                         // this is now safe as we have already probed it
		//
		trace("done") ;
	}

	static ::string slurm_err() {
		return SlurmApi::strerror(errno) ;
	}

	static cxxopts::Options _create_parser() {
		cxxopts::Options res { "slurm" , "Slurm options parser for lmake" } ;
		res.add_options()
			( "c,cpus-per-task" , "cpus-per-task" , cxxopts::value<uint16_t>() )
			( "mem"             , "mem"           , cxxopts::value<::string>() )
			( "tmp"             , "tmp"           , cxxopts::value<::string>() )
			( "C,constraint"    , "constraint"    , cxxopts::value<::string>() )
			( "x,exclude"       , "exclude nodes" , cxxopts::value<::string>() )
			( "gres"            , "gres"          , cxxopts::value<::string>() )
			( "L,licenses"      , "licenses"      , cxxopts::value<::string>() )
			( "w,nodelist"      , "nodes"         , cxxopts::value<::string>() )
			( "p,partition"     , "partition"     , cxxopts::value<::string>() )
			( "q,qos"           , "qos"           , cxxopts::value<::string>() )
			( "reservation"     , "reservation"   , cxxopts::value<::string>() )
			( "h,help"          , "print usage"                                )
		;
		return res ;
	}
	RsrcsData parse_args(::string const& args) {
		static ::string         s_slurm     = "slurm"          ;                   // apparently "slurm"s.data() does not work as memory is freed right away
		static cxxopts::Options s_opt_parse = _create_parser() ;
		//
		Trace trace(BeChnl,"parse_args",args) ;
		//
		if (!args) return {} ;                                                     // fast path
		//
		::vector_s      arg_vec = split(args,' ') ; arg_vec.push_back(":")       ; // sentinel to parse last args
		::vector<char*> argv    ;                   argv.reserve(arg_vec.size()) ;
		RsrcsData       res     ;
		//
		argv.push_back(s_slurm.data()) ;
		for ( ::string& arg : arg_vec ) {
			if (arg!=":") {
				argv.push_back(arg.data()) ;
				continue ;
			}
			RsrcsDataSingle res1 ;
			try {
				auto opts = s_opt_parse.parse(argv.size(),argv.data()) ;
				//
				if (opts.count("cpus-per-task")) res1.cpu      =                                                   opts["cpus-per-task"].as<uint16_t>()  ;
				if (opts.count("mem"          )) res1.mem      = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(opts["mem"          ].as<::string>()) ;
				if (opts.count("tmp"          )) res1.tmp      = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(opts["tmp"          ].as<::string>()) ;
				if (opts.count("constraint"   )) res1.features =                                                   opts["constraint"   ].as<::string>()  ;
				if (opts.count("exclude"      )) res1.excludes =                                                   opts["exclude"      ].as<::string>()  ;
				if (opts.count("gres"         )) res1.gres     =                                                   opts["gres"         ].as<::string>()  ;
				if (opts.count("licenses"     )) res1.licenses =                                                   opts["licenses"     ].as<::string>()  ;
				if (opts.count("nodelist"     )) res1.nodes    =                                                   opts["nodelist"     ].as<::string>()  ;
				if (opts.count("partition"    )) res1.part     =                                                   opts["partition"    ].as<::string>()  ;
				if (opts.count("qos"          )) res1.qos      =                                                   opts["qos"          ].as<::string>()  ;
				if (opts.count("reservation"  )) res1.reserv   =                                                   opts["reservation"  ].as<::string>()  ;
				if (opts.count("help"         )) throw s_opt_parse.help() ;
			} catch (cxxopts::exceptions::exception const& e) {
				throw "Error while parsing slurm options: "s+e.what() ;
			}
			res.push_back(::move(res1)) ;
			argv.resize(1)              ;
		}
		return res ;
	}

	void slurm_cancel(SlurmId slurm_id) {
		//This for loop with a retry comes from the scancel Slurm utility code
		//Normally we kill mainly waiting jobs, but some "just started jobs" could be killed like that also
		//Running jobs are killed by lmake/job_exec
		Trace trace(BeChnl,"slurm_cancel",slurm_id) ;
		int i = 0/*garbage*/ ;
		Lock lock { _slurm_mutex } ;
		for( i=0 ; i<SlurmCancelTrials ; i++ ) {
			if (SlurmApi::kill_job(slurm_id,SIGKILL,KILL_FULL_JOB)==SLURM_SUCCESS) { trace("done") ; return ; }
			switch (errno) {
				case ESLURM_INVALID_JOB_ID             :
				case ESLURM_ALREADY_DONE               : trace("already_dead",errno) ;                return ;
				case ESLURM_TRANSITION_STATE_NO_UPDATE : trace("retry",i)            ; ::sleep(1+i) ; break  ;
				default : goto Bad ;
			}
		}
	Bad :
		FAIL("cannot cancel job ",slurm_id," after ",i," retries : ",slurm_err()) ;
	}

	::pair_s<Bool3/*job_ok*/> slurm_job_state(SlurmId slurm_id) {                                                     // Maybe means job has not completed
		Trace trace(BeChnl,"slurm_job_state",slurm_id) ;
		SWEAR(slurm_id) ;
		job_info_msg_t* resp = nullptr/*garbage*/ ;
		{	Lock lock { _slurm_mutex } ;
			if (SlurmApi::load_job(&resp,slurm_id,SHOW_LOCAL)!=SLURM_SUCCESS) switch (errno) {
				case EAGAIN                              :
				case ESLURM_ERROR_ON_DESC_TO_RECORD_COPY : //!                                             job_ok
				case ESLURM_NODES_BUSY                   : return { "slurm daemon busy : "   +slurm_err() , Maybe } ; // no info : heartbeat will retry, end will eventually cancel
				default                                  : return { "cannot load job info : "+slurm_err() , Yes   } ;
			}
		}
		::string                msg ;
		Bool3                   ok  = Yes     ;
		slurm_job_info_t const* ji  = nullptr ;
		for ( uint32_t i=0 ; i<resp->record_count ; i++ ) {
			ji = &resp->job_array[i] ;
			job_states js = job_states( ji->job_state & JOB_STATE_BASE ) ;
			switch (js) {
				// if slurm sees job failure, somthing weird occurred (if actual job fails, job_exec reports an error and completes successfully)
				// possible job_states values (from slurm.h) :
				case JOB_PENDING   :                              ok = Maybe ; continue  ;                            // queued waiting for initiation
				case JOB_RUNNING   :                              ok = Maybe ; continue  ;                            // allocated resources and executing
				case JOB_SUSPENDED :                              ok = Maybe ; continue  ;                            // allocated resources, execution suspended
				case JOB_COMPLETE  :                                           continue  ;                            // completed execution successfully
				case JOB_CANCELLED : msg = "cancelled by user"s ; ok = Yes   ; goto Done ;                            // cancelled by user
				case JOB_TIMEOUT   : msg = "timeout"s           ; ok = No    ; goto Done ;                            // terminated on reaching time limit
				case JOB_NODE_FAIL : msg = "node failure"s      ; ok = Yes   ; goto Done ;                            // terminated on node failure
				case JOB_PREEMPTED : msg = "preempted"s         ; ok = Yes   ; goto Done ;                            // terminated due to preemption
				case JOB_BOOT_FAIL : msg = "boot failure"s      ; ok = Yes   ; goto Done ;                            // terminated due to node boot failure
				case JOB_DEADLINE  : msg = "deadline reached"s  ; ok = Yes   ; goto Done ;                            // terminated on deadline
				case JOB_OOM       : msg = "out of memory"s     ; ok = No    ; goto Done ;                            // experienced out of memory error
				//   JOB_END                                                                                          // not a real state, last entry in table
				case JOB_FAILED :                                                                                     // completed execution unsuccessfully
					// when job_exec receives a signal, the bash process which launches it (which the process seen by slurm) exits with an exit code > 128
					// however, the user is interested in the received signal, not mapped bash exit code, so undo mapping
					// signaled wstatus are barely the signal number
					/**/                                      msg = "failed ("                                                                                           ;
					if      (WIFSIGNALED(ji->exit_code)     ) msg << "signal " << WTERMSIG(ji->exit_code)           <<'-'<< ::strsignal(WTERMSIG(ji->exit_code)        ) ;
					else if (!WIFEXITED(ji->exit_code)      ) msg << "??"                                                                                                ; // weird, could be a FAIL
					else if (WEXITSTATUS(ji->exit_code)>0x80) msg << "signal " << (WEXITSTATUS(ji->exit_code)-0x80) <<'-'<< ::strsignal(WEXITSTATUS(ji->exit_code)-0x80) ; // cf comment above
					else if (WEXITSTATUS(ji->exit_code)!=0  ) msg << "exit "   << WEXITSTATUS(ji->exit_code)                                                             ;
					else                                      msg << "ok"                                                                                                ;
					/**/                                      msg << ')'                                                                                                 ;
					ok = No ;
					goto Done ;
				default : FAIL("Slurm : wrong job state return for job (",slurm_id,") : ") ;
			}
		}
	Done :
		if ( +msg && ji->nodes ) msg << (::strchr(ji->nodes,' ')==nullptr?" on node : ":" on nodes : ") << ji->nodes ;
		SlurmApi::free_job_info_msg(resp) ;
		return { msg , ok } ;
	}

	static ::string _get_log_dir_s  (Job job) { return job.ancillary_file(AncillaryTag::Backend)+'/' ; }
	static ::string _get_stderr_file(Job job) { return _get_log_dir_s(job) + "stderr"                ; }
	static ::string _get_stdout_file(Job job) { return _get_log_dir_s(job) + "stdout"                ; }

	::string read_stderr(Job job) {
		Trace trace(BeChnl,"Slurm::read_stderr",job) ;
		::string stderr_file = _get_stderr_file(job) ;
		try {
			::string res = AcFd(stderr_file).read() ;
			if (!res) return {}                                    ;
			else      return "stderr from : "+stderr_file+'\n'+res ;
		} catch (::string const&) {
			return "stderr not found : "+stderr_file ;
		}
	}

	static ::string _cmd_to_string(::vector_s const& cmd_line) {
		::string res   = "#!/bin/sh" ;
		First    first ;
		for ( ::string const& s : cmd_line ) res <<first("\n"," ")<< s ;
		res += '\n' ;
		return res ;
	}
	SlurmId slurm_spawn_job(
		::stop_token            st
	,	::string         const& key
	,	Job                     job
	,	::vector<ReqIdx> const& reqs
	,	int32_t                 nice
	,	::vector_s       const& cmd_line
	,	const char**            env
	,	RsrcsData        const& rsrcs
	,	bool                    verbose
	) {
		static ::string wd = no_slash(*g_repo_root_s) ;
		Trace trace(BeChnl,"slurm_spawn_job",key,job,nice,cmd_line,rsrcs,STR(verbose)) ;
		//
		SWEAR(rsrcs.size()> 0) ;
		SWEAR(nice        >=0) ;
		// first element is treated specially to avoid allocation in the very frequent case of a single element
		::string                 job_name    = key + job->name()        ;
		::string                 script      = _cmd_to_string(cmd_line) ;
		::string                 stderr_file ;                                                                           //                 keep alive until slurm is called
		::string                 stdout_file ;                                                                           //                 .
		job_desc_msg_t           job_desc0   ;                                                                           // first element   .
		::string                 gres0       ;                                                                           // .             , .
		::vector<job_desc_msg_t> job_descs   ; job_descs.reserve(rsrcs.size()-1) ;                                       // other elements  .
		::vector_s               gress       ; gress    .reserve(rsrcs.size()-1) ;                                       // .             , .
		if(verbose) {
			stderr_file = _get_stderr_file(job) ;
			stdout_file = _get_stdout_file(job) ;
			mk_dir_s(_get_log_dir_s(job)) ;
		}
		for( uint32_t i=0 ; RsrcsDataSingle const& r : rsrcs ) {
			//                            first element            other elements
			job_desc_msg_t& j    = i==0 ? job_desc0              : job_descs.emplace_back()               ;              // keep alive
			::string      & gres = i==0 ? (gres0="gres:"+r.gres) : gress    .emplace_back("gres:"+r.gres) ;              // .
			//
			SlurmApi::init_job_desc_msg(&j) ;
			/**/                     j.cpus_per_task   = r.cpu                                                         ;
			/**/                     j.environment     = const_cast<char**>(env)                                       ;
			/**/                     j.env_size        = 1                                                             ;
			/**/                     j.name            = const_cast<char*>(job_name.c_str())                           ;
			/**/                     j.pn_min_memory   = r.mem                                                         ; //in MB
			if (r.tmp!=uint32_t(-1)) j.pn_min_tmp_disk = r.tmp                                                         ; //in MB
			/**/                     j.std_err         = verbose ? stderr_file.data() : const_cast<char*>("/dev/null") ; // keep alive
			/**/                     j.std_out         = verbose ? stdout_file.data() : const_cast<char*>("/dev/null") ; // keep alive
			/**/                     j.work_dir        = wd.data()                                                     ;
			//
			if(+r.excludes) j.exc_nodes     = const_cast<char*>(r.excludes.data()) ;
			if(+r.features) j.features      = const_cast<char*>(r.features.data()) ;
			if(+r.licenses) j.licenses      = const_cast<char*>(r.licenses.data()) ;
			if(+r.nodes   ) j.req_nodes     = const_cast<char*>(r.nodes   .data()) ;
			if(+r.part    ) j.partition     = const_cast<char*>(r.part    .data()) ;
			if(+r.qos     ) j.qos           = const_cast<char*>(r.qos     .data()) ;
			if(+r.reserv  ) j.reservation   = const_cast<char*>(r.reserv  .data()) ;
			if(+r.gres    ) j.tres_per_node =                   gres      .data()  ;
			if(i==0       ) j.script        =                   script    .data()  ;
			/**/            j.nice          = NICE_OFFSET+nice                     ;
			i++ ;
		}
		for( int i=0 ; i<SlurmSpawnTrials ; i++ ) {
			submit_response_msg_t* msg = nullptr/*garbage*/ ;
			bool                   err = false  /*garbage*/ ;
			errno = 0 ;                                                                         // normally useless
			{	Lock lock { _slurm_mutex } ;
				if (!job_descs) {                                                               // single element case
					err = SlurmApi::submit_batch_job(&job_desc0,&msg)!=SLURM_SUCCESS ;
				} else {                                                                        // multi-elements case
					List l = SlurmApi::list_create(nullptr/*dealloc_func*/) ;
					/**/                                  SlurmApi::list_append(l,&job_desc0) ; // first element
					for ( job_desc_msg_t& c : job_descs ) SlurmApi::list_append(l,&c        ) ; // other elements
					err = SlurmApi::submit_batch_het_job(l,&msg)!=SLURM_SUCCESS ;
					SlurmApi::list_destroy(l) ;
				}
			}
			int sav_errno = errno ;                                                             // save value before calling any slurm or libc function
			if (msg) {
				SlurmId res = msg->job_id ;
				SWEAR(res!=0) ;                                                                 // null id is used to signal absence of id
				SlurmApi::free_submit_response_response_msg(msg) ;
				if (!sav_errno) { SWEAR(!err) ; return res ; }
			}
			SWEAR(sav_errno!=0) ;                                                               // if err, we should have a errno, else if no errno, we should have had a msg containing an id
			::string err_msg ;
			switch (sav_errno) {
				case EAGAIN                              :
				case ESLURM_ERROR_ON_DESC_TO_RECORD_COPY :
				case ESLURM_NODES_BUSY                   : {
					trace("retry",sav_errno,SlurmApi::strerror(sav_errno)) ;
					bool zombie = true ;
					for ( Req r : reqs ) if (!r.zombie()) { zombie = false ; continue ; }
					if ( zombie || !Delay(1).sleep_for(st) ) {
						trace("interrupted",i,STR(zombie)) ;
						throw "interrupted while connecting to slurm daemon"s ;
					}
				} continue ;
			#if SLURM_VERSION_NUMBER>=0x170200
				case ESLURM_LICENSES_UNAVAILABLE :
			#endif
				case ESLURM_INVALID_LICENSES :
					err_msg = job_desc0.licenses ;                                              // licenses are only on first step
				break ;
				case ESLURM_INVALID_GRES   :
				case ESLURM_DUPLICATE_GRES :
			#if SLURM_VERSION_NUMBER>=0x130500
				case ESLURM_INVALID_GRES_TYPE :
				case ESLURM_UNSUPPORTED_GRES  :
			#endif
			#if SLURM_VERSION_NUMBER>=0x160500
				case ESLURM_INSUFFICIENT_GRES :
			#endif
					/**/                                                  err_msg <<(rsrcs.size()>1?"[ ":"") ;
					for( First first ; RsrcsDataSingle const& r : rsrcs ) err_msg <<first(""," , ")<< r.gres ;
					/**/                                                  err_msg <<(rsrcs.size()>1?" ]":"") ;
				break ;
			DN}
			trace("spawn_error" ,sav_errno) ;
			throw "slurm spawn job error : "s+SlurmApi::strerror(sav_errno)+(+err_msg?" (":"")+err_msg+(+err_msg?")":"") ;
		}
		trace("cannot_spawn") ;
		throw "cannot connect to slurm daemon"s ;
	}

	Daemon slurm_sense_daemon() {
		Trace trace(BeChnl,"slurm_sense_daemon") ;
		slurm_conf_t* conf = nullptr ;
		// XXX? : remember last conf read so as to pass a real update_time param & optimize call (maybe not worthwhile)
		{	Lock lock { _slurm_mutex } ;
			if (!is_target("/etc/slurm/slurm.conf")                           ) throw "no slurm config file /etc/slur/slurm.conf"s ;
			if (SlurmApi::load_ctl_conf(0/*update_time*/,&conf)!=SLURM_SUCCESS) throw "cannot reach slurm daemon : "+slurm_err()   ;
		}
		SWEAR(conf) ;
		Daemon res ;
		trace("conf",STR(conf),conf->select_type_param) ;
		res.manage_mem = conf->select_type_param&CR_MEMORY ;
		if (conf->priority_params) {
			static ::string const to_mrkr  = "time_origin=" ;
			static ::string const npd_mrkr = "nice_factor=" ;
			trace("priority_params",conf->priority_params) ;
			//
			::string spp = conf->priority_params ;
			if ( size_t pos=spp.find(to_mrkr) ; pos!=Npos ) {
				pos += to_mrkr.size() ;
				res.time_origin = Pdate(spp.substr(pos,spp.find(',',pos))) ;
			}
			//
			if ( size_t pos=spp.find(npd_mrkr) ; pos!=Npos ) {
				pos += npd_mrkr.size() ;
				res.nice_factor = from_string<float>(spp.substr(pos,spp.find(',',pos))) ;
			}
		}
		if (conf->licenses) {
			trace("licenses",conf->licenses) ;
			::vector_s rsrc_vec = split(conf->licenses,',') ;
			for( ::string const& r : rsrc_vec ) {
				size_t   p = r.find(':')                                      ;
				::string k = r.substr(0,p)                                    ;
				size_t   v = p==Npos ? 1 : from_string<size_t>(r.substr(p+1)) ;
				//
				res.licenses.emplace(k,v) ;
			}
		}
		SlurmApi::free_ctl_conf(conf) ;
		return res ;
	}

}
