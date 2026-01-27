// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "cache_utils.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

static constexpr Crc::Val CrcOrNone = 1<< NBits<CrcSpecial>    ;
static constexpr Crc::Val CrcErr    = 1<<(NBits<CrcSpecial>+1) ;

::string& operator+=( ::string& os , CompileDigest const& cd ) {
	First first ;
	/**/               os << "CompileDigest("                         ;
	if ( cd.n_statics) os << first("",",")<<"NS:"<<cd.n_statics       ;
	if (+cd.deps     ) os << first("",",")<<"D:" <<cd.deps    .size() ;
	if (+cd.dep_crcs ) os << first("",",")<<"DC:"<<cd.dep_crcs.size() ;
	return             os << ')'                                      ;
}

CompileDigest::~CompileDigest() {
	for( Cnode& d : deps ) d->dec() ;
}

CompileDigest::CompileDigest( ::vmap<StrId<CnodeIdx>,DepDigest> const& repo_deps , bool for_download , ::vector<CnodeIdx>* dep_ids ) {
	struct Dep {
		// services
		bool operator<(Dep const& other) const { return ::pair(bucket,+node) < ::pair(other.bucket,+other.node) ; }
		// data
		int   bucket = 0/*garbage*/ ;                                                                // deps are sorted statics first, then existing, then non-existing
		Cnode node   ;
		Crc   crc    ;
	} ;
	::vector<Dep> deps_ ;
	Trace trace("compile",repo_deps.size(),STR(for_download),STR(bool(dep_ids))) ;
	for( auto& [n,dd] : repo_deps ) {
		bool      is_name = n.is_name()                                              ;
		CnodeIdx* dep_id  = is_name && dep_ids ? &dep_ids->emplace_back(0) : nullptr ;
		Accesses  a       = dd.accesses                                              ;
		if      (!dd.dflags[Dflag::Full] )   a = {} ;                                                // dep is used for resources only, no real accesses
		else if (!for_download           )   SWEAR( !dd.never_match()     , n,dd ) ;                 // meaningless, should not have reached here
		if      (dd.dflags[Dflag::Static]) { SWEAR( n_statics<Max<VarIdx>        ) ; n_statics++ ; }
		else if (!a                      )   continue ;                                              // dep was not accessed, ignore but keep static deps as they must not depend on run
		//
		Cnode node ;
		if (is_name) {
			if ( for_download          ) node    = {       n.name } ;
			else                         node    = { New , n.name } ;
			if ( dep_id                ) *dep_id = +node            ;
			if ( for_download && !node ) continue ;                                                  // if it is not known in cache, it has no impact on matching
		} else {
			node = {n.id} ;
		}
		//
		Crc crc = dd.crc() ;
		if (!for_download)                                                                           // Crc::Unknown means any existing file
			switch (+(a&Accesses(Access::Lnk,Access::Reg,Access::Stat))) {
				case +Accesses(                        ) :                      crc = +Crc::Unknown|CrcOrNone ; break ;
				case +Accesses(Access::Lnk             ) : if (!crc.is_lnk()  ) crc = +Crc::Reg    |CrcOrNone ; break ;
				case +Accesses(Access::Reg             ) : if (!crc.is_reg()  ) crc = +Crc::Lnk    |CrcOrNone ; break ;
				case +Accesses(            Access::Stat) : if ( crc!=Crc::None) crc =  Crc::Unknown           ; break ;
				case +Accesses(Access::Lnk,Access::Stat) : if ( crc.is_reg()  ) crc =  Crc::Reg               ; break ;
				case +Accesses(Access::Reg,Access::Stat) : if ( crc.is_lnk()  ) crc =  Crc::Lnk               ; break ;
			DN}
		// we lose 1 bit of crc but we must manage errors and it does not desserv an additional field
		if ( dd.err && a[Access::Err] ) crc = +crc |  CrcErr ;                                       // if not sensed, ignore error status (lmake would stop if not ignored anyway)
		else                            crc = +crc & ~CrcErr ;
		//
		deps_.push_back({
			.bucket = dd.dflags[Dflag::Static] ? 0 : crc!=Crc::None ? 1 : 2
		,	.node   = node
		,	.crc    = crc
		}) ;
	}
	::sort(deps_) ;
	for( Dep      & dep : deps_ ) { dep.node->inc() ;          deps    .push_back(dep.node) ; }
	for( Dep const& dep : deps_ ) { if (dep.bucket==2) break ; dep_crcs.push_back(dep.crc ) ; }
	trace("done",dep_ids?dep_ids->size():size_t(0)) ;
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

Disk::DiskSz run_sz( Disk::DiskSz total_z_sz , Disk::DiskSz job_info_sz , CompileDigest const& compile_digest ) {
	return
		total_z_sz
	+	job_info_sz
	+	sizeof(CrunData)
	+	compile_digest.deps    .size()*sizeof(CnodeIdx)
	+	compile_digest.dep_crcs.size()*sizeof(Crc     )
	;
}

float from_rate( CacheConfig const& config , Rate rate ) {
	return config.max_rate * ::expf(-::ldexpf(rate,-4)) ;
}

Rate to_rate( CacheConfig const& config , float rate ) {
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

