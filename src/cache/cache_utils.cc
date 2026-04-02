// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "engine.hh"
#include "cache_utils.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

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

void rename_run( ::string const& old_name , ::string const& new_name , NfsGuard* nfs_guard ) {
	try {
		rename( old_name+"-data" , new_name+"-data" , {.nfs_guard=nfs_guard} ) ;
		rename( old_name+"-info" , new_name+"-info" , {.nfs_guard=nfs_guard} ) ;
		Trace trace("moved",old_name,new_name);
	} catch(::string const& e) {
		Trace trace("cannot_move",old_name,new_name,e) ;
		exit( Rc::System , "cache error : ",e,"\n  consider : lcache_repair ",mk_shell_str(no_slash(cwd_s())) ) ;
	}
}

void unlnk_run( ::string const& name , NfsGuard* nfs_guard ) {
	try {
		unlnk( name+"-data" , {.nfs_guard=nfs_guard} ) ;
		unlnk( name+"-info" , {.nfs_guard=nfs_guard} ) ;
		Trace trace("unlnked",name);
	} catch(::string const& e) {
		Trace trace("cannot_unlnk",name,e) ;
		exit( Rc::System , "cache error : ",e,"\n  consider : lcache_repair ",mk_shell_str(no_slash(cwd_s())) ) ;
	}
}
