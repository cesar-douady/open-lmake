// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>
#include <filesystem>

#include <slurm/slurm.h>

#include "ext/cxxopts.hpp"

#include "generic.hh"

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

	bool/*ok*/           loadSlurmApi     (                        ) ;
	p_cxxopts            createParser     (                        ) ;
	RsrcsData            parseSlurmArgs   (::string const& args    ) ;
	void                 slurm_cancel     (uint32_t        slurm_id) ;
	::pair_s<job_states> slurm_job_state  (uint32_t        slurm_id) ;
	::string             readStderrLog    (JobIdx                  ) ;
	Pdate                slurm_time_origin(                        ) ;
	//
	uint32_t slurm_spawn_job( JobIdx , int32_t nice , ::vector_s const& cmd_line , RsrcsData const& rsrcs , bool verbose ) ;

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

		// services
		virtual bool config(Config::Backend const& config) {
			if(!loadSlurmApi()) return false ;
			::vmap_ss force ;
			for( auto const& [k,v] : config.dct ) {
				try {
					switch (k[0]) {
						case 'n' : if(k=="n_max_queue_jobs") { n_max_queue_jobs = from_chars<uint32_t>(v) ; continue ; } break ;
						case 's' : if(k=="slurm_args"      ) { config_force     = parseSlurmArgs      (v) ; continue ; } break ;
						case 'u' : if(k=="use_nice"        ) { use_nice         = from_chars<bool    >(v) ; continue ; } break ;
						default : ;
					}
				} catch (::string const& e) { throw to_string("wrong value for entry ",k,": ",v) ; }
				throw "unexpected config entry: "+k ;
			}
			if (use_nice) time_origin = slurm_time_origin() ;
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
			//
			RsrcsData req_force = parseSlurmArgs(Req(req)->options.flag_args[+ReqFlag::Backend]) ;
			grow(req_forces,req) = blend(RsrcsData(config_force),req_force) ;
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
		virtual ::pair_s<Bool3/*launch*/> start_job( JobIdx , SpawnedEntry const& se ) const {
			spawned_rsrcs.dec(se.rsrcs) ;
			return { to_string("slurm_id:",se.id) , Maybe/*launch*/ } ;        // only launch jobs w/ same resources
		}
		virtual ::pair_s<Bool3/*launch*/> end_job( JobIdx j , SpawnedEntry const& se , Status s ) const {
			if (!( s==Status::Err && se.verbose )) return {{},No/*launch*/} ;
			//
			//for( int i=0 ; i<100 ; Delay(0.1).sleep_for(),i++ )                // Ensure slurm is done so that its stderr is available
			//	if (slurm_job_state(se.id).second>=JOB_COMPLETE)
			//		return { readStderrLog(j) , false/*launch*/ } ;
			//return { "cannot get error log" , No/*lauch*/ } ;
			(void)se ;                                                         // XXX : replace by above code when validated
			Delay(1.).sleep_for() ;                                            // .
			return { readStderrLog(j) , No/*launch*/ } ;                       // .
		}
		virtual ::pair_s<Bool3/*ok*/> heartbeat_queued_job( JobIdx j , SpawnedEntry const& se ) const {
			::pair_s<job_states> info = slurm_job_state(se.id) ;
			if (info.second<JOB_COMPLETE) return {{},Yes/*ok*/} ;
			//
			bool isErr = info.second==JOB_FAILED || info.second==JOB_OOM ;
			if ( isErr && se.verbose ) append_to_string(info.first,'\n',readStderrLog(j)) ;
			spawned_rsrcs.dec(se.rsrcs) ;
			return { info.first , Maybe&!isErr } ;
		}
		virtual void kill_queued_job( JobIdx , SpawnedEntry const& se ) const {
			slurm_cancel(se.id) ;
			spawned_rsrcs.dec(se.rsrcs) ;
		}
		virtual uint32_t launch_job( JobIdx j , Pdate prio , ::vector_s const& cmd_line , Rsrcs const& rs , bool verbose ) const {
			int32_t nice = 0 ;
			if (use_nice) nice = (prio-time_origin).sec()&0x7fffffff ;                   // slurm will not accept negative values, protect against overflow in 2091...
			uint32_t slurm_id = slurm_spawn_job( j , nice , cmd_line , *rs , verbose ) ;
			spawned_rsrcs.inc(rs) ;
			return slurm_id ;
		}

		// data
		SpawnedMap mutable  spawned_rsrcs    ;                                 // number of spawned jobs queued in slurm queue
		::vector<RsrcsData> req_forces       ;                                 // indexed by req, resources forced by req
		RsrcsData           config_force     ;                                 // resources forced by config
		uint32_t            n_max_queue_jobs = -1                      ;       // no limit by default
		bool                use_nice         = false                   ;
		Pdate               time_origin      { "2023-01-01 00:00:00" } ;       // this leaves room til 2091
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

	inline RsrcsData::RsrcsData(::vmap_ss const& m) : Base{1} {                                // force at least one entry
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

	#define DECL_DYN_SYMBOL(symbol) decltype(::symbol)* DYNAPI_##symbol
	DECL_DYN_SYMBOL(slurm_free_ctl_conf                    ) ;
	DECL_DYN_SYMBOL(slurm_free_job_info_msg                ) ;
	DECL_DYN_SYMBOL(slurm_free_submit_response_response_msg) ;
	DECL_DYN_SYMBOL(slurm_get_errno                        ) ;
	DECL_DYN_SYMBOL(slurm_init_job_desc_msg                ) ;
	DECL_DYN_SYMBOL(slurm_kill_job                         ) ;
	DECL_DYN_SYMBOL(slurm_load_ctl_conf                    ) ;
	DECL_DYN_SYMBOL(slurm_list_append                      ) ;
	DECL_DYN_SYMBOL(slurm_list_create                      ) ;
	DECL_DYN_SYMBOL(slurm_list_destroy                     ) ;
	DECL_DYN_SYMBOL(slurm_load_job                         ) ;
	DECL_DYN_SYMBOL(slurm_strerror                         ) ;
	DECL_DYN_SYMBOL(slurm_submit_batch_het_job             ) ;
	DECL_DYN_SYMBOL(slurm_submit_batch_job                 ) ;
	#undef DECL_DYN_SYMBOL

	bool/*ok*/ loadSlurmApi() {
		void* slurmHandler = dlopen("libslurm.so",RTLD_NOW|RTLD_GLOBAL) ;
		if (!slurmHandler) return false/*ok*/ ;
		#define SET_SLURM_API(func) \
			DYNAPI_##func = reinterpret_cast<decltype(::func)*>(::dlsym(slurmHandler,#func)) ; \
			if(DYNAPI_##func==NULL) return false/*ok*/
		SET_SLURM_API(slurm_free_ctl_conf                    ) ;
		SET_SLURM_API(slurm_free_job_info_msg                ) ;
		SET_SLURM_API(slurm_free_submit_response_response_msg) ;
		SET_SLURM_API(slurm_get_errno                        ) ;
		SET_SLURM_API(slurm_init_job_desc_msg                ) ;
		SET_SLURM_API(slurm_kill_job                         ) ;
		SET_SLURM_API(slurm_load_ctl_conf                    ) ;
		SET_SLURM_API(slurm_list_append                      ) ;
		SET_SLURM_API(slurm_list_create                      ) ;
		SET_SLURM_API(slurm_list_destroy                     ) ;
		SET_SLURM_API(slurm_load_job                         ) ;
		SET_SLURM_API(slurm_strerror                         ) ;
		SET_SLURM_API(slurm_submit_batch_het_job             ) ;
		SET_SLURM_API(slurm_submit_batch_job                 ) ;
		#undef SET_SLURM_API
		return true /*ok*/ ;
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
			int err = DYNAPI_slurm_kill_job(slurm_id,SIGKILL,KILL_FULL_JOB) ;
			if ( err==SLURM_SUCCESS || errno!=ESLURM_TRANSITION_STATE_NO_UPDATE ) {
				Trace trace("Cancel slurm jodid: ",slurm_id) ;
				return ;
			}
			sleep(5+i) ; // Retry
		}
		Trace trace("Error while killing job: ",slurm_id," error: ",DYNAPI_slurm_strerror(errno)) ;
	}

	::pair_s<job_states> slurm_job_state(uint32_t slurm_id) {
		job_info_msg_t* resp = nullptr/*garbage*/ ;
		if ( DYNAPI_slurm_load_job(&resp,slurm_id,SHOW_LOCAL) != SLURM_SUCCESS )
			return { to_string("Error while loading job info (",slurm_id,"): ",DYNAPI_slurm_strerror(errno)) , JOB_BOOT_FAIL } ;
		//
		job_states job_state = job_states( resp->job_array[0].job_state & JOB_STATE_BASE ) ;
		::string   info      ;
		//
		for ( uint32_t i=0 ; i<resp->record_count ; i++ ) {
			slurm_job_info_t const& ji = resp->job_array[i]                          ;
			job_states              js = job_states( ji.job_state & JOB_STATE_BASE ) ;
			//
			if( js>JOB_COMPLETE && job_state<=JOB_COMPLETE ) { // first error is reported
				job_state = js ;
				switch(js) {
					// possible job_states values (from slurm.h) :
					//   JOB_PENDING                                                                                                    queued waiting for initiation
					//   JOB_RUNNING                                                                                                    allocated resources and executing
					//   JOB_SUSPENDED                                                                                                  allocated resources, execution suspended
					//   JOB_COMPLETE                                                                                                   completed execution successfully
					case JOB_CANCELLED : info =           "Job cancelled by user"s                                         ; break ; // cancelled by user
					case JOB_FAILED    : info = to_string("Failed (exit code = ",ji.exit_code,") on node(s): "  ,ji.nodes) ; break ; // completed execution unsuccessfully
					case JOB_TIMEOUT   : info = to_string("Job terminated on reaching time limit on node(s): "  ,ji.nodes) ; break ; // terminated on reaching time limit
					case JOB_NODE_FAIL : info = to_string("Job terminated on node failure on node(s): "         ,ji.nodes) ; break ; // terminated on node failure
					case JOB_PREEMPTED : info = to_string("Job terminated due to preemption on node(s): "       ,ji.nodes) ; break ; // terminated due to preemption
					case JOB_BOOT_FAIL : info = to_string("Job terminated due to node boot failure on node(s): ",ji.nodes) ; break ; // terminated due to node boot failure
					case JOB_DEADLINE  : info = to_string("Job terminated on deadline on node(s): "             ,ji.nodes) ; break ; // terminated on deadline
					case JOB_OOM       : info = to_string("Out of memory killed on node(s): "                   ,ji.nodes) ; break ; // experienced out of memory error
					//   JOB_END                                                                                                        not a real state, last entry in table
					default : FAIL("Slurm: wrong job state return for job (",slurm_id,"): ",job_state) ;
				}
			}
		}
		DYNAPI_slurm_free_job_info_msg(resp) ;
		return {info,job_state} ;
	}

	inline ::string getLogPath      (JobIdx job) { return Job(job)->ancillary_file(AncillaryTag::Backend) ; }
	inline ::string getLogStderrPath(JobIdx job) { return getLogPath(job) + "/stderr"s                    ; }
	inline ::string getLogStdoutPath(JobIdx job) { return getLogPath(job) + "/stdout"s                    ; }

	::string readStderrLog(JobIdx job) {
		::string   err_file   = getLogStderrPath(job) ;
		::ifstream err_stream { err_file }            ;
		if (err_stream) return to_string( "Error from: "           , err_file , '\n' , err_stream.rdbuf() ) ;
		else            return to_string( "Error file not found: " , err_file , '\n'                      ) ;
	}

	inline ::string cmd_to_string(::vector_s const& cmd_line) {
		::string res = "#!/bin/sh" ;
		char sep = '\n' ;
		for ( ::string const& s : cmd_line ) { append_to_string(res,sep,s) ; sep = ' ' ; }
		res += '\n' ;
		return res ;
	}
	// XXX : use prio to compute a nice value
	uint32_t slurm_spawn_job( JobIdx job , int32_t nice , ::vector_s const& cmd_line , RsrcsData const& rsrcs , bool verbose ) {
		static char* env[1] = {const_cast<char *>("")} ;
		//
		SWEAR(rsrcs.size()> 0) ;
		SWEAR(nice        >=0) ;
		//
		::string                 wd        = *g_root_dir                                         ;
		auto                     job_name  = *(--::filesystem::path(wd).end()) / Job(job).name() ; // ="repoDir/target" // XXX : add a key in config as this may not be pertinent for all users
		::string                 script    = cmd_to_string(cmd_line)                             ;
		::string                 s_errPath ;
		::string                 s_outPath ;
		::vector<job_desc_msg_t> jDesc     { rsrcs.size() }                                      ;
		if(verbose) {
			s_errPath = getLogStderrPath(job) ;
			s_outPath = getLogStdoutPath(job) ;
			Disk::make_dir(getLogPath(job)) ;
		}
		for( uint32_t i=0 ; RsrcsDataSingle const& r : rsrcs ) {
			job_desc_msg_t* j = &jDesc[i] ;
			DYNAPI_slurm_init_job_desc_msg(j) ;
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
			ret = DYNAPI_slurm_submit_batch_job(&jDesc[0],&jMsg) ;
		} else {
			List jList = DYNAPI_slurm_list_create(nullptr) ;
			for ( job_desc_msg_t& c : jDesc ) DYNAPI_slurm_list_append(jList,&c) ;
			ret = DYNAPI_slurm_submit_batch_het_job(jList,&jMsg) ;
			DYNAPI_slurm_list_destroy(jList) ;
		}
		if (ret!=SLURM_SUCCESS) throw "Launch slurm job error: "s + DYNAPI_slurm_strerror(DYNAPI_slurm_get_errno()) ;
		DYNAPI_slurm_free_submit_response_response_msg(jMsg) ;
		return jMsg->job_id ;
	}

	// XXX : this is not really necessary as default value is ok til 2091, but we keep the code as we will shortly need to gather resources to improve interface
	Pdate slurm_time_origin() {
		static ::string const mrkr = "time_origin=" ;
		//
		Pdate         res  ;
		slurm_conf_t* conf = nullptr/*garbage*/ ;
		DYNAPI_slurm_load_ctl_conf(0/*update_time*/,&conf) ;                   // XXX : remember last conf read so as to pass a real update_time param & optimize call
		//
		if ( conf && conf->priority_params ) {
			::string_view params = conf->priority_params ;
			size_t        pos    = params.find(mrkr)     ;
			if (pos!=Npos) {
				pos += mrkr.size() ;
				size_t end = params.find(',',pos) ;
				res = Pdate(params.substr(pos,end)) ;
			}
		}
		//
		DYNAPI_slurm_free_ctl_conf(conf) ;
		//
		return res ;
	}

}
