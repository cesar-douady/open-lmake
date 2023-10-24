// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <grp.h>

#include "core.hh"

#include "caches/dir_cache.hh"                                                 // PER_CACHE : add include line for each cache method

namespace Caches {

	//
	// Cache
	//

	::map_s<Cache*> Cache::s_tab ;

	void Cache::s_config(::map_s<Config::Cache> const& configs) {
		for( auto const& [key,config] : configs ) {
			Cache* cache = nullptr/*garbage*/ ;
			switch (config.tag) {
				case Tag::None : cache = new Cache    ; break ;                // base class Cache actually caches nothing
				case Tag::Dir  : cache = new DirCache ; break ;                // PER_CACHE : add a case for each cache method
				default : FAIL(config.tag) ;
			}
			cache->config(config) ;
			s_tab.emplace(key,cache) ;
		}
	}

}
