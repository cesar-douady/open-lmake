// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "env.hh"

using namespace Disk ;

::ostream& operator<<( ::ostream& os , AutodepEnv const& ade ) {
	os << "AutodepEnv(" ;
	/**/                 os <<      static_cast<RealPathEnv const&>(ade) ;
	/**/                 os <<','<< ade.service                          ;
	if (ade.auto_mkdir ) os <<",auto_mkdir"                              ;
	if (ade.ignore_stat) os <<",ignore_stat"                             ;
	if (ade.disabled   ) os <<",disabled"                                ;
	return os <<')' ;
}

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (!env) {
		try                     { root_dir_s = search_root_dir_s().first ; }
		catch (::string const&) { root_dir_s = cwd_s()                   ; }
		return ;
	}
	size_t pos = env.find(':'           ) ; if (pos==Npos) goto Fail ;
	/**/   pos = env.find(':',pos+1/*:*/) ; if (pos==Npos) goto Fail ;
	// service
	service = env.substr(0,pos) ;
	pos++/*:*/ ;
	// options
	for( ; env[pos]!=':' ; pos++ )
		switch (env[pos]) {
			case 'd' : disabled      = true             ; break ;
			case 'i' : ignore_stat   = true             ; break ;
			case 'm' : auto_mkdir    = true             ; break ;
			case 'n' : lnk_support   = LnkSupport::None ; break ;
			case 'f' : lnk_support   = LnkSupport::File ; break ;
			case 'a' : lnk_support   = LnkSupport::Full ; break ;
			case 'r' : reliable_dirs = true             ; break ;
			default  : goto Fail ;
		}
	// other dirs
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } tmp_dir_s  = parse_printable<'"'>                 (env,pos) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } root_dir_s = parse_printable<'"'>                 (env,pos) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; }                                      src_dirs_s = parse_printable<::vector_s>          (env,pos) ;
	{ if (env[pos++]!=':') goto Fail ; }                                      views      = parse_printable<::vmap_s<::vector_s>>(env,pos) ;
	{ if (env[pos  ]!=0  ) goto Fail ; }
	for( ::string const& src_dir_s : src_dirs_s ) if (!is_dirname(src_dir_s)) goto Fail ;
	return ;
Fail :
	fail_prod("bad autodep env format at pos",pos,":",env) ;
}

AutodepEnv::operator ::string() const {
	// service
	::string res = service ;
	// options
	res << ':' ;
	if (disabled     ) res << 'd' ;
	if (ignore_stat  ) res << 'i' ;
	if (auto_mkdir   ) res << 'm' ;
	if (reliable_dirs) res << 'r' ;
	switch (lnk_support) {
		case LnkSupport::None : res << 'n' ; break ;
		case LnkSupport::File : res << 'f' ; break ;
		case LnkSupport::Full : res << 'a' ; break ;
	DF}
	res <<':'<< '"'<<mk_printable<'"'>(tmp_dir_s )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(root_dir_s)<<'"' ;
	res <<':'<<      mk_printable     (src_dirs_s)      ;
	res <<':'<<      mk_printable     (views     )      ;
	return res ;
}
