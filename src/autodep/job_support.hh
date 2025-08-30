// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"

#include "record.hh"
#include "rpc_job_exec.hh"

namespace JobSupport {

	::pair<::vector<VerboseInfo>,bool/*ok*/> depend    ( Record const& , ::vector_s&& files , AccessDigest , bool no_follow , bool regexpr=false ) ;
	void                                     target    ( Record const& , ::vector_s&& files , AccessDigest ,                  bool regexpr=false ) ;
	Bool3                                    check_deps( Record const& ,                                                      bool sync   =false ) ;
	::vector_s                               list      ( Record const& , Bool3 write                                                             ) ; // No:deps, Yes:targets, Maybe:both
	::pair_s<                    bool/*ok*/> decode    ( Record const& , ::string&& file , ::string&& code , ::string&& ctx                      ) ;
	::pair_s<                    bool/*ok*/> encode    ( Record const& , ::string&& file , ::string&& val  , ::string&& ctx , uint8_t min_len=1  ) ;

}
