// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>
#include <filesystem>

#include <slurm/slurm.h>

#include "ext/cxxopts.hpp"

#include "generic.hh"

using namespace Disk ;

namespace Backends::Slurm {

	//
	// resources
	//

	struct RsrcsDataSingle {
		friend ::ostream& operator<<( ::ostream& , RsrcsDataSingle const& ) ;
		// accesses
		bool operator==(RsrcsDataSingle const&) const = default ;
		// data
		uint16_t cpu      = 0 ;        // number of logical cpu  (sbatch --cpus-per-task  option)
		uint32_t mem      = 1 ;        // memory in MB           (sbatch --mem            option) default : 1 MB (0 means single job, i.e. all available memory)
		uint32_t tmp      = 0 ;        // tmp disk in MB         (sbatch --tmp            option)
		::string part     ;            // partition name         (sbatch -p,--partition   option)
		::string gres     ;            // generic resources      (sbatch --gres           option)
		::string licence  ;            // licence                (sbtach -L,--licenses    option)
		::string feature  ;            // feature/contraint      (sbatch -C,--constraint  option)
		::string qos      ;            // Quality Of Service     (sbtach -q,--qos         option)
		::string reserv   ;            // Reservation            (sbtach -r,--reservation option)
		::string excludes ;            // List of required nodes (sbatch -w,--nodelist    option)
		::string nodes    ;            // List of excludes nodes (sbatch -x,--exclude     option)
	} ;

	struct RsrcsData : ::vector<RsrcsDataSingle> {
		using Base = ::vector<RsrcsDataSingle> ;
		// cxtors & casts
		RsrcsData(                ) = default ;
		RsrcsData(::vmap_ss const&) ;
		// services
		::vmap_ss mk_vmap(void) const ;
	} ;

	RsrcsData blend( RsrcsData&& rsrcs , RsrcsData const& force ) ;

}

namespace std {
	template<> struct hash<Backends::Slurm::RsrcsData> {
		size_t operator()(Backends::Slurm::RsrcsData const& rs) const {
			Hash::Xxh h ;
			h.update(rs.size()) ;
			for( auto r : rs ) {
				h.update(r.cpu    ) ;
				h.update(r.mem    ) ;
				h.update(r.tmp    ) ;
				h.update(r.part   ) ;
				h.update(r.gres   ) ;
				h.update(r.licence) ;
				h.update(r.feature) ;
			}
			return +::move(h).digest() ;
		}
	} ;
}

//
// SlurmBackend
//

namespace Backends::Slurm {

	using p_cxxopts = ::unique_ptr<cxxopts::Options> ;

	namespace SlurmApi {
		bool/*ok*/ init() ;
	}
	p_cxxopts                 createParser         (                        ) ;
	RsrcsData                 parseSlurmArgs       (::string const& args    ) ;
	void                      slurm_cancel         (uint32_t        slurm_id) ;
	::pair_s<Bool3/*job_ok*/> slurm_job_state      (uint32_t        slurm_id) ;
	::string                  readStderrLog        (JobIdx                  ) ;
	::string                  slurm_priority_params(                        ) ;
	//
	uint32_t slurm_spawn_job( ::string const& key , JobIdx , int32_t nice , ::vector_s const& cmd_line , RsrcsData const& rsrcs , bool verbose ) ;

	p_cxxopts g_optParse = createParser() ;

	constexpr Tag MyTag = Tag::Slurm ;

