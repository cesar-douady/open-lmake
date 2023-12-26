// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#ifdef STRUCT_DECL

namespace Caches {

	struct Cache ;

	using namespace Engine ;

	using Tag = CacheTag ;

}

using Caches::Cache ;

#endif
#ifdef DATA_DEF
namespace Caches {

	struct Cache {
		using Id = ::string ;

		struct Match {
			bool           completed = true  ;             //                            if false <=> answer is delayed and an action will be post to the main loop when ready
			Bool3          hit       = No    ;             // if completed
			::vector<Node> new_deps  = {}    ;             // if completed&&hit==Maybe : deps that were not done and need to be done before answering hit/miss
			Id             id        = {}    ;             // if completed&&hit==Yes   : an id to easily retrieve matched results when calling download
		} ;

		// statics
		static void s_config(::map_s<Config::Cache> const&) ;
		//
		// static data
		static ::map_s<Cache*> s_tab ;

		// services
		// default implementation : no caching, but enforce protocal
		virtual void config(Config::Cache const&) {}
		//
		virtual Match      match   ( Job , Req                                                   ) { return { .completed=true , .hit=No } ; }
		virtual JobDigest  download( Job , Id        const& , JobReason const& , Disk::NfsGuard& ) { FAIL() ;                               } // no download possible since we never match
		virtual bool/*ok*/ upload  ( Job , JobDigest const& ,                    Disk::NfsGuard& ) { return false ;                         }

	} ;

}
#endif
