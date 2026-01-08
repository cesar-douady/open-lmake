// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 5 times : with STRUCT_DECL defined, then with STRUCT_DEF defined, then with INFO_DEF defined, then with DATA_DEF defined, then with IMPL defined

#include "rpc_job.hh"

#ifdef DATA_DEF

namespace Cache {

	struct CacheServerSide : CacheRemoteSide {
		struct DownloadDigest {
			CacheHitInfo    hit_info         = {} ;
			Engine::JobInfo job_info         = {} ; // if hit_info< Miss
			::string        file_actions_msg = {} ; // if hit_info==Hit
		} ;
		// statics
		static void s_config( ::vmap_s<::vmap_ss> const& caches ) ;
		// static data
		static ::vector<CacheServerSide> s_tab ;
		// cxtors & casts
		CacheServerSide() = default ;
		CacheServerSide(::vmap_ss const&) ;
		// services
		template<IsStream S> void _serdes(S& s) {
			::serdes( s , static_cast<CacheRemoteSide&>(self) ) ;
			::serdes( s , repo_key                            ) ;
		}
		::vmap_ss descr() const ;
		DownloadDigest download( Engine::Job , Engine::Rule::RuleMatch const& , bool incremental ) ;
		void commit            ( Engine::Job , CacheUploadKey                                    ) ;
		// data
		::string repo_key ;
	private :
		ClientSockFd _fd     ;
		IMsgBuf      _imsg   ;
		AcFd         _dir_fd ;
	} ;


}

#endif
