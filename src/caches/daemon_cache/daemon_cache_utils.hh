// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "engine.hh"

struct CompileDigest {
	VarIdx              n_statics = 0 ;
	::vector<Cnode>     deps      ;
	::vector<Hash::Crc> dep_crcs  ;
} ;

CompileDigest compile( ::vmap_s<DepDigest> const& repo_deps , bool for_download ) ;

Disk::DiskSz run_sz( JobInfo const& job_info , ::string job_info_str , CompileDigest const& compile_digest ) ;

Rate rate( DaemonCache::Config const& , Disk::DiskSz , Time::Delay ) ;
