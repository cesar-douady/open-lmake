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

	::string& operator+=( ::string& os , CacheRpcReq const& crr ) {
		/**/                  os << "CacheRpcReq("<<crr.proc    ;
		if (+crr.repo_key   ) os << ",K:"<<crr.repo_key         ;
		if (+crr.job        ) os << ','  <<crr.job              ;
		if (+crr.repo_deps  ) os << ",D:"<<crr.repo_deps.size() ;
		if (+crr.conn_id    ) os << ",C:"<<crr.conn_id          ;
		if (+crr.reserved_sz) os << ",S:"<<crr.reserved_sz      ;
		if (+crr.total_z_sz ) os << ",Z:"<<crr.total_z_sz       ;
		if (+crr.job_info_sz) os << ",J:"<<crr.job_info_sz      ;
		if (+crr.exe_time   ) os << ','  <<crr.exe_time         ;
		if (+crr.upload_key ) os << ",U:"<<crr.upload_key       ;
		return                os << ')'                         ;
	}

	::string& operator+=( ::string& os , CacheRpcReply const& crr ) {
		/**/                  os << "CacheRpcReply("<<crr.proc                 ;
		if (+crr.conn_id    ) os << ",C:"<<crr.conn_id                         ;
		if (+crr.hit_info   ) os << ','  <<crr.hit_info                        ;
		if (+crr.key        ) os << ",K:"<<crr.key<<'-'<<"FL"[crr.key_is_last] ;
		if (+crr.dep_ids    ) os << ",D:"<<crr.dep_ids.size()                  ;
		if (+crr.job_id     ) os << ",J:"<<crr.job_id                          ;
		if (+crr.upload_key ) os << ','  <<crr.upload_key                      ;
		if (+crr.msg        ) os << ','  <<crr.msg                             ;
		return                os << ')'                                        ;
	}

}
