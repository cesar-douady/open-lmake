// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "slurm_api.hh" // /!\ must be first because Python.h must be first

#include <dlfcn.h>

#include <filesystem>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized" // seems to be necessary for compiling with -fsanitize
#include "ext/cxxopts.hpp"
#pragma GCC diagnostic pop

using namespace Disk ;

namespace Backends::Slurm {

	namespace SlurmApi {
		void*                               g_lib_handler      ;
		::umap_s<Daemon(*)(void* /*conf*/)> g_sense_daemon_tab ;
		//
		SlurmId (*spawn_job_func)(
			::stop_token            st
		,	::string         const& key
		,	Job                     job
		,	::vector<ReqIdx> const& reqs
		,	int32_t                 nice
		,	::vector_s       const& cmd_line
		,	const char**            env
		,	RsrcsData        const& rsrcs
		,	bool                    verbose
		) = nullptr ;
		::pair_s<Bool3/*job_ok*/> (*job_state_func)(SlurmId) = nullptr ;
		void                      (*cancel_func   )(SlurmId) = nullptr ;
	}

	//
	// resources
	//

	RsrcsData blend( RsrcsData&& rsrcs , RsrcsData const& force ) ;

}

namespace std {
	template<> struct hash<Backends::Slurm::RsrcsData> {
		size_t operator()(Backends::Slurm::RsrcsData const& rs) const {
			return +Hash::Xxh(static_cast<::vector<Backends::Slurm::RsrcsDataSingle> const&>(rs)).digest() ;
		}
	} ;
}

//
// SlurmBackend
//

namespace Backends::Slurm {

	Mutex<MutexLvl::Slurm> slurm_mutex ; // ensure no more than a single outstanding request to daemon

	RsrcsData parse_args        (::string const& args                                     ) ;
	::string  read_stderr       (Job                                                      ) ;
	Daemon    slurm_sense_daemon( ::string const& lib_slurm , ::string const& config_file ) ;
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
		static QueueThread<SlurmId> _s_slurm_cancel_thread ; // when a req is killed, a lot of queued jobs may be canceled, better to do it in a separate thread

		// accesses

		virtual bool call_launch_after_start() const { return true ; }

		// services

