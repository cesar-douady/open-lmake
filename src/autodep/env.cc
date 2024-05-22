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
		try                     { root_dir = search_root_dir().first ; }
		catch (::string const&) { root_dir = cwd()                   ; }
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
	{ if (env[pos++]!=':') goto Fail ; if (env[pos++]!='"') goto Fail ; tie(tmp_dir ,pos) = parse_printable<'"'>(env,pos) ; if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; if (env[pos++]!='"') goto Fail ; tie(root_dir,pos) = parse_printable<'"'>(env,pos) ; if (env[pos++]!='"') goto Fail ; }
	//source dirs
	if (env[pos++]!=':') goto Fail ;
	if (env[pos++]!='(') goto Fail ;
	for ( bool first=true ; env[pos]!=')' ; first=false ) {
		if (!first && env[pos++]!=',') goto Fail ;
		::string src_dir_s ;
		if (env[pos++]!='"') goto Fail ;
		tie(src_dir_s,pos) = parse_printable<'"'>(env,pos) ;
		if (env[pos++]!='"') goto Fail ;
		if (src_dir_s.back()!='/') goto Fail ;
		src_dirs_s.push_back(::move(src_dir_s)) ;
	}
	if (env[pos++]!=')') goto Fail ;
	// views
	if (env[pos++]!=':') goto Fail ;
	if (env[pos++]!='{') goto Fail ;
	for ( bool first1=true ; env[pos]!='}' ; first1=false ) {
		if (!first1 && env[pos++]!=',') goto Fail ;
		//
		::string view ;
		if (env[pos++]!='"') goto Fail ;
		tie(view,pos) = parse_printable<'"'>(env,pos) ;
		if (env[pos++]!='"') goto Fail ;
		//
		if (env[pos++]!=':') goto Fail ;
		//
		::vmap_s<FileLoc> phys ;
		if (env[pos++]!='(') goto Fail ;
		for ( bool first2=true ; env[pos]!=')' ; first2=false ) {
			if (!first2 && env[pos++]!=',') goto Fail ;
			size_t col = env.find(':',pos) ;
			if (col==Npos) goto Fail ;
			FileLoc fl ; try { fl = mk_enum<FileLoc>(env.substr(pos,col-pos)) ; } catch (::string const&) { goto Fail ; }
			pos = col+1 ;
			::string phy ;
			if (env[pos++]!='"') goto Fail ;
			tie(phy,pos) = parse_printable<'"'>(env,pos) ;
			if (env[pos++]!='"') goto Fail ;
			phys.emplace_back(::move(phy),fl) ;
		}
		if (env[pos++]!=')') goto Fail ;
		views.emplace_back(view,phys) ;
	}
	if (env[pos++]!='}') goto Fail ;
	//
	if (env[pos]!=0) goto Fail ;
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
	// dirs
	res <<':'<< '"'<<mk_printable<'"'>(tmp_dir )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(root_dir)<<'"' ;
	// source dirs
	res << ':' ;
	bool first = true ;
	res << '(' ;
	for( ::string const& sd_s : src_dirs_s ) {
		SWEAR( sd_s.back()=='/' , sd_s ) ;
		if (!first) res <<',' ; else first = false ;
		res << '"'<<mk_printable<'"'>(sd_s)<<'"' ;
	}
	res << ')' ;
	// views
	res << ':' ;
	bool first1 = true ;
	res << '{' ;
	for( auto const& [view,phys] : views ) {
		if (!first1) res << ',' ; else first1 = false ;
		res << '"'<<mk_printable<'"'>(view)<<'"' ;
		res << ':'                               ;
		bool first2 = true ;
		res << '(' ;
		for( ::pair_s<FileLoc> const& phy : phys ) {
			if (!first2) res <<',' ; else first2 = false ;
			append_to_string( res , phy.second ,':', '"',mk_printable<'"'>(phy.first),'"' ) ;
		}
		res << ')' ;
	}
	res << '}' ;
	//
	return res ;
}
