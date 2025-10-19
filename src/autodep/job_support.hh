// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"
#include "time.hh"

#include "record.hh"
#include "rpc_job_exec.hh"

namespace JobSupport {

	::pair<::vector<VerboseInfo>,bool/*ok*/> depend     ( ::vector_s&& files , AccessDigest , bool no_follow , bool regexpr=false , bool direct=false ) ;
	void                                     target     ( ::vector_s&& files , AccessDigest , bool no_follow , bool regexpr=false                     ) ;
	Bool3                                    chk_deps   ( Time::Delay , bool sync=false                                                               ) ; // date is used for delayed action
	::vector_s                               list       ( Bool3 write , ::optional_s&& dir , ::optional_s&& regexpr                                   ) ; // No:deps, Yes:targets, Maybe:both
	::string                                 list_root_s(               ::string    && dir                                                            ) ;
	::string                                 decode     ( ::string&& file , ::string&& ctx , ::string&& code                                          ) ;
	::string                                 encode     ( ::string&& file , ::string&& ctx , ::string&& val  , uint8_t min_len=1                      ) ;

}