		virtual void sub_config( ::vmap_ss const& dct , ::vmap_ss const& env_ , bool dyn ) {
			Trace trace(BeChnl,"Slurm::config",STR(dyn),dct) ;
			//
			repo_key = base_name(no_slash(*g_repo_root_s))+':' ; // cannot put this code directly as init value as g_repo_root_s is not available early enough
			for( auto const& [k,v] : dct ) {
				try {
					switch (k[0]) {
						case 'c' : if (k=="config"           ) { config_file       =                       v  ; continue ; } break ;
						case 'l' : if (k=="lib_slurm"        ) { lib_slurm         =                       v  ; continue ; } break ;
						case 'n' : if (k=="n_max_queued_jobs") { n_max_queued_jobs = from_string<uint32_t>(v) ; continue ; } break ;
						case 'r' : if (k=="repo_key"         ) { repo_key          =                       v  ; continue ; } break ;
						case 'u' : if (k=="use_nice"         ) { use_nice          = from_string<bool    >(v) ; continue ; } break ;
					DN}
				} catch (::string const& e) { trace("bad_val",k,v) ; throw "wrong value for entry "   +k+": "+v ; }
				/**/                        { trace("bad_key",k  ) ; throw "unexpected config entry: "+k        ; }
			}
			if (!dyn) {
				daemon = slurm_sense_daemon( lib_slurm , config_file ) ;
				_s_slurm_cancel_thread.open('K',SlurmApi::cancel_func) ;
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
				if (+rds.features ) single = true ;
				if (+rds.partition) single = true ;
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
			if ( !se.verbose && s==Status::Ok ) return {{}/*msg*/,true/*retry*/} ;                   // common case, must be fast, if job is in error, better to ask slurm why, e.g. could be OOM
			::pair_s<Bool3/*job_ok*/> info ;
			for( int c : iota(2) ) {
				Delay d { 0.01 }                                               ;
				Pdate e = Pdate(New) + ::max(g_config->network_delay,Delay(1)) ; // ensure a reasonable minimum
				for( Pdate pd = New ;; pd+=d ) {
					info = SlurmApi::job_state_func(se.id) ;
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
			::pair_s<Bool3/*job_ok*/> info = SlurmApi::job_state_func(se.id) ;
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
			SlurmId id = SlurmApi::spawn_job_func( st , repo_key , j , reqs , nice , cmd_line , _slurm_env.get() , *se.rsrcs , se.verbose ) ;
			Trace trace(BeChnl,"Slurm::launch_job",repo_key,j,id,nice,cmd_line,se.rsrcs,STR(se.verbose)) ;
			return id ;
		}

		// data
		SpawnedMap mutable  spawned_rsrcs     ;                 // number of spawned jobs queued in slurm queue
		::vector<RsrcsData> req_forces        ;                 // indexed by req, resources forced by req
		::string            config_file       ;
		::string            lib_slurm         = "libslurm.so" ;
		uint32_t            n_max_queued_jobs = 10            ; // by default, limit to 10 the number of jobs waiting for a given set of resources
		bool                use_nice          = false         ;
		::string            repo_key          ;                 // a short identifier of the repository
		Daemon              daemon            ;                 // info sensed from slurm daemon
	private :
		::unique_ptr<const char*[]> _slurm_env     ;
		::vector_s                  _slurm_env_vec ;
	} ;

	QueueThread<SlurmId> SlurmBackend::_s_slurm_cancel_thread ;

	//
	// init
	//

	bool _inited = (SlurmBackend::s_init(),true) ;

	//
	// Daemon
	//

	::string& operator+=( ::string& os , Daemon const& d ) {                                                                           // START_OF_NO_COV
		return os << "Daemon(" << d.time_origin <<','<< d.nice_factor <<','<< d.licenses <<','<< (d.manage_mem?"mem":"no_mem") <<')' ;
	}                                                                                                                                  // END_OF_NO_COV

	//
	// RsrcsData
	//

	::string& operator+=( ::string& os , RsrcsDataSingle const& rsds ) { // START_OF_NO_COV
		/**/                 os <<'('<< rsds.cpu       ;
		if ( rsds.mem      ) os <<','<< rsds.mem<<"MB" ;
		if ( rsds.tmp      ) os <<','<< rsds.tmp<<"MB" ;
		if (+rsds.partition) os <<','<< rsds.partition ;
		if (+rsds.gres     ) os <<','<< rsds.gres      ;
		if (+rsds.licenses ) os <<','<< rsds.licenses  ;
		if (+rsds.features ) os <<','<< rsds.features  ;
		if (+rsds.qos      ) os <<','<< rsds.qos       ;
		if (+rsds.reserv   ) os <<','<< rsds.reserv    ;
		if (+rsds.excludes ) os <<','<< rsds.excludes  ;
		if (+rsds.nodes    ) os <<','<< rsds.nodes     ;
		return              os <<')'                  ;
	}                                                                    // END_OF_NO_COV

	static void _sort(::string& s) {
		if (s.find(',')==Npos) return ;
		::vector_s v = split(s,',') ;
		SWEAR(v.size()>1) ;
		sort(v) ;
		s = v[0] ;
		for( size_t i=1 ; i<v.size() ; i++ ) s<<','<<v[i] ;
	}
	inline RsrcsData::RsrcsData( ::vmap_ss&& m , Daemon d , JobIdx ji ) : Base{1} { // ensure we have at least 1 entry as we sometimes access element 0
		sort(m) ;
		for( auto& [kn,v] : m ) {
			size_t           p    = kn.find(':')                                                   ;
			::string         k    = p==Npos ? ::move(kn) :                       kn.substr(0  ,p)  ;
			uint32_t         n    = p==Npos ? 0          : from_string<uint32_t>(kn.substr(p+1  )) ;
			RsrcsDataSingle& rsds = grow(self,n)                                                   ;
			//
			auto chk_first = [&]()->void {
				throw_unless( n==0 , k," is only for 1st component of job, not component ",n ) ;
			} ;
			switch (k[0]) { //!                                                                                                    RndUp
				case 'c' : if (k=="cpu"      ) {                          rsds.cpu       = from_string_with_unit<    uint32_t     >(v) ; continue ; } break ;
				case 'm' : if (k=="mem"      ) {                          rsds.mem       = from_string_with_unit<'M',uint32_t,true>(v) ; continue ; } break ; // no mem if not managed
				case 't' : if (k=="tmp"      ) {                          rsds.tmp       = from_string_with_unit<'M',uint32_t,true>(v) ; continue ; } break ;
				case 'e' : if (k=="excludes" ) {                          rsds.excludes  = ::move                                  (v) ; continue ; } break ;
				case 'f' : if (k=="features" ) {                          rsds.features  = ::move                                  (v) ; continue ; } break ;
				case 'g' : if (k=="gres"     ) {               _sort(v) ; rsds.gres      = ::move                                  (v) ; continue ; } break ; // normalize to favor resources sharing
				case 'l' : if (k=="licenses" ) { chk_first() ; _sort(v) ; rsds.licenses  = ::move                                  (v) ; continue ; } break ; // .
				case 'n' : if (k=="nodes"    ) {                          rsds.nodes     = ::move                                  (v) ; continue ; } break ;
				case 'p' : if (k=="partition") {                          rsds.partition = ::move                                  (v) ; continue ; } break ;
				case 'q' : if (k=="qos"      ) {                          rsds.qos       = ::move                                  (v) ; continue ; } break ;
				case 'r' : if (k=="reserv"   ) {                          rsds.reserv    = ::move                                  (v) ; continue ; } break ;
			DN}
			if ( auto it = d.licenses.find(k) ; it==d.licenses.end() ) {               { if ( +rsds.gres     && rsds.gres    .back()!=',' ) rsds.gres     += ',' ; } rsds.gres     += k+':'+v+',' ; }
			else                                                       { chk_first() ; { if ( +rsds.licenses && rsds.licenses.back()!=',' ) rsds.licenses += ',' ; } rsds.licenses += k+':'+v+',' ; }
		}
		for( RsrcsDataSingle& rsds : self )    if ( +rsds.gres     && rsds.gres    .back()==',' ) rsds.gres    .pop_back() ;
		/**/ RsrcsDataSingle& rsds = self[0] ; if ( +rsds.licenses && rsds.licenses.back()==',' ) rsds.licenses.pop_back() ;                                  // licenses are only for first job step
		//
		for( RsrcsDataSingle const& rds : self ) {
			if (                 !rds.cpu ) throw "must reserve cpu, consider : "s                                +Job(ji)->rule()->user_name()+".resources={'cpu':'1'}"  ;
			if ( d.manage_mem && !rds.mem ) throw "must reserve memory when managed by slurm daemon, consider : "s+Job(ji)->rule()->user_name()+".resources={'mem':'1M'}" ;
		}
	}
	::vmap_ss RsrcsData::mk_vmap() const {
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
				if ( force1.cpu              ) rsrcs[i].cpu       = force1.cpu       ;
				if ( force1.mem              ) rsrcs[i].mem       = force1.mem       ;
				if ( force1.tmp!=uint32_t(-1)) rsrcs[i].tmp       = force1.tmp       ;
				if (+force1.excludes         ) rsrcs[i].excludes  = force1.excludes  ;
				if (+force1.features         ) rsrcs[i].features  = force1.features  ;
				if (+force1.gres             ) rsrcs[i].gres      = force1.gres      ;
				if (+force1.licenses         ) rsrcs[i].licenses  = force1.licenses  ;
				if (+force1.nodes            ) rsrcs[i].nodes     = force1.nodes     ;
				if (+force1.partition        ) rsrcs[i].partition = force1.partition ;
				if (+force1.qos              ) rsrcs[i].qos       = force1.qos       ;
				if (+force1.reserv           ) rsrcs[i].reserv    = force1.reserv    ;
			}
		return ::move(rsrcs) ;
	}

	//
	// slurm API
	//

	static void _exit1() {
		::_exit(1) ;
	}
	Daemon slurm_sense_daemon( ::string const& lib_slurm , ::string const& config_file ) {
		Trace trace(BeChnl,"slurm_sense_daemon",lib_slurm,config_file) ;
		if (!SlurmApi::g_sense_daemon_tab) throw ""s ;                        // if nothing to try, no backend but no error
		//
		SlurmApi::g_lib_handler = ::dlopen(lib_slurm.c_str(),RTLD_NOW|RTLD_GLOBAL) ; throw_unless( SlurmApi::g_lib_handler , "cannot find ",lib_slurm ) ;
		//
		const char*               cf                 = +config_file ? config_file.c_str() : nullptr                                                        ;
		SlurmApi::InitFunc        init_func          = reinterpret_cast<SlurmApi::InitFunc       >(::dlsym(SlurmApi::g_lib_handler,"slurm_init"         )) ;
		SlurmApi::LoadCtlConfFunc load_ctl_conf_func = reinterpret_cast<SlurmApi::LoadCtlConfFunc>(::dlsym(SlurmApi::g_lib_handler,"slurm_load_ctl_conf")) ;
		SlurmApi::FreeCtlConfFunc free_ctl_conf_func = reinterpret_cast<SlurmApi::FreeCtlConfFunc>(::dlsym(SlurmApi::g_lib_handler,"slurm_free_ctl_conf")) ;
		throw_unless( init_func          , "cannot find slurm_init"          ) ;
		throw_unless( load_ctl_conf_func , "cannot find slurm_load_ctl_conf" ) ;
		throw_unless( free_ctl_conf_func , "cannot find slurm_free_ctl_conf" ) ;
		// /!\ stupid SlurmApi::init function calls exit(1) in case of error !
		// so the idea here is to fork a process to probe SlurmApi::init
		if ( pid_t child_pid=::fork() ; !child_pid ) {
			// in child
			::atexit(_exit1) ;                                                // we are unable to call the exit handlers from here, so we add an additional one which exits immediately
			Fd dev_null_fd { "/dev/null" , Fd::Write } ;                      // this is just a probe, we want nothing on stderr
			::dup2(dev_null_fd,2) ;                                           // so redirect to /dev/null
			init_func(cf) ;                                                   // in case of error, SlurmApi::init calls exit(1), which in turn calls _exit1 as the first handler (last registered)
			::_exit(0) ;                                                      // if we are here, everything went smoothly
		} else {
			// in parent
			int wstatus ;
			pid_t rc = ::waitpid(child_pid,&wstatus,0) ;                      // gather status to know if we were able to call SlurmApi::init
			if ( rc<=0 || !wstatus_ok(wstatus) ) throw "cannot init slurm"s ; // no, report error
		}
		init_func(cf) ;                                                       // this should be safe now that we have checked it works in a child
		//
		void* conf = nullptr ;
		// XXX? : remember last conf read so as to pass a real update_time param & optimize call (maybe not worthwhile)
		{	Lock     lock { slurm_mutex }                        ;
			::string cf   = config_file|"/etc/slurm/slurm.conf"s ;
			if (!is_target(cf)                               ) throw "no slurm config file "+cf  ;
			if (load_ctl_conf_func(0/*update_time*/,&conf)!=0) throw "cannot reach slurm daemon" ;
		}
		SWEAR(conf) ;
		//
		trace("search_version") ;
		Daemon daemon ;
		bool   found  = false ;
		for( auto const& [version,sense_daemon_func] : SlurmApi::g_sense_daemon_tab ) {
			trace("try_version",version) ;
			try {
				daemon = sense_daemon_func(conf) ;
				found  = true                    ;
				break ;
			} catch (::string const&) {}                                      // ignore errors as well
		}
		//
		free_ctl_conf_func(conf) ;
		throw_unless( found , "unsupported slurm version" ) ;
		return daemon ;
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
				if (opts.count("cpus-per-task")) res1.cpu       =                                                   opts["cpus-per-task"].as<uint16_t>()  ;
				if (opts.count("mem"          )) res1.mem       = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(opts["mem"          ].as<::string>()) ;
				if (opts.count("tmp"          )) res1.tmp       = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(opts["tmp"          ].as<::string>()) ;
				if (opts.count("constraint"   )) res1.features  =                                                   opts["constraint"   ].as<::string>()  ;
				if (opts.count("exclude"      )) res1.excludes  =                                                   opts["exclude"      ].as<::string>()  ;
				if (opts.count("gres"         )) res1.gres      =                                                   opts["gres"         ].as<::string>()  ;
				if (opts.count("licenses"     )) res1.licenses  =                                                   opts["licenses"     ].as<::string>()  ;
				if (opts.count("nodelist"     )) res1.nodes     =                                                   opts["nodelist"     ].as<::string>()  ;
				if (opts.count("partition"    )) res1.partition =                                                   opts["partition"    ].as<::string>()  ;
				if (opts.count("qos"          )) res1.qos       =                                                   opts["qos"          ].as<::string>()  ;
				if (opts.count("reservation"  )) res1.reserv    =                                                   opts["reservation"  ].as<::string>()  ;
				if (opts.count("help"         )) throw s_opt_parse.help() ;
			} catch (cxxopts::exceptions::exception const& e) {
				throw "Error while parsing slurm options: "s+e.what() ;
			}
			res.push_back(::move(res1)) ;
			argv.resize(1)              ;
		}
		return res ;
	}

	::string read_stderr(Job job) {
		Trace trace(BeChnl,"Slurm::read_stderr",job) ;
		::string stderr_file = get_stderr_file(job) ;
		try {
			::string res = AcFd(stderr_file).read() ;
			if (!res) return {}                                    ;
			else      return "stderr from : "+stderr_file+'\n'+res ;
		} catch (::string const&) {
			return "stderr not found : "+stderr_file ;
		}
	}

}
