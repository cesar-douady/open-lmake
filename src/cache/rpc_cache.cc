// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "process.hh"

#include "app.hh"

#include "rpc_cache.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

namespace Cache {

	::string& operator+=( ::string& os , CacheRpcReq const& dcrr ) {
		/**/                   os << "CacheRpcReq("<<dcrr.proc    ;
		if (+dcrr.job        ) os << ','  <<dcrr.job              ;
		if (+dcrr.repo_deps  ) os << ",D:"<<dcrr.repo_deps.size() ;
		if (+dcrr.reserved_sz) os << ",S:"<<dcrr.reserved_sz      ;
		if (+dcrr.upload_key ) os << ",K:"<<dcrr.upload_key       ;
		return                 os << ')'                          ;
	}

	::string& operator+=( ::string& os , CacheRpcReply const& dcrr ) {
		/**/                   os << "CacheRpcReply("<<dcrr.proc                  ;
		if (+dcrr.hit_info   ) os << ','  <<dcrr.hit_info                         ;
		if (+dcrr.key        ) os << ",K:"<<dcrr.key<<'-'<<"FL"[dcrr.key_is_last] ;
		if (+dcrr.dep_ids    ) os << ",D:"<<dcrr.dep_ids.size()                   ;
		if (+dcrr.job_id     ) os << ",J:"<<dcrr.job_id                           ;
		if (+dcrr.upload_key ) os << ','<<dcrr.upload_key                         ;
		return                 os << ')'                                          ;
	}

}
