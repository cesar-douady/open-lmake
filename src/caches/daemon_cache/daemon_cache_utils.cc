// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "daemon_cache_utils.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

static constexpr Crc::Val CrcOrNone = 1<< NBits<CrcSpecial>    ;
static constexpr Crc::Val CrcErr    = 1<<(NBits<CrcSpecial>+1) ;

CompileDigest compile( ::vmap_s<DepDigest> const& repo_deps , bool for_download ) {
	struct Dep {
		// services
		bool operator<(Dep const& other) const { return ::pair(bucket,+node) < ::pair(other.bucket,+other.node) ; }
		// data
		int   bucket = 0/*garbage*/ ;                                                                 // deps are sorted statics first, then existing, then non-existing
		Cnode node   ;
		Crc   crc    ;
	} ;
	CompileDigest res  ;
	::vector<Dep> deps ;
	for( auto const& [n,dd] : repo_deps ) {
		Accesses a = dd.accesses ;
		if      (!dd.dflags[Dflag::Full] )   a = {} ;                                                 // dep is used for resources only, no real accesses
		else if (!for_download           )   SWEAR( !dd.never_match() , n,dd ) ;                      // meaningless, should not have reached here
		if      (dd.dflags[Dflag::Static]) { SWEAR( res.n_statics<Max<VarIdx> ) ; res.n_statics++ ; }
		else if (!a                      )   continue ;                                               // dep was not accessed, ignore but keep static deps as they must not depend on run
		//
		Cnode node ;
		if (for_download) { node = {       n } ; if (!node) continue ; }                              // if it is not known in cache, it has no impact on matching
		else                node = { New , n } ;
		//
		Crc crc = dd.crc() ;
		if (!for_download)                                                                            // Crc::Unknown means any existing file
			switch (+(a&Accesses(Access::Lnk,Access::Reg,Access::Stat))) {
				case +Accesses(                        ) :                      crc = +Crc::Unknown|CrcOrNone ; break ;
				case +Accesses(Access::Lnk             ) : if (!crc.is_lnk()  ) crc = +Crc::Reg    |CrcOrNone ; break ;
				case +Accesses(Access::Reg             ) : if (!crc.is_reg()  ) crc = +Crc::Lnk    |CrcOrNone ; break ;
				case +Accesses(            Access::Stat) : if ( crc!=Crc::None) crc =  Crc::Unknown           ; break ;
				case +Accesses(Access::Lnk,Access::Stat) : if ( crc.is_reg()  ) crc =  Crc::Reg               ; break ;
				case +Accesses(Access::Reg,Access::Stat) : if ( crc.is_lnk()  ) crc =  Crc::Lnk               ; break ;
			DN}
		// we lose 1 bit of crc but we must manage errors and it does not desserv an additional field
		if (dd.err) { SWEAR(a[Access::Err]) ; crc = +crc |  CrcErr ; }                                // if error are not sensed, lmake engine would stop if dep is in error
		else                                  crc = +crc & ~CrcErr ;
		//
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

// check if cache_crc and repo_crc are compatible
// cache_crc has undergone generalisation above, repo_crc may have undergone it (in case of commit, we first match to avoid double entries)
bool crc_ok( Crc cache_crc , Crc repo_crc ) {
	Crc cc = +cache_crc & ~(CrcOrNone|CrcErr) ;
	if ( cc.valid()                                ) return repo_crc==cache_crc ;                      // common case, other ones are exceptional
	if ( (+cache_crc&CrcErr) != (+repo_crc&CrcErr) ) return false               ;
	//
	Crc rc = +repo_crc & ~(CrcOrNone|CrcErr) ;
	if (rc.valid()) {                                                                                  // check if repo_crc is compatible with cache_crc
		if ( (+cache_crc&CrcOrNone) && repo_crc==Crc::None   ) return true                ;
		if ( cc==Crc::Unknown                                ) return repo_crc!=Crc::None ;
		if ( cc==Crc::Lnk                                    ) return repo_crc.is_lnk()   ;
		if ( cc==Crc::Reg                                    ) return repo_crc.is_reg()   ;
	} else {                                                                                           // check if repo_crc and cache_crc have a compatible content
		if ( (+cache_crc&CrcOrNone) && (+repo_crc&CrcOrNone) ) return true                           ; // None is a solution
		if ( cc==Crc::Unknown                                ) return true                           ; // any Target is a solution
		if ( cc==Crc::Lnk                                    ) return rc==Crc::Lnk||rc==Crc::Unknown ; // any Lnk is a solution
		if ( cc==Crc::Reg                                    ) return rc==Crc::Reg||rc==Crc::Unknown ; // any Reg is a solution
	}
	FAIL(cache_crc,repo_crc) ;
}

Disk::DiskSz run_sz( JobInfo const& job_info , ::string job_info_str , CompileDigest const& compile_digest ) {
	return
		job_info.end.total_z_sz
	+	job_info_str.size()
	+	sizeof(CrunData)
	+	compile_digest.deps    .size()*sizeof(CnodeIdx)
	+	compile_digest.dep_crcs.size()*sizeof(Crc     )
	;
}

float from_rate( DaemonCache::Config const& config , Rate rate ) {
	return config.max_rate * ::expf(-::ldexpf(rate,-4)) ;
}

Rate to_rate( DaemonCache::Config const& config , float rate ) {
	float r = ::ldexpf(
		::logf( config.max_rate / rate )
	,	4
	) ;
	if      (r< 0     ) r = 0        ;
	else if (r>=NRates) r = NRates-1 ;
	Rate res = r ;
	Trace trace("rate",rate,res) ;
	return res ;
}

