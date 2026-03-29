// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_cache.hh"

namespace Cache {

	void CacheRpcReq::operator>>(::string& os) const {    // START_OF_NO_COV
		/**/              os << "CacheRpcReq("<<proc    ;
		if (+repo_key   ) os << ",K:"<<repo_key         ;
		if (+job        ) os << ','  <<job              ;
		if (+repo_deps  ) os << ",D:"<<repo_deps.size() ;
		if (+conn_id    ) os << ",C:"<<conn_id          ;
		if (+reserved_sz) os << ",S:"<<reserved_sz      ;
		if (+total_z_sz ) os << ",Z:"<<total_z_sz       ;
		if (+job_info_sz) os << ",J:"<<job_info_sz      ;
		if (+exe_time   ) os << ','  <<exe_time         ;
		if (+upload_key ) os << ",U:"<<upload_key       ;
		/**/              os << ')'                     ;
	}                                                     // END_OF_NO_COV

	void CacheRpcReply::operator>>(::string& os) const {             // START_OF_NO_COV
		/**/              os << "CacheRpcReply("<<proc             ;
		if (+conn_id    ) os << ",C:"<<conn_id                     ;
		if (+fqdn       ) os << ','  <<fqdn                        ;
		if (+hit_info   ) os << ','  <<hit_info                    ;
		if (+key        ) os << ",K:"<<key<<'-'<<"FL"[key_is_last] ;
		if (+dep_ids    ) os << ",D:"<<dep_ids.size()              ;
		if (+job_id     ) os << ",J:"<<job_id                      ;
		if (+upload_key ) os << ','  <<upload_key                  ;
		if (+msg        ) os << ','  <<msg                         ;
		/**/              os << ')'                                ;
	}                                                                // END_OF_NO_COV

}
