// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "version.hh"

#include "env.hh"

using namespace Disk ;

namespace Codec {

	CodecRemoteSide::CodecRemoteSide(::string const& descr) {
		SWEAR( descr.size()>=6            , descr ) ;
		SWEAR( descr[descr.size()-6]==':' , descr ) ;
		tab = descr.substr(0,descr.size()-6) ;
		switch (descr[descr.size()-5]) {
			case 'N' : file_sync = FileSync::None ; break ;
			case 'D' : file_sync = FileSync::Dir  ; break ;
			case 'S' : file_sync = FileSync::Sync ; break ;
		DF}
		umask = mod_from_str(descr.substr(descr.size()-4)) ;
	}

	CodecRemoteSide::CodecRemoteSide( NewType , ::string const& dir_s ) {
		SWEAR( is_dir_name(dir_s) , dir_s ) ;
		//
		FileStat st ; throw_unless( ::lstat( +dir_s?dir_s.c_str():"." , &st )==0 , "cannot access (",StrErr(),") ",dir_s,rm_slash ) ;
		/**/          throw_unless( S_ISDIR(st.st_mode)           , "not a dir : "                                ,dir_s,rm_slash ) ;
		umask = ~st.st_mode & 0777 ; // ensure permissions on top-level dir are propagated to all underlying dirs and files
		//
		::string init_msg  = cat("echo <val> >",dir_s,AdminDirS,"file_sync # with <val> being one of none, dir or sync") ;
		::string clean_msg = cat("rm -rf ",dir_s,rm_slash," ; mkdir ",dir_s,AdminDirS,rm_slash," ; ",init_msg          ) ;
		chk_version( dir_s , { .chk=Maybe , .key="codec dir" , .init_msg=init_msg , .clean_msg=clean_msg , .umask=umask , .version=Version::Codec } ) ;
		//
		::string fs_file = cat(dir_s,AdminDirS,"file_sync") ;
		::string fs_str  = strip(AcFd(fs_file).read())      ; throw_unless( can_mk_enum<FileSync>(fs_str) , "unrecognized file_sync : ",fs_str ) ;
		//
		tab       = dir_s                     ;
		file_sync = mk_enum<FileSync>(fs_str) ;
	}

	CodecRemoteSide::operator ::string() const {
		::string res = tab ;
		res << ':' ;
		switch (file_sync) {
			case FileSync::None : res << 'N' ; break ;
			case FileSync::Dir  : res << 'D' ; break ;
			case FileSync::Sync : res << 'S' ; break ;
		DF}
		res << mod_to_str(umask) ;
		return res ;
	}

	::string& operator+=( ::string& os , CodecRemoteSide const& crs ) { // START_OF_NO_COV
		/**/                        os << "Codec("<<crs.tab          ;
		if (+crs.file_sync        ) os << ','<<crs.file_sync         ;
		if ( crs.umask!=mode_t(-1)) os << ','<<mod_to_str(crs.umask) ;
		return                      os << ')'                        ;
	}                                                                   // END_OF_NO_COV

}

