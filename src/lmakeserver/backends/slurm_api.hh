// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "generic.hh" // /!\ must be first because Python.h must be first

namespace Backends::Slurm {

	using SlurmId = uint32_t ;

	struct Daemon {
		friend ::string& operator+=( ::string& , Daemon const& ) ;
		// data
		Pdate           time_origin { "2023-01-01 00:00:00" } ; // this leaves room til 2091
		float           nice_factor { 1                     } ; // conversion factor in the form of number of nice points per second
		::map_s<size_t> licenses    ;                           // licenses sampled from daemon
		bool            manage_mem  = false/*garbage*/        ;
	} ;

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
		uint16_t cpu       = 0 ; // number of logical cpu  (sbatch    --cpus-per-task option)
		uint32_t mem       = 0 ; // memory   in MB         (sbatch    --mem           option) default : illegal (memory reservation is compulsery)
		uint32_t tmp       = 0 ; // tmp disk in MB         (sbatch    --tmp           option) default : dont manage tmp size (provide infinite storage, reserv none)
		::string excludes  ;     // list of excludes nodes (sbatch -x,--exclude       option)
		::string features  ;     // features/contraint     (sbatch -C,--constraint    option)
		::string gres      ;     // generic resources      (sbatch    --gres          option)
		::string licenses  ;     // licenses               (sbatch -L,--licenses      option)
		::string nodes     ;     // list of required nodes (sbatch -w,--nodelist      option)
		::string partition ;     // partition name         (sbatch -p,--partition     option)
		::string qos       ;     // quality of service     (sbatch -q,--qos           option)
		::string reserv    ;     // reservation            (sbatch -r,--reservation   option)
	} ;

	struct RsrcsData : ::vector<RsrcsDataSingle> {
		using Base = ::vector<RsrcsDataSingle> ;
		// cxtors & casts
		RsrcsData(                               ) = default ;
		RsrcsData( ::vmap_ss&& , Daemon , JobIdx ) ;
		// services
		::vmap_ss mk_vmap() const ;
		RsrcsData round(Backend const&) const {
			RsrcsData res ;
			for( RsrcsDataSingle rds : self ) res.push_back(rds.round()) ;
			return res ;
		}
		size_t hash() const {
			return +Hash::Crc( New , static_cast<::vector<Backends::Slurm::RsrcsDataSingle> const&>(self) ) ;
		}
	} ;

	extern Mutex<MutexLvl::Slurm> slurm_mutex ; // ensure no more than a single outstanding request to daemon
}

namespace Backends::Slurm::SlurmApi {

	using InitFunc        = void (*)( const char*                                    ) ; // this is used before version is known
	using LoadCtlConfFunc = int  (*)( time_t update_time , void** /*out*/ slurm_conf ) ; // slurm_conf_t is not known yet
	using FreeCtlConfFunc = void (*)( void* slurm_conf                               ) ;

	extern void*                                     g_lib_handler      ; // handler for libslurm.so as returned by ::dlsym
	extern ::umap_s<Daemon(*)(void const* /*conf*/)> g_sense_daemon_tab ; // map slurm version to init function, which returns true if version matches else returns false or traps with SIGSEGV/SIGBUS
	//
	extern SlurmId (*spawn_job_func)(
		::stop_token            st
	,	::string         const& key
	,	Job                     job
	,	::vector<ReqIdx> const& reqs
	,	int32_t                 nice
	,	::vector_s       const& cmd_line
	,	const char**            env
	,	RsrcsData        const& rsrcs
	,	bool                    verbose
	) ;
	extern ::pair_s<Bool3/*job_ok*/> (*job_state_func)(SlurmId) ;         // Maybe means job has not completed
	extern void                      (*cancel_func   )(SlurmId) ;

}
