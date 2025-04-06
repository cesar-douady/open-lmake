// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dlfcn.h>
#include <setjmp.h>

#include "slurm_api.hh"

namespace Backends::Slurm::SlurmApi { // necessary to ensure all types are different, which is important for LTO
	#include <slurm/slurm.h>
}

using namespace Disk ;

namespace Backends::Slurm::SlurmApi {

	static constexpr int SlurmSpawnTrials  = 15 ;
	static constexpr int SlurmCancelTrials = 10 ;

	// ensure functions we call before knowing slurm version have a compatible prototype
	static_assert( requires(                ) { slurm_init         (""                                             ) ; } ) ;
	static_assert( requires(slurm_conf_t* sc) { slurm_load_ctl_conf(time_t(0),reinterpret_cast<slurm_conf_t**>(&sc)) ; } ) ;
	static_assert( requires(slurm_conf_t* sc) { slurm_free_ctl_conf(          reinterpret_cast<slurm_conf_t* >( sc)) ; } ) ;

	static decltype(slurm_free_job_info_msg                )* _free_job_info_msg                 = nullptr/*garbage*/ ;
	static decltype(slurm_free_submit_response_response_msg)* _free_submit_response_response_msg = nullptr/*garbage*/ ;
	static decltype(slurm_init_job_desc_msg                )* _init_job_desc_msg                 = nullptr/*garbage*/ ;
	static decltype(slurm_kill_job                         )* _kill_job                          = nullptr/*garbage*/ ;
	static decltype(slurm_list_append                      )* _list_append                       = nullptr/*garbage*/ ;
	static decltype(slurm_list_create                      )* _list_create                       = nullptr/*garbage*/ ;
	static decltype(slurm_list_destroy                     )* _list_destroy                      = nullptr/*garbage*/ ;
	static decltype(slurm_load_job                         )* _load_job                          = nullptr/*garbage*/ ;
	static decltype(slurm_strerror                         )* _strerror                          = nullptr/*garbage*/ ;
	static decltype(slurm_submit_batch_het_job             )* _submit_batch_het_job              = nullptr/*garbage*/ ;
	static decltype(slurm_submit_batch_job                 )* _submit_batch_job                  = nullptr/*garbage*/ ;