::string& operator+=( ::string& os , AutodepEnv const& ade ) {                             // START_OF_NO_COV
	/**/                       os << "AutodepEnv("<<static_cast<RealPathEnv const&>(ade) ;
	if (+ade.fast_mail       ) os << ','<<ade.fast_mail                                  ;
	if (+ade.fast_report_pipe) os << ','<<ade.fast_report_pipe                           ;
	/**/                       os << ','<<ade.service                                    ;
	if (+ade.fqdn            ) os << ','<<ade.fqdn                                       ;
	if ( ade.disabled        ) os << ",disabled"                                         ;
	if ( ade.auto_mkdir      ) os << ",auto_mkdir"                                       ;
	if ( ade.mount_chroot_ok ) os << ",mount_chroot_ok"                                  ;
	if ( ade.readdir_ok      ) os << ",readdir_ok"                                       ;
	if (+ade.sub_repo_s      ) os << ','<<ade.sub_repo_s                                 ;
	if (+ade.codecs          ) os << ','<<ade.codecs                                     ;
	if (+ade.views_s         ) os << ','<<ade.views_s                                    ;
	return                     os << ')'                                                 ;
}                                                                                          // END_OF_NO_COV

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (!env) {
		repo_root_s = cwd_s() ;
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
			case 'd' : disabled        = true ; break ;
			case 'D' : readdir_ok      = true ; break ;
			case 'i' : ignore_stat     = true ; break ;
			case 'm' : auto_mkdir      = true ; break ;
			case 'M' : mount_chroot_ok = true ; break ;
			case 'X' : deps_in_system  = true ; break ;
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
	// other stuff
	using CRS = Codec::CodecRemoteSide ; //!                                                                                                          empty_ok
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } fqdn        =              parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } tmp_dir_s   =              parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } repo_root_s =              parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; } { if (env[pos++]!='"') goto Fail ; } sub_repo_s  =              parse_printable<'"'>                 (env,pos       ) ; { if (env[pos++]!='"') goto Fail ; }
	{ if (env[pos++]!=':') goto Fail ; }                                      src_dirs_s  =              parse_printable<::vector_s          >(env,pos,false ) ;
	{ if (env[pos++]!=':') goto Fail ; }                                      codecs      = mk_umap<CRS>(parse_printable<::vmap_ss           >(env,pos,false )) ;
	{ if (env[pos++]!=':') goto Fail ; }                                      views_s     =              parse_printable<::vmap_s<::vector_s>>(env,pos,false ) ;
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
	// START_OF_VERSIONING CACHE REPO JOB
	res << ':' ;
	if (auto_mkdir     ) res << 'm' ;
	if (deps_in_system ) res << 'X' ;
	if (disabled       ) res << 'd' ;
	if (ignore_stat    ) res << 'i' ;
	if (mount_chroot_ok) res << 'M' ;
	if (readdir_ok     ) res << 'D' ;
	switch (file_sync) {
		case FileSync::None : res << "sn" ; break ;
		case FileSync::Dir  : res << "sd" ; break ;
		case FileSync::Sync : res << "ss" ; break ;
	DF} //! NO_COV
	switch (lnk_support) {
		case LnkSupport::None : res << "ln" ; break ;
		case LnkSupport::File : res << "lf" ; break ;
		case LnkSupport::Full : res << "la" ; break ;
	DF} //! NO_COV                                                   empty_ok
	res <<':'<< '"'<<mk_printable<'"'>(                  fqdn               )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(                  tmp_dir_s          )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(                  repo_root_s        )<<'"' ;
	res <<':'<< '"'<<mk_printable<'"'>(                  sub_repo_s         )<<'"' ;
	res <<':'<<      mk_printable     (                  src_dirs_s  ,false )      ;
	res <<':'<<      mk_printable     (mk_vmap<::string>(codecs     ),false )      ;
	res <<':'<<      mk_printable     (                  views_s     ,false )      ;
	// END_OF_VERSIONING
	return res ;
}

void AutodepEnv::chk(bool for_cache) const {
	RealPathEnv::chk(for_cache) ;
	throw_unless( !sub_repo_s || sub_repo_s.back()=='/'                , "bad sub_repo" ) ;
	throw_unless( is_canon(sub_repo_s,false/*ext_ok*/,true/*empy_ok*/) , "bad sub_repo" ) ;
	for( auto const& [view_s,phys_s] : views_s ) {
		/**/                                throw_unless( is_dir_name(view_s) && is_canon(view_s) , "bad view"      ) ;
		for( ::string const& p_s : phys_s ) throw_unless( is_dir_name(p_s   ) && is_canon(p_s   ) , "bad view phys" ) ;
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
		if (!self) return {} ;
		KeyedService s = service ;
		if (mail()==fast_mail) s.addr = 0 ;
		try {
			return ClientSockFd(s) ;
		} catch (::string const&) {
			if (!fqdn) throw ;
			s.addr = SockFd::s_addr(fqdn) ;            // 2nd chance if addr does not work
			return ClientSockFd(s) ;
		}
	} catch (::string const& e) {                      // START_OF_NO_COV
		fail_prod("while trying to report deps :",e) ;
	}                                                  // END_OF_NO_COV
}
