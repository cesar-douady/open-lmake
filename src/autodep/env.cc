// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "env.hh"

using namespace Disk ;

::string& operator+=( ::string& os , AutodepEnv const& ade ) {
	/**/                       os << "AutodepEnv("                             ;
	/**/                       os <<      static_cast<RealPathEnv const&>(ade) ;
	if (+ade.fast_report_pipe) os <<','<< ade.fast_report_pipe                 ;
	/**/                       os <<','<< ade.service                          ;
	if ( ade.auto_mkdir      ) os <<",auto_mkdir"                              ;
	if ( ade.enable          ) os <<",enable"                                  ;
	if (+ade.sub_repo_s      ) os <<','<< ade.sub_repo_s                       ;
	return                     os <<')'                                        ;
}

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (!env) {
		try {
			SearchRootResult srr = search_root_s() ;
			repo_root_s = srr.top_s ;
			sub_repo_s  = srr.sub_s ;
		} catch (::string const&) {
			repo_root_s = cwd_s() ;
		}
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
			case 'd' : enable        = false            ; break ;
			case 'i' : ignore_stat   = true             ; break ;
			case 'm' : auto_mkdir    = true             ; break ;
			case 'n' : lnk_support   = LnkSupport::None ; break ;
			case 'f' : lnk_support   = LnkSupport::File ; break ;
			case 'a' : lnk_support   = LnkSupport::Full ; break ;
			case 'r' : reliable_dirs = true             ; break ;
			default  : goto Fail ;
		}
	// other stuff
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } fast_host        = parse_printable<'"'>                 (env,pos                  ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } fast_report_pipe = parse_printable<'"'>                 (env,pos                  ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } tmp_dir_s        = parse_printable<'"'>                 (env,pos                  ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } repo_root_s      = parse_printable<'"'>                 (env,pos                  ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } sub_repo_s       = parse_printable<'"'>                 (env,pos                  ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; }                                      src_dirs_s       = parse_printable<::vector_s>          (env,pos,false/*empty_ok*/) ;
	{ if (env[pos++]!=':') goto Fail ; }                                      views            = parse_printable<::vmap_s<::vector_s>>(env,pos,false/*empty_ok*/) ;
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
	if (!enable      ) res << 'd' ;
	if (ignore_stat  ) res << 'i' ;
	if (auto_mkdir   ) res << 'm' ;
	if (reliable_dirs) res << 'r' ;
	switch (lnk_support) {
		case LnkSupport::None : res << 'n' ; break ;
		case LnkSupport::File : res << 'f' ; break ;
		case LnkSupport::Full : res << 'a' ; break ;
	DF}
	res <<':'<< '"'<<mk_printable<'"'>(fast_host                   )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(fast_report_pipe            )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(tmp_dir_s                   )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(repo_root_s                 )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(sub_repo_s                  )<<'"' ;
	res <<':'<<      mk_printable     (src_dirs_s,false/*empty_ok*/)      ;
	res <<':'<<      mk_printable     (views     ,false/*empty_ok*/)      ;
	return res ;
}

Fd AutodepEnv::fast_report_fd() const {
	if (!fast_report_pipe) return report_fd() ;                                              // default to plain report if no fast pipe is available
	if (host()!=fast_host) return report_fd() ;                                              // default to plain report if not running on host reading fast_report_pipe
	Fd res ;
	try                       { res = { fast_report_pipe , Fd::Append , true/*no_std*/ } ; } // append if writing to a file
	catch (::string const& e) { fail_prod("while trying to report deps :",e) ;             }
	swear_prod(+res,"cannot append to fast report pipe",fast_report_pipe) ;
	return res ;
}

Fd AutodepEnv::report_fd() const {
	Fd res ;
	try                       { res = ClientSockFd(service).detach() ; res.no_std() ; } // establish connection with server
	catch (::string const& e) { fail_prod("while trying to report deps :",e) ;        }
	swear_prod(+res,"cannot connect to report",service) ;
	return res ;
}

Fd AutodepEnv::repo_root_fd() const {
	Fd res = { repo_root_s , Fd::Dir , true/*no_std*/ } ;      // avoid poluting standard descriptors
	swear_prod(+res,"cannot open repo root dir",repo_root_s) ;
	return res ;
}