	static ::string _cmd_to_string(::vector_s const& cmd_line) {
		::string res   = "#!/bin/sh" ;
		First    first ;
		for ( ::string const& s : cmd_line ) res <<first("\n"," ")<< s ;
		res += '\n' ;
		return res ;
	}
	static SlurmId _spawn_job(
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
			stderr_file = get_stderr_file(job) ;
			stdout_file = get_stdout_file(job) ;
			mk_dir_s(get_log_dir_s(job)) ;
		}
		for( uint32_t i=0 ; RsrcsDataSingle const& r : rsrcs ) {
			//                            first element            other elements
			job_desc_msg_t& j    = i==0 ? job_desc0              : job_descs.emplace_back()               ;              // keep alive
			::string      & gres = i==0 ? (gres0="gres:"+r.gres) : gress    .emplace_back("gres:"+r.gres) ;              // .
			//
			_init_job_desc_msg(&j) ;
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
			if(+r.excludes ) j.exc_nodes     = const_cast<char*>(r.excludes .data()) ;
			if(+r.features ) j.features      = const_cast<char*>(r.features .data()) ;
			if(+r.licenses ) j.licenses      = const_cast<char*>(r.licenses .data()) ;
			if(+r.nodes    ) j.req_nodes     = const_cast<char*>(r.nodes    .data()) ;
			if(+r.partition) j.partition     = const_cast<char*>(r.partition.data()) ;
			if(+r.qos      ) j.qos           = const_cast<char*>(r.qos      .data()) ;
			if(+r.reserv   ) j.reservation   = const_cast<char*>(r.reserv   .data()) ;
			if(+r.gres     ) j.tres_per_node =                   gres       .data()  ;
			if(i==0        ) j.script        =                   script     .data()  ;
			/**/             j.nice          = NICE_OFFSET+nice                      ;
			i++ ;
		}
		for( int i=0 ; i<SlurmSpawnTrials ; i++ ) {
			submit_response_msg_t* msg = nullptr/*garbage*/ ;
			bool                   err = false  /*garbage*/ ;
			errno = 0 ;                                                                // normally useless
			{	Lock lock { slurm_mutex } ;
				if (!job_descs) {                                                      // single element case
					err = _submit_batch_job(&job_desc0,&msg)!=SLURM_SUCCESS ;
				} else {                                                               // multi-elements case
					auto* l = _list_create(nullptr/*dealloc_func*/) ;                  // depending on version, this may be List* or list_t*
					/**/                                  _list_append(l,&job_desc0) ; // first element
					for ( job_desc_msg_t& c : job_descs ) _list_append(l,&c        ) ; // other elements
					err = _submit_batch_het_job(l,&msg)!=SLURM_SUCCESS ;
					_list_destroy(l) ;
				}
			}
			int sav_errno = errno ;                                                    // save value before calling any slurm or libc function
			if (msg) {
				SlurmId res = msg->job_id ;
				SWEAR(res!=0) ;                                                        // null id is used to signal absence of id
				_free_submit_response_response_msg(msg) ;
				if (!sav_errno) { SWEAR(!err) ; return res ; }
			}
			SWEAR(sav_errno!=0) ;                                                      // if err, we should have a errno, else if no errno, we should have had a msg containing an id
			::string err_msg ;
			switch (sav_errno) {
				case EAGAIN                              :
				case ESLURM_ERROR_ON_DESC_TO_RECORD_COPY :
				case ESLURM_NODES_BUSY                   : {
					trace("retry",sav_errno,_strerror(sav_errno)) ;
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
					err_msg = job_desc0.licenses ;                                     // licenses are only on first step
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
			throw "slurm spawn job error : "s+_strerror(sav_errno)+(+err_msg?" (":"")+err_msg+(+err_msg?")":"") ;
		}
		trace("cannot_spawn") ;
		throw "cannot connect to slurm daemon"s ;
	}

	static ::pair_s<Bool3/*job_ok*/> _job_state(SlurmId slurm_id) {                                                         // Maybe means job has not completed
		Trace trace(BeChnl,"slurm_job_state",slurm_id) ;
		SWEAR(slurm_id) ;
		job_info_msg_t* resp = nullptr/*garbage*/ ;
		{	Lock lock { slurm_mutex } ;
			if (_load_job(&resp,slurm_id,SHOW_LOCAL)!=SLURM_SUCCESS) switch (errno) {
				case EAGAIN                              :
				case ESLURM_ERROR_ON_DESC_TO_RECORD_COPY : //!                                                   job_ok
				case ESLURM_NODES_BUSY                   : return { "slurm daemon busy : "s   +_strerror(errno) , Maybe } ; // no info : heartbeat will retry, end will eventually cancel
				default                                  : return { "cannot load job info : "s+_strerror(errno) , Yes   } ;
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
				case JOB_PENDING   :                              ok = Maybe ; continue  ;                                  // queued waiting for initiation
				case JOB_RUNNING   :                              ok = Maybe ; continue  ;                                  // allocated resources and executing
				case JOB_SUSPENDED :                              ok = Maybe ; continue  ;                                  // allocated resources, execution suspended
				case JOB_COMPLETE  :                                           continue  ;                                  // completed execution successfully
				case JOB_CANCELLED : msg = "cancelled by user"s ; ok = Yes   ; goto Done ;                                  // cancelled by user
				case JOB_TIMEOUT   : msg = "timeout"s           ; ok = No    ; goto Done ;                                  // terminated on reaching time limit
				case JOB_NODE_FAIL : msg = "node failure"s      ; ok = Yes   ; goto Done ;                                  // terminated on node failure
				case JOB_PREEMPTED : msg = "preempted"s         ; ok = Yes   ; goto Done ;                                  // terminated due to preemption
				case JOB_BOOT_FAIL : msg = "boot failure"s      ; ok = Yes   ; goto Done ;                                  // terminated due to node boot failure
				case JOB_DEADLINE  : msg = "deadline reached"s  ; ok = Yes   ; goto Done ;                                  // terminated on deadline
				case JOB_OOM       : msg = "out of memory"s     ; ok = No    ; goto Done ;                                  // experienced out of memory error
				//   JOB_END                                                                                                // not a real state, last entry in table
				case JOB_FAILED :                                                                                           // completed execution unsuccessfully
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
		_free_job_info_msg(resp) ;
		return { msg , ok } ;
	}

	static void _cancel(SlurmId slurm_id) {
		//This for loop with a retry comes from the scancel Slurm utility code
		//Normally we kill mainly waiting jobs, but some "just started jobs" could be killed like that also
		//Running jobs are killed by lmake/job_exec
		Trace trace(BeChnl,"slurm_cancel",slurm_id) ;
		int  i    = 0/*garbage*/   ;
		Lock lock { slurm_mutex } ;
		for( i=0 ; i<SlurmCancelTrials ; i++ ) {
			if (_kill_job(slurm_id,SIGKILL,KILL_FULL_JOB)==SLURM_SUCCESS) { trace("done") ; return ; }
			switch (errno) {
				case ESLURM_INVALID_JOB_ID             :
				case ESLURM_ALREADY_DONE               : trace("already_dead",errno) ;                return ;
				case ESLURM_TRANSITION_STATE_NO_UPDATE : trace("retry",i)            ; ::sleep(1+i) ; break  ;
				default : goto Bad ;
			}
		}
	Bad :
		FAIL("cannot cancel job ",slurm_id," after ",i," retries : ",_strerror(errno)) ;
	}

	static sigjmp_buf _jmp_env ;
	template<class T> void _load_func( T*& dst , const char* name ) {
		dst = reinterpret_cast<T*>(::dlsym(g_lib_handler,name)) ;
		if (!dst) throw "cannot find "s+name ;
	}
	static void _silent_ignore_sig(int /*sig*/) {
		::siglongjmp( _jmp_env , true/*val*/ ) ;
	}
	static Daemon _sense_daemon(void* conf_) {
		static const ::string SlurmVersion = SLURM_VERSION ;                     // cannot be constexpr with gcc-11
		Trace trace("_sense_daemon",SlurmVersion) ;
		//
		slurm_conf_t* conf = ::launder(reinterpret_cast<slurm_conf_t*>(conf_)) ; // if version mismatch, this may lead to SIGSEGV/SIGBUS handled by caller
		const char*   cv   = conf->version                                     ;
		{	WithSigHandler<_silent_ignore_sig> sav_segv { SIGSEGV } ;            // field offsets vary with version, so we may access a bad pointer
			WithSigHandler<_silent_ignore_sig> sav_bus  { SIGBUS  } ;            // .
			if (::sigsetjmp( _jmp_env , true/*save_sigs*/ )==0) {                // from direct call
				// /!\ no RAII here as setjmp/longjmp will not correctly destroy it
				for( size_t i : iota(SlurmVersion.size()) )
					if(cv[i]!=SlurmVersion[i]) {
						trace("mismach") ;
						goto BadVersion ;
					}
			} else {                                                             // signal caught
				trace("bad_access") ;
			BadVersion :
				throw "bad_version"s ;
			}
		}
		//
		_load_func( _free_job_info_msg                 , "slurm_free_job_info_msg"                 ) ;
		_load_func( _free_submit_response_response_msg , "slurm_free_submit_response_response_msg" ) ;
		_load_func( _init_job_desc_msg                 , "slurm_init_job_desc_msg"                 ) ;
		_load_func( _kill_job                          , "slurm_kill_job"                          ) ;
		_load_func( _list_append                       , "slurm_list_append"                       ) ;
		_load_func( _list_create                       , "slurm_list_create"                       ) ;
		_load_func( _list_destroy                      , "slurm_list_destroy"                      ) ;
		_load_func( _load_job                          , "slurm_load_job"                          ) ;
		_load_func( _strerror                          , "slurm_strerror"                          ) ;
		_load_func( _submit_batch_het_job              , "slurm_submit_batch_het_job"              ) ;
		_load_func( _submit_batch_job                  , "slurm_submit_batch_job"                  ) ;
		//
		spawn_job_func = _spawn_job ;
		job_state_func = _job_state ;
		cancel_func    = _cancel    ;
		//
		Daemon res ;
		res.manage_mem = conf->select_type_param&CR_MEMORY ;
		trace(STR(res.manage_mem)) ;
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
		trace("done",res) ;
		return res ;
	}

	static bool once = ( g_sense_daemon_tab[SLURM_VERSION]=_sense_daemon , true ) ; // register version

}
