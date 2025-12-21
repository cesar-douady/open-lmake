// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "daemon_cache_utils.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

CompileDigest compile( ::vmap_s<DepDigest> const& repo_deps , bool for_download ) {
	struct Dep {
		// services
		bool operator<(Dep const& other) const { return ::pair(bucket,+node) < ::pair(other.bucket,+other.node) ; }
		// data
		int   bucket = 0/*garbage*/ ;                                    // deps are sorted statics first, then existing, then non-existing
		Cnode node   ;
		Crc   crc    ;
	} ;
	CompileDigest res  ;
	::vector<Dep> deps ;
	for( auto const& [n,dd] : repo_deps ) {
		Cnode node ;
		if (for_download) { node = {       n } ; if (!node) continue ; } // if it is not known in cache, it has no impact on matching
		else                node = { New , n } ;
		if (dd.dflags[Dflag::Static]) { SWEAR( res.n_statics<Max<VarIdx> ) ; res.n_statics++ ; }
		Crc crc = dd.crc() ;
		deps.push_back({
			.bucket = dd.dflags[Dflag::Static] ? 0 : crc!=Crc::None ? 1 : 2
		,	.node   = node
		,	.crc    = crc
		}) ;
	}
	::sort(deps) ;
	for( Dep const& dep : deps )                              res.deps    .push_back(dep.node) ;
	for( Dep const& dep : deps ) { if (dep.bucket==2) break ; res.dep_crcs.push_back(dep.crc ) ; }
	return res ;
}

Rate rate( Disk::DiskSz sz , Time::Delay exe_time ) {
	float r = ::ldexpf(
		::logf( 1e9 * float(exe_time) / sz ) // fastest throughput is 1GB/s (beyond that, cache is obviously needless)
	,	4
	) ;
	if (r<0      ) r = 0        ;
	if (r>=NRates) r = NRates-1 ;
	Trace trace("_run_rate",Rate(r)) ;
	return r ;
}

