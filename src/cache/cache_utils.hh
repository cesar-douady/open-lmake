// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "engine.hh"

struct CompileDigest {
	friend ::string& operator+=( ::string& , CompileDigest const& ) ;
	// cxtors & casts
	CompileDigest() = default ;
	CompileDigest( ::vmap<StrId<CnodeIdx>,DepDigest> const& repo_deps , bool for_download , ::vector<CnodeIdx>* dep_ids=nullptr ) ; // dep_ids are filled for each named dep in repo_deps
	~CompileDigest() ;
	// data
	VarIdx              n_statics = 0 ;
	::vector<Cnode>     deps      ;
	::vector<Hash::Crc> dep_crcs  ;
} ;

inline ::string cache_clean_msg() {
	::string cwd_s_ = Disk::cwd_s() ;
	return cat(
		"cfg=$(cat    "  ,cwd_s_,"LMAKE/config.py)",'\n'
	,	"rm -rf       "  ,cwd_s_,rm_slash          ,'\n'
	,	"mkdir -p     "  ,cwd_s_,"LMAKE"           ,'\n'
	,	"echo \"$cfg\" >",cwd_s_,"LMAKE/config.py"
	) ;
}

bool crc_ok( Hash::Crc cache_crc , Hash::Crc repo_crc ) ;

Disk::DiskSz run_sz( Disk::DiskSz total_z_sz , Disk::DiskSz job_info_sz , CompileDigest const& compile_digest ) ;

float from_rate( CacheConfig const& , Rate  ) ;
Rate  to_rate  ( CacheConfig const& , float ) ;

inline Rate to_rate( CacheConfig const& config , Disk::DiskSz sz , Time::Delay exe_time ) {
	return to_rate( config , sz/float(exe_time) ) ;
}

void rename_run( ::string const& old_name , ::string const& new_name , NfsGuard* nfs_guard=nullptr ) ;
void unlnk_run ( ::string const& name     ,                            NfsGuard* nfs_guard=nullptr ) ;