	struct SlurmBackend
	:	             GenericBackend<MyTag,uint32_t,RsrcsData,RsrcsData,false/*IsLocal*/>
	{	using Base = GenericBackend<MyTag,uint32_t,RsrcsData,RsrcsData,false/*IsLocal*/> ;

		struct SpawnedMap : ::umap<Rsrcs,JobIdx> {
			// count number of jobs spawned but not started yet
			// no entry is equivalent to entry with 0
			void inc(Rsrcs rs) { try_emplace(rs,0).first->second++ ; }         // create 0 entry if necessary
			void dec(Rsrcs rs) {                                               // entry must exist
				auto sit = find(rs) ;
				if(!--sit->second) erase(sit) ;                                // no entry means 0, so collect when possible (questionable)
			}
			JobIdx n_spawned(Rsrcs rs) {
				auto it = find(rs) ;
				if (it==end()) return 0          ;                             // no entry means 0
				else           return it->second ;
			}
		} ;

		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			SlurmBackend& self = *new SlurmBackend ;
			s_register(MyTag,self) ;
		}

		// accesses

		virtual Bool3 call_launch_after_start() const { return Maybe ; }       // if Maybe, only launch jobs w/ same resources
		virtual Bool3 call_launch_after_end  () const { return No    ; }       // .

