// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "env.hh"

using namespace Disk ;

::string& operator+=( ::string& os , AutodepEnv const& ade ) {                   // START_OF_NO_COV
	/**/                       os << "AutodepEnv("                             ;
	/**/                       os <<      static_cast<RealPathEnv const&>(ade) ;
	if (+ade.fast_mail       ) os <<','<< ade.fast_mail                        ;
	if (+ade.fast_report_pipe) os <<','<< ade.fast_report_pipe                 ;
	/**/                       os <<','<< ade.service                          ;
	if ( ade.enable          ) os <<",enable"                                  ;
	if ( ade.auto_mkdir      ) os <<",auto_mkdir"                              ;
	if ( ade.readdir_ok      ) os <<",readdir_ok"                              ;
	if (+ade.sub_repo_s      ) os <<','<< ade.sub_repo_s                       ;
	if (+ade.views           ) os <<','<< ade.views                            ;
	return                     os <<')'                                        ;
}                                                                                // END_OF_NO_COV

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (!env) {
		try {
			SearchRootResult srr = search_root() ;
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
	// fast report
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } fast_mail        = parse_printable<'"'>(env,pos) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } fast_report_pipe = parse_printable<'"'>(env,pos) ; { if (env[pos++]!='"') goto Fail ; }
	// options
	if (env[pos++]!=':') goto Fail ;
	for( ; env[pos]!=':' ; pos++ )
		switch (env[pos]) {
			case 'd' : enable        = false            ; break ;
			case 'D' : readdir_ok    = true             ; break ;
			case 'i' : ignore_stat   = true             ; break ;
			case 'm' : auto_mkdir    = true             ; break ;
			case 'l' :
				pos++ ;
				switch (env[pos]) {
					case 'n' : lnk_support = LnkSupport::None ; break ;
					case 'f' : lnk_support = LnkSupport::File ; break ;
					case 'a' : lnk_support = LnkSupport::Full ; break ;
					default  : goto Fail ;
				}
			break ;
			case 's' :
				pos++ ;
				switch (env[pos]) {
					case 'n' : file_sync = FileSync::None ; break ;
					case 'd' : file_sync = FileSync::Dir  ; break ;
					case 's' : file_sync = FileSync::Sync ; break ;
					default  : goto Fail ;
				}
			break ;
			default  : goto Fail ;
		}
	// other stuff                                                                                                                       empty_ok
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } tmp_dir_s   = parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } repo_root_s = parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } sub_repo_s  = parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; }                                      src_dirs_s  = parse_printable<::vector_s>          (env,pos,false ) ;
	{ if (env[pos++]!=':') goto Fail ; }                                      views       = parse_printable<::vmap_s<::vector_s>>(env,pos,false ) ;
	{ if (env[pos  ]!=0  ) goto Fail ; }
	for( ::string const& src_dir_s : src_dirs_s ) if (!is_dir_name(src_dir_s)) goto Fail ;
	return ;
Fail :
	fail_prod("bad autodep env format at pos",pos,":",env) ; // NO_COV
}

AutodepEnv::operator ::string() const {
	// service
	::string res = +service ? ::string(service) : ":"s ;
	// fast report
	res <<':'<< '"'<<mk_printable<'"'>(fast_mail       )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(fast_report_pipe)<<'"' ;
	// options
	res << ':' ;
	if (!enable    ) res << 'd' ;
	if (readdir_ok ) res << 'D' ;
	if (ignore_stat) res << 'i' ;
	if (auto_mkdir ) res << 'm' ;
	switch (lnk_support) {
		case LnkSupport::None : res << "ln" ; break ;
		case LnkSupport::File : res << "lf" ; break ;
		case LnkSupport::Full : res << "la" ; break ;
	DF} //! NO_COV
	switch (file_sync) {
		case FileSync::None : res << "sn" ; break ;
		case FileSync::Dir  : res << "sd" ; break ;
		case FileSync::Sync : res << "ss" ; break ;
	DF} //! NO_COV                                empty_ok
	res <<':'<< '"'<<mk_printable<'"'>(tmp_dir_s         )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(repo_root_s       )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(sub_repo_s        )<<'"' ;
	res <<':'<<      mk_printable     (src_dirs_s ,false )      ;
	res <<':'<<      mk_printable     (views      ,false )      ;
	return res ;
}

void AutodepEnv::chk(bool for_cache) const {
	RealPathEnv::chk(for_cache) ;
	throw_unless( !sub_repo_s || sub_repo_s.back()=='/'                , "bad sub_repo" ) ;
	throw_unless( is_canon(sub_repo_s,false/*ext_ok*/,true/*empy_ok*/) , "bad sub_repo" ) ;
	for( auto const& [view,phys] : views ) {
		/**/                            throw_unless( is_canon(view) , "bad view" ) ;
		for( ::string const& p : phys ) throw_unless( is_canon(p) , "bad view phys" ) ;
	}
	if (for_cache) {
		throw_unless( !fast_report_pipe , "bad report info" ) ;
	}
}

Fd AutodepEnv::repo_root_fd() const {
	try                       { return { repo_root_s , {.flags=O_RDONLY|O_DIRECTORY,.no_std=true} } ; } // avoid poluting standard descriptors
	catch (::string const& e) { fail_prod("cannot open repo root dir",repo_root_s) ;                  }
}

bool AutodepEnv::can_fast_report() const {
	return +fast_report_pipe && mail()==fast_mail ;
}

AcFd AutodepEnv::fast_report_fd() const {
	SWEAR( can_fast_report() , "cannot fast report" , self ) ;                             // else must use slow report
	try {
		if (+self) return { fast_report_pipe , {.flags=O_WRONLY|O_APPEND,.no_std=true} } ; // append if writing to a file
		else       return {                                                            } ;
	} catch (::string const& e) {                                                          // START_OF_NO_COV
		fail_prod("while trying to report deps :",e) ;
	}                                                                                      // END_OF_NO_COV
}

ClientSockFd AutodepEnv::slow_report_fd() const {
	try {
		if (+self) { SockFd::Service s = service ; if (host()==fast_mail) s.addr = 0 ; return ClientSockFd(s) ; }
		else                                                                           return {}              ;
	} catch (::string const& e) {                                                                               // START_OF_NO_COV
		fail_prod("while trying to report deps :",e) ;
	}                                                                                                           // END_OF_NO_COV
}
