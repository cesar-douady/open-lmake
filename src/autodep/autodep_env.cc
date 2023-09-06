// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "autodep_env.hh"

::ostream& operator<<( ::ostream& os , AutodepEnv const& ade ) {
	/**/                         os << "AutodepEnv(" << ade.service <<','<< ade.root_dir <<',' ;
	if ( ade.auto_mkdir        ) os <<",auto_mkdir"                                            ;
	if ( ade.ignore_stat       ) os <<",ignore_stat"                                           ;
	if (!ade.src_dirs_s.empty()) os <<','<< ade.src_dirs_s                                     ;
	return                       os <<','<< ade.lnk_support <<')'                              ;
}

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (env.empty()) return ;
	{	size_t pos1 = env.find(':'       ) ; if (pos1==Npos) goto Fail ;
		/**/   pos1 = env.find(':',pos1+1) ; if (pos1==Npos) goto Fail ;
		size_t pos2 = env.find(':',pos1+1) ; if (pos2==Npos) goto Fail ;
		// service
		service = env.substr(0,pos1) ;
		// options
		for( size_t i=pos1+1 ; i<pos2 ; i++ )
			switch (env[i]) {
				case 'd' : auto_mkdir  = true             ; break ;
				case 'i' : ignore_stat = true             ; break ;
				case 'n' : lnk_support = LnkSupport::None ; break ;
				case 'f' : lnk_support = LnkSupport::File ; break ;
				case 'a' : lnk_support = LnkSupport::Full ; break ;
				default : goto Fail ;
			}
		//source dirs
		size_t pos3 = pos2+1 ;                                                 // compute pos3 during source dirs analysis
		for ( bool first=true ; env[pos3]!=':' ; first=false ) {
			if ( !first && env[pos3++]!=',' ) goto Fail ;
			size_t sz = parse_c_str(env,pos3) ;
			if (sz==Npos) goto Fail ;
			src_dirs_s.push_back(env.substr(pos3+1,sz-2)) ;                    // account for quotes
			SWEAR(src_dirs_s.back().back()=='/') ;
			pos3 += sz ;
		}
		// root dir
		root_dir = env.substr(pos3+1) ;
		return ;
	}
Fail :
	fail_prod( "bad autodep env format : " , env ) ;
}

AutodepEnv::operator ::string() const {
	::string res ; res.reserve(24+root_dir.size()) ;
	// service
	res += service ;
	// options
	res += ':' ;
	if (auto_mkdir ) res += 'd' ;
	if (ignore_stat) res += 'i' ;
	switch (lnk_support) {
		case LnkSupport::None : res += 'n' ; break ;
		case LnkSupport::File : res += 'f' ; break ;
		case LnkSupport::Full : res += 'a' ; break ;
		default : FAIL(lnk_support) ;
	}
	// source dirs
	res += ':' ;
	bool first = true ;
	for( ::string const& sd_s : src_dirs_s ) {
		SWEAR(sd_s.back()=='/') ;
		if (first) first  = false ;
		else       res   += ','   ;
		res += mk_c_str(sd_s) ;
	}
	// root dir
	res += ':'      ;
	res += root_dir ;
	//
	return res ;
}