		// services
		virtual bool/*ok*/ config(Config::Backend const& config) {
			if(!SlurmApi::init()) return false ;
			repo_key = base_name(*g_root_dir)+':' ;                            // cannot put this code directly as init value as g_root_dir is not available early enough
			for( auto const& [k,v] : config.dct ) {
				try {
					switch (k[0]) {
						case 'n' : if(k=="n_max_queue_jobs") { n_max_queue_jobs = from_chars<uint32_t>(v) ; continue ; } break ;
						case 'r' : if(k=="repo_key"        ) { repo_key         =                      v  ; continue ; } break ;
						case 'u' : if(k=="use_nice"        ) { use_nice         = from_chars<bool    >(v) ; continue ; } break ;
						default : ;
					}
				} catch (::string const& e) { throw to_string("wrong value for entry "   ,k,": ",v) ; }
				/**/                          throw to_string("unexpected config entry: ",k       ) ;
			}
			if (use_nice)
				if ( ::string spp=slurm_priority_params() ; !spp.empty() ) {
					static ::string const to_mrkr  = "time_origin=" ;
					static ::string const npd_mrkr = "nice_factor=" ;
					//
					size_t pos = spp.find(to_mrkr) ;
					if (pos!=Npos) {
						pos += to_mrkr.size() ;
						size_t end = spp.find(',',pos) ;
						time_origin = Pdate(spp.substr(pos,end)) ;
					}
					//
					pos = spp.find(npd_mrkr) ;
					if (pos!=Npos) {
						pos += npd_mrkr.size() ;
						size_t end = spp.find(',',pos) ;
						nice_factor = from_chars<float>(spp.substr(pos,end)) ;
					}
				}
			return true ;
		}
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& capacity ) const {
			bool             single = false             ;
			::umap_s<size_t> capa   = mk_umap(capacity) ;
			::umap_s<size_t> rs     ;
			for( auto&& [k,v] : rsrcs ) {
				if      ( capa.contains(k)                     ) { size_t s = from_string_rsrc<size_t>(k,v) ; rs[::move(k)] = s ; } // capacities of local backend are only integer information
				else if ( k=="gres" && !v.starts_with("shard") ) { single = true ;                                                }
			}
			::vmap_ss res ;
			if (single) for( auto&& [k,v] : rs ) { ::string s = to_string_rsrc(k,        capa[k] ) ; res.emplace_back( ::move(k) , ::move(s) ) ; }
			else        for( auto&& [k,v] : rs ) { ::string s = to_string_rsrc(k,::min(v,capa[k])) ; res.emplace_back( ::move(k) , ::move(s) ) ; }
			return res ;
		}
		virtual void open_req( ReqIdx req , JobIdx n_jobs ) {
			Base::open_req(req,n_jobs) ;
			grow(req_forces,req) = parseSlurmArgs(Req(req)->options.flag_args[+ReqFlag::Backend]) ;
		}
		virtual void close_req(ReqIdx req) {
			Base::close_req(req) ;
			if(reqs.empty()) SWEAR(spawned_rsrcs.empty(),spawned_rsrcs) ;
		}

		virtual RsrcsData adapt  ( RsrcsData const& rsa              ) const { return rsa                        ; }
		virtual ::vmap_ss export_( RsrcsData const& rs               ) const { return rs.mk_vmap()               ; }
		virtual RsrcsData import_( ::vmap_ss     && rsa , ReqIdx req ) const { return blend(rsa,req_forces[req]) ; }
		//
		virtual bool/*ok*/ fit_now(RsrcsAsk rsa) const {
			return spawned_rsrcs.n_spawned(rsa) < n_max_queue_jobs ;
		}
		virtual ::string start_job( JobIdx , SpawnedEntry const& se ) const {
			spawned_rsrcs.dec(se.rsrcs) ;
			return to_string("slurm_id:",se.id) ;
		}
		virtual ::pair_s<bool/*retry*/> end_job( JobIdx j , SpawnedEntry const& se , Status s ) const {
			Delay relax_time = g_config.network_delay+Delay(1) ;
			if ( !se.verbose && s>Status::Garbage ) return {{},true/*retry*/} ;                         // common case, must be fast
			::pair_s<Bool3/*job_ok*/> info ;
			for( int c=0 ; c<2 ; c++ ) {
				for( Delay i{0} ; i<relax_time ; Delay(0.1).sleep_for(),i+=Delay(0.1) ) { // wait a little while hoping job is dying
					info = slurm_job_state(se.id) ;
					if (info.second!=Maybe) goto JobDead ;
				}
				if (c==0) slurm_cancel(se.id) ;                                // if still alive after a little while, cancel job and retry
			}
			info.first = "job is still alive" ;
		JobDead :
			if (se.verbose) {
				::string stderr = readStderrLog(j) ;
				if (!stderr.empty()) info.first = ensure_nl(::move(info.first))+stderr ; // full report
			}
			return { info.first , info.second!=No } ;
		}
		virtual ::pair_s<bool/*alive*/> heartbeat_queued_job( JobIdx j , SpawnedEntry const& se ) const {
			::pair_s<Bool3/*job_ok*/> info = slurm_job_state(se.id) ;
			if (info.second==Maybe) return {{},true/*alive*/} ;
			//
			if ( info.second==No && se.verbose ) {
				::string stderr = readStderrLog(j) ;
				if (!stderr.empty()) info.first = ensure_nl(::move(info.first))+stderr ;
			}
			spawned_rsrcs.dec(se.rsrcs) ;
			return { info.first , false/*alive*/ } ;
		}
		virtual void kill_queued_job( JobIdx , SpawnedEntry const& se ) const {
			slurm_cancel(se.id) ;
			spawned_rsrcs.dec(se.rsrcs) ;
		}
		virtual uint32_t launch_job( JobIdx j , Pdate prio , ::vector_s const& cmd_line , Rsrcs const& rs , bool verbose ) const {
			int32_t nice = use_nice ? int32_t((prio-time_origin).sec()*nice_factor) : 0 ;
			nice &= 0x7fffffff ;                                                          // slurm will not accept negative values, default values overflow in ... 2091
			spawned_rsrcs.inc(rs) ;
			return slurm_spawn_job( repo_key , j , nice , cmd_line , *rs , verbose ) ;
		}

		// data
		SpawnedMap mutable  spawned_rsrcs    ;                                 // number of spawned jobs queued in slurm queue
		::vector<RsrcsData> req_forces       ;                                 // indexed by req, resources forced by req
		uint32_t            n_max_queue_jobs = -1                         ;    // no limit by default
		bool                use_nice         = false                      ;
		::string            repo_key         ;                                 // a short identifier of the repository
		Pdate               time_origin      { "2023-01-01 00:00:00" }    ;    // this leaves room til 2091
		float               nice_factor      { 1                     }    ;    // conversion factor in the form of number of nice points per second
	} ;

	//
	// init
	//

	bool _inited = (SlurmBackend::s_init(),true) ;

	//
	// RsrcsData
	//

	::ostream& operator<<( ::ostream& os , RsrcsDataSingle const& rsds ) {
		/**/                        os <<'('<< rsds.cpu        ;
		/**/                        os <<','<< rsds.mem <<"MB" ;
		if ( rsds.tmp             ) os <<','<< rsds.tmp <<"MB" ;
		if (!rsds.part    .empty()) os <<','<< rsds.part       ;
		if (!rsds.gres    .empty()) os <<','<< rsds.gres       ;
		if (!rsds.licence .empty()) os <<','<< rsds.licence    ;
		if (!rsds.feature .empty()) os <<','<< rsds.feature    ;
		if (!rsds.qos     .empty()) os <<','<< rsds.qos        ;
		if (!rsds.reserv  .empty()) os <<','<< rsds.reserv     ;
		if (!rsds.excludes.empty()) os <<','<< rsds.excludes   ;
		if (!rsds.nodes   .empty()) os <<','<< rsds.nodes      ;
		return                      os <<')'                   ;
	}

	inline RsrcsData::RsrcsData(::vmap_ss const& m) : Base{1} {                                // ensure we have at least 1 entry as we sometimes access element 0
		auto fill = [&]( ::string const& k , const char* field=nullptr ) -> RsrcsDataSingle& {
			if ( field && k.starts_with(field) ) return grow( *this , from_chars<uint32_t>(&k[strlen(field)],true/*empty_ok*/) ) ;
			else                                 throw to_string("no resource ",k," for backend ",mk_snake(MyTag)) ;
		} ;
		for( auto const& [k,v] : m ) {
			switch (k[0]) {
				case 'c' : fill(k,"cpu"     ).cpu      = from_string_with_units<    uint32_t>(v) ; break ;
				case 'e' : fill(k,"excludes").excludes =                                      v  ; break ;
				case 'f' : fill(k,"feature" ).feature  =                                      v  ; break ;
				case 'g' : fill(k,"gres"    ).gres     = "gres:"+                             v  ; break ;
				case 'l' : fill(k,"licence" ).licence  =                                      v  ; break ;
				case 'm' : fill(k,"mem"     ).mem      = from_string_with_units<'M',uint32_t>(v) ; break ;
				case 'n' : fill(k,"nodes"   ).nodes    =                                      v  ; break ;
				case 'p' : fill(k,"part"    ).part     =                                      v  ; break ;
				case 'q' : fill(k,"qos"     ).qos      =                                      v  ; break ;
				case 'r' : fill(k,"reserv"  ).reserv   =                                      v  ; break ;
				case 't' : fill(k,"tmp"     ).tmp      = from_string_with_units<'M',uint32_t>(v) ; break ;
				default  : fill(k) ;
			}
		}
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
		if (force.empty())
			for( size_t i=0 ; i<::min(rsrcs.size(),force.size()) ; i++ ) {
				RsrcsDataSingle const& force1 = force[i] ;
				if ( force1.cpu             ) rsrcs[i].cpu      = force1.cpu      ;
				if ( force1.mem             ) rsrcs[i].mem      = force1.mem      ;
				if ( force1.tmp             ) rsrcs[i].tmp      = force1.tmp      ;
				if (!force1.part    .empty()) rsrcs[i].part     = force1.part     ;
				if (!force1.gres    .empty()) rsrcs[i].gres     = force1.gres     ;
				if (!force1.licence .empty()) rsrcs[i].licence  = force1.licence  ;
				if (!force1.feature .empty()) rsrcs[i].feature  = force1.feature  ;
				if (!force1.qos     .empty()) rsrcs[i].qos      = force1.qos      ;
				if (!force1.reserv  .empty()) rsrcs[i].reserv   = force1.reserv   ;
				if (!force1.nodes   .empty()) rsrcs[i].nodes    = force1.nodes    ;
				if (!force1.excludes.empty()) rsrcs[i].excludes = force1.excludes ;
			}
		return rsrcs ;
	}

	//
	// slurm API
	//

	namespace SlurmApi {

		decltype(::slurm_free_ctl_conf                    )* free_ctl_conf                     = nullptr/*garbage*/ ;
		decltype(::slurm_free_job_info_msg                )* free_job_info_msg                 = nullptr/*garbage*/ ;
		decltype(::slurm_free_submit_response_response_msg)* free_submit_response_response_msg = nullptr/*garbage*/ ;
		decltype(::slurm_get_errno                        )* get_errno                         = nullptr/*garbage*/ ;
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

		template<class T> static inline bool/*ok*/ _loadFunc( void* handler , T*& dst , const char* name ) {
			dst = reinterpret_cast<T*>(::dlsym(handler,name)) ;
			return dst ;
		}
		bool/*ok*/ init() {
			void* handler = ::dlopen("libslurm.so",RTLD_NOW|RTLD_GLOBAL) ;
			return
				handler
			&&	_loadFunc( handler , free_ctl_conf                     , "slurm_free_ctl_conf"                     )
			&&	_loadFunc( handler , free_job_info_msg                 , "slurm_free_job_info_msg"                 )
			&&	_loadFunc( handler , free_submit_response_response_msg , "slurm_free_submit_response_response_msg" )
			&&	_loadFunc( handler , get_errno                         , "slurm_get_errno"                         )
			&&	_loadFunc( handler , init_job_desc_msg                 , "slurm_init_job_desc_msg"                 )
			&&	_loadFunc( handler , kill_job                          , "slurm_kill_job"                          )
			&&	_loadFunc( handler , load_ctl_conf                     , "slurm_load_ctl_conf"                     )
			&&	_loadFunc( handler , list_append                       , "slurm_list_append"                       )
			&&	_loadFunc( handler , list_create                       , "slurm_list_create"                       )
			&&	_loadFunc( handler , list_destroy                      , "slurm_list_destroy"                      )
			&&	_loadFunc( handler , load_job                          , "slurm_load_job"                          )
			&&	_loadFunc( handler , strerror                          , "slurm_strerror"                          )
			&&	_loadFunc( handler , submit_batch_het_job              , "slurm_submit_batch_het_job"              )
			&&	_loadFunc( handler , submit_batch_job                  , "slurm_submit_batch_job"                  )
			;
		}

	}

	p_cxxopts createParser() {
		p_cxxopts allocated{new cxxopts::Options("slurm","Slurm options parser for lmake")} ;
		allocated->add_options()
			( "c,cpus-per-task" , "cpus-per-task" , cxxopts::value<uint16_t>() )
			( "mem"             , "mem"           , cxxopts::value<uint32_t>() )
			( "tmp"             , "tmp"           , cxxopts::value<uint32_t>() )
			( "C,constraint"    , "Constraint"    , cxxopts::value<::string>() )
			( "gres"            , "gres"          , cxxopts::value<::string>() )
			( "L,licenses"      , "licenses"      , cxxopts::value<::string>() )
			( "p,partition"     , "Partition"     , cxxopts::value<::string>() )
			( "q,qos"           , "qos"           , cxxopts::value<::string>() )
			( "reservation"     , "reservation"   , cxxopts::value<::string>() )
			( "w,nodelist"      , "nodes"         , cxxopts::value<::string>() )
			( "x,exclude"       , "exclude nodes" , cxxopts::value<::string>() )
			( "h,help"          , "Print usage"                                )
		;
		return allocated;
	}

	RsrcsData parseSlurmArgs(::string const& args) {
		static ::string slurm = "slurm" ;                                        // apparently "slurm"s.data() does not work as memory is freed right away
		//
		if (args.empty()) return {} ;                                          // fast path
		//
		::vector_s compArgs  = split(args,' ')            ;
		uint32_t   argc      = 1                          ;
		char **    argv      = new char*[compArgs.size()] ;                    // large enough to hold all args (may not be entirely used if there are several RsrcsDataSingle's)
		RsrcsData  res       ;
		bool       seen_help = false                      ;
		//
		argv[0] = slurm.data() ;
		compArgs.push_back(":") ;                                              // sentinel to parse last args
		for ( ::string& ca : compArgs ) {
			if (ca!=":") {
				argv[argc++] = ca.data() ;
				continue ;
			}
			RsrcsDataSingle res1 ;
			try {
				auto result = g_optParse->parse(argc,argv) ;
				//
				if (result.count("cpus-per-task")) res1.cpu      = result["cpus-per-task"].as<uint16_t>() ;
				if (result.count("mem"          )) res1.mem      = result["mem"          ].as<uint32_t>() ;
				if (result.count("tmp"          )) res1.tmp      = result["tmp"          ].as<uint32_t>() ;
				if (result.count("constraint"   )) res1.feature  = result["constraint"   ].as<::string>() ;
				if (result.count("exclude"      )) res1.excludes = result["exclude"      ].as<::string>() ;
				if (result.count("gres"         )) res1.gres     = result["gres"         ].as<::string>() ;
				if (result.count("licenses"     )) res1.licence  = result["licenses"     ].as<::string>() ;
				if (result.count("nodelist"     )) res1.nodes    = result["nodelist"     ].as<::string>() ;
				if (result.count("partition"    )) res1.part     = result["partition"    ].as<::string>() ;
				if (result.count("qos"          )) res1.qos      = result["qos"          ].as<::string>() ;
				if (result.count("reservation"  )) res1.reserv   = result["reservation"  ].as<::string>() ;
				//
				if (result.count("help")) seen_help = true ;
			} catch (const cxxopts::exceptions::exception& e) {
				throw to_string("Error while parsing slurm options: ",e.what()) ;
			}
			res.push_back(res1) ;
			argc = 1 ;
		}
		delete[] argv ;
		if (seen_help) throw g_optParse->help() ;
		return res ;
	}

	void slurm_cancel(uint32_t slurm_id) {
		//This for loop with a retry comes from the scancel Slurm utility code
		//Normally we kill mainly waiting jobs, but some "just started jobs" could be killed like that also
		//Running jobs are killed by lmake/job_exec
		for( int i=0 ; i<10/*MAX_CANCEL_RETRY*/ ; i++ ) {
			int err = SlurmApi::kill_job(slurm_id,SIGKILL,KILL_FULL_JOB) ;
			if ( err==SLURM_SUCCESS || errno!=ESLURM_TRANSITION_STATE_NO_UPDATE ) {
				Trace trace("Cancel slurm jodid: ",slurm_id) ;
				return ;
			}
			sleep(5+i) ; // Retry
		}
		Trace trace("Error while killing job: ",slurm_id," error: ",SlurmApi::strerror(errno)) ;
	}

	::pair_s<Bool3/*job_ok*/> slurm_job_state(uint32_t slurm_id) {             // Maybe means job has not completed
		job_info_msg_t* resp = nullptr/*garbage*/ ;
		//
		if ( SlurmApi::load_job(&resp,slurm_id,SHOW_LOCAL) != SLURM_SUCCESS ) return { "cannot load job info : "s+SlurmApi::strerror(errno) , Yes/*job_ok*/ } ; // no info on job -> retry
		//
		bool completed = true ;                                                // job is completed if all tasks are
		for ( uint32_t i=0 ; i<resp->record_count ; i++ ) {
			slurm_job_info_t const& ji = resp->job_array[i]                          ;
			job_states              js = job_states( ji.job_state & JOB_STATE_BASE ) ;
			//
			completed &= js>=JOB_COMPLETE ;
			if (js<=JOB_COMPLETE) continue ;                                                                  // we only search errors
			const char* on_nodes  = !ji.nodes||::strchr(ji.nodes,' ')==nullptr?" on node : ":" on nodes : " ;
			int         exit_code = ji.exit_code                                                 ;
			// when job_exec receives a signal, the bash process which launches it (which the process seen by slurm) exits with an exit code > 128
			// however, the user is interested in the received signal, not mapped bash exit code, so undo mapping
			// signaled wstatus are barely the signal number
			if ( WIFEXITED(ji.exit_code) && WEXITSTATUS(ji.exit_code)>0x80 ) exit_code = WEXITSTATUS(ji.exit_code)-0x80 ;
			switch(js) {
				// if slurm sees job failure, somthing weird occurred (if actual job fails, job_exec reports an error and completes successfully) -> retry
				// possible job_states values (from slurm.h) :
				//   JOB_PENDING                                                                                                        queued waiting for initiation
				//   JOB_RUNNING                                                                                                        allocated resources and executing
				//   JOB_SUSPENDED                                                                                                      allocated resources, execution suspended
				//   JOB_COMPLETE                                                                                                       completed execution successfully
				case JOB_CANCELLED : return {           "cancelled by user"s                                     , Yes/*job_ok*/ } ; // cancelled by user
				case JOB_FAILED    : return { to_string("failed (",wstatus_str(exit_code),')',on_nodes,ji.nodes) , Yes/*job_ok*/ } ; // completed execution unsuccessfully
				case JOB_TIMEOUT   : return { to_string("timeout"                            ,on_nodes,ji.nodes) , No /*job_ok*/ } ; // terminated on reaching time limit
				case JOB_NODE_FAIL : return { to_string("node failure"                       ,on_nodes,ji.nodes) , Yes/*job_ok*/ } ; // terminated on node failure
				case JOB_PREEMPTED : return { to_string("preempted"                          ,on_nodes,ji.nodes) , Yes/*job_ok*/ } ; // terminated due to preemption
				case JOB_BOOT_FAIL : return { to_string("boot failure"                       ,on_nodes,ji.nodes) , Yes/*job_ok*/ } ; // terminated due to node boot failure
				case JOB_DEADLINE  : return { to_string("deadline reached"                   ,on_nodes,ji.nodes) , Yes/*job_ok*/ } ; // terminated on deadline
				case JOB_OOM       : return { to_string("out of memory"                      ,on_nodes,ji.nodes) , No /*job_ok*/ } ; // experienced out of memory error
				//   JOB_END                                                                                                            not a real state, last entry in table
				default : FAIL("Slurm: wrong job state return for job (",slurm_id,"): ",js) ;
			}
		}
		SlurmApi::free_job_info_msg(resp) ;
		return { {} , Maybe|completed } ;
	}

	inline ::string getLogPath      (JobIdx job) { return Job(job)->ancillary_file(AncillaryTag::Backend) ; }
	inline ::string getLogStderrPath(JobIdx job) { return getLogPath(job) + "/stderr"s                    ; }
	inline ::string getLogStdoutPath(JobIdx job) { return getLogPath(job) + "/stdout"s                    ; }

	::string readStderrLog(JobIdx job) {
		::string   err_file   = getLogStderrPath(job) ;
		::ifstream err_stream { err_file }            ;
		//
		if (!err_stream                  ) return to_string("stderr not found : ",err_file                        ) ;
		if (::istream::sentry(err_stream)) return to_string("stderr from : "     ,err_file,'\n',err_stream.rdbuf()) ; // /!\ rdbuf() fails on an empty file
		else                               return {}                                                                ;
	}

	inline ::string cmd_to_string(::vector_s const& cmd_line) {
		::string res = "#!/bin/sh" ;
		char sep = '\n' ;
		for ( ::string const& s : cmd_line ) { append_to_string(res,sep,s) ; sep = ' ' ; }
		res += '\n' ;
		return res ;
	}
	uint32_t slurm_spawn_job( ::string const& key , JobIdx job , int32_t nice , ::vector_s const& cmd_line , RsrcsData const& rsrcs , bool verbose ) {
		static char* env[1] = {const_cast<char *>("")} ;
		//
		SWEAR(rsrcs.size()> 0) ;
		SWEAR(nice        >=0) ;
		//
		::string                 wd        = *g_root_dir             ;
		auto                     job_name  = key + Job(job)->name()  ;
		::string                 script    = cmd_to_string(cmd_line) ;
		::string                 s_errPath ;
		::string                 s_outPath ;
		::vector<job_desc_msg_t> jDesc     { rsrcs.size() }          ;
		if(verbose) {
			s_errPath = getLogStderrPath(job) ;
			s_outPath = getLogStdoutPath(job) ;
			make_dir(getLogPath(job)) ;
		}
		for( uint32_t i=0 ; RsrcsDataSingle const& r : rsrcs ) {
			job_desc_msg_t* j = &jDesc[i] ;
			SlurmApi::init_job_desc_msg(j) ;
			//
			j->env_size        = 1                                                           ;
			j->environment     = env                                                         ;
			j->cpus_per_task   = r.cpu                                                       ;
			j->pn_min_memory   = r.mem                                                       ; //in MB
			j->pn_min_tmp_disk = r.tmp                                                       ; //in MB
			j->std_err         = verbose ? s_errPath.data() : const_cast<char*>("/dev/null") ;
			j->std_out         = verbose ? s_outPath.data() : const_cast<char*>("/dev/null") ;
			j->work_dir        = wd.data()                                                   ;
			j->name            = const_cast<char*>(job_name.c_str())                         ;
			//
			if(!r.excludes.empty()) j->exc_nodes     = const_cast<char*>(r.excludes.data()) ;
			if(!r.nodes   .empty()) j->req_nodes     = const_cast<char*>(r.nodes   .data()) ;
			if(!r.reserv  .empty()) j->reservation   = const_cast<char*>(r.reserv  .data()) ;
			if(!r.qos     .empty()) j->qos           = const_cast<char*>(r.qos     .data()) ;
			if(!r.feature .empty()) j->features      = const_cast<char*>(r.feature .data()) ;
			if(!r.licence .empty()) j->licenses      = const_cast<char*>(r.licence .data()) ;
			if(!r.part    .empty()) j->partition     = const_cast<char*>(r.part    .data()) ;
			if(!r.gres    .empty()) j->tres_per_node = const_cast<char*>(r.gres    .data()) ;
			if(i==0               ) j->script        =                   script    .data()  ;
			/**/                    j->nice          = NICE_OFFSET+nice                     ;
			i++ ;
		}
		int                    ret  = 0      /*garbage*/ ;
		submit_response_msg_t* jMsg = nullptr/*garbage*/ ;
		if (jDesc.size()==1) {
			ret = SlurmApi::submit_batch_job(&jDesc[0],&jMsg) ;
		} else {
			List jList = SlurmApi::list_create(nullptr) ;
			for ( job_desc_msg_t& c : jDesc ) SlurmApi::list_append(jList,&c) ;
			ret = SlurmApi::submit_batch_het_job(jList,&jMsg) ;
			SlurmApi::list_destroy(jList) ;
		}
		if (ret!=SLURM_SUCCESS) throw "Launch slurm job error: "s + SlurmApi::strerror(SlurmApi::get_errno()) ;
		SlurmApi::free_submit_response_response_msg(jMsg) ;
		return jMsg->job_id ;
	}

	::string slurm_priority_params() {
		slurm_conf_t* conf = nullptr/*garbage*/ ;
		SlurmApi::load_ctl_conf(0/*update_time*/,&conf) ;                      // XXX : remember last conf read so as to pass a real update_time param & optimize call
		//
		if ( conf && conf->priority_params ) { SlurmApi::free_ctl_conf(conf) ; return conf->priority_params ; }
		else                                 { SlurmApi::free_ctl_conf(conf) ; return {}                    ; }
	}

}
