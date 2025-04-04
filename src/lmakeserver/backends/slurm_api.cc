// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "slurm.h"

namespace Backends::Slurm::SlurmApi {

	static_assert( is_same_v<decltype(slurm_init),InitFunc> ) ; // init is performed before before we have had a chance to #include slurm.h, it'd better have the right type

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

	template<class T> void _load_func( void* handler , T*& dst , const char* name ) {
		dst = reinterpret_cast<T*>(::dlsym(handler,name)) ;
		if (!dst) throw "cannot find "s+name ;
	}
	static void _exit1() { ::_exit(1) ; }
	Daemon slurm_sense_daemon( ::string const& lib_slurm , ::string const& config_file ) {
		Trace trace(BeChnl,"slurm_sense_daemon") ;
		//
		void* handler = ::dlopen(lib_slurm.c_str(),RTLD_NOW|RTLD_GLOBAL) ;
		if (!handler) throw "cannot find "s+lib_slurm ;
		try {
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
		} catch (::string const& e) {
			throw e+" in "+lib_slurm ;
		}
		// /!\ stupid SlurmApi::init function calls exit(1) in case of error !
		// so the idea here is to fork a process to probe SlurmApi::init
		const char* cf = +config_file ? config_file.c_str() : nullptr ;
		if ( pid_t child_pid=::fork() ; !child_pid ) {
			// in child
			::atexit(_exit1) ;                                                // we are unable to call the exit handlers from here, so we add an additional one which exits immediately
			Fd dev_null_fd { "/dev/null" , Fd::Write } ;                      // this is just a probe, we want nothing on stderr
			::dup2(dev_null_fd,2) ;                                           // so redirect to /dev/null
			SlurmApi::init(cf) ;                                              // in case of error, SlurmApi::init calls exit(1), which in turn calls _exit1 as the first handler (last registered)
			::_exit(0) ;                                                      // if we are here, everything went smoothly
		} else {
			// in parent
			int wstatus ;
			pid_t rc = ::waitpid(child_pid,&wstatus,0) ;                      // gather status to know if we were able to call SlurmApi::init
			if ( rc<=0 || !wstatus_ok(wstatus) ) throw "cannot init slurm"s ; // no, report error
		}
		SlurmApi::init(cf) ;                                                  // this should be safe now that we have checked it works in a child
		//
		slurm_conf_t* conf = nullptr ;
		// XXX? : remember last conf read so as to pass a real update_time param & optimize call (maybe not worthwhile)
		{	Lock lock { _slurm_mutex } ;
			::string cf = config_file|"/etc/slurm/slurm.conf"s ;
			if (!is_target(cf)                                                ) throw "no slurm config file "+cf                 ;
			if (SlurmApi::load_ctl_conf(0/*update_time*/,&conf)!=SLURM_SUCCESS) throw "cannot reach slurm daemon : "+slurm_err() ;
			trace("version",conf->version) ;
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
		trace("done",res) ;
		return res ;
	}

}
