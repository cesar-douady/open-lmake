// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <exception> // ::set_terminate

#include "disk.hh"
#include "process.hh"
#include "trace.hh"

#include "app.hh"

using namespace Disk ;
using namespace Time ;

StaticUniqPtr<::string> g_lmake_root_s  ;
StaticUniqPtr<::string> g_repo_root_s   ;
StaticUniqPtr<::string> g_startup_dir_s ; // relative to g_repo_root_s , dir from which command was launched
StaticUniqPtr<::string> g_exe_name      ;

void crash_handler( int sig , void* addr ) {
	if (sig==SIGABRT) crash(4,sig,"aborted"                         ) ;
	else              crash(2,sig,::strsignal(sig),"at address",addr) ;
}

static void _terminate() {
	try                          { ::rethrow_exception(::current_exception()) ;       }
	catch (::string    const& e) { crash(4,SIGABRT,"uncaught exception :",e       ) ; }
	catch (int         const& e) { crash(4,SIGABRT,"uncaught exception :",e       ) ; }
	catch (::exception const& e) { crash(4,SIGABRT,"uncaught exception :",e.what()) ; }
	catch (...                 ) { crash(4,SIGABRT,"uncaught exception"           ) ; }
}

bool/*read_only*/ app_init(AppInitAction const& action) {
	t_thread_key = '=' ;                                                                                      // we are the main thread
	//
	::set_terminate(_terminate) ;
	for( int sig : iota(1,NSIG) ) if (is_sig_sync(sig)) set_sig_handler<crash_handler>(sig) ;                 // catch all synchronous signals so as to generate a backtrace
	//
	bool     read_only = false     ;                                                                          // unless proven read-only, assume we can write traces
	::string exe_path  = get_exe() ;
	g_exe_name = new ::string { base_name(exe_path) } ;
	try {
		g_lmake_root_s = new ::string { dir_name_s(exe_path,2) } ;
	} catch (::string const&) {
		exit(Rc::Usage,"cannot recognize this executable which is not in a standard lmake installation dir :",exe_path) ;
	}
	//
	bool do_trace = action.trace==Yes ;
	if (action.chk_version!=No) {
		if (!g_repo_root_s)
			try {
				SearchRootResult srr = search_root(action) ;
				g_repo_root_s   = new ::string{::move(srr.top_s    )} ;
				g_startup_dir_s = new ::string{::move(srr.startup_s)} ;
			} catch (::string const& e) {
				exit( Rc::Usage , e ) ;
			}
		//
		read_only = ::access(g_repo_root_s->c_str(),W_OK)!=0 ;
		if ( read_only && !action.read_only_ok ) exit(Rc::Perm,"cannot run in read-only repository") ;
		//
		try {
			chk_version( {}/*dir_s*/ , {
				.chk       = action.chk_version
			,	.key       = action.key
			,	.init_msg  = action.init_msg
			,	.clean_msg = action.clean_msg|git_clean_msg()
			,	.umask     = action.umask
			,	.version   = action.version
			} ) ;
		} catch (::string const& e) { exit(Rc::Version,e) ;                                                                                                                                           }
		//
		do_trace |= action.trace==Maybe ;
	}
	if ( !read_only && do_trace ) {
		if (!g_trace_file) g_trace_file = new ::string { cat(PrivateAdminDirS,"trace/",*g_exe_name) } ;
		try                       { Trace::s_start() ; }
		catch (::string const& e) { exit(Rc::Perm,e) ; }
		#if PROFILING
			set_env( "GMON_OUT_PREFIX" , dir_guard(cat(*g_repo_root_s,AdminDirS,"gmon.out/",*g_exe_name)) ) ; // ensure unique gmon data file in a non-intrusive (wrt autodep) place
		#endif
	}
	Trace trace("app_init",action.chk_version,STR(action.cd_root),+g_startup_dir_s?*g_startup_dir_s:""s) ;
	return read_only ;
}

SearchRootResult search_root(AppInitAction const& action) {
	::string   from_dir_s   = cwd_s()    ;
	::string   repo_root_s  = from_dir_s ;
	::vector_s candidates_s ;
	//
	auto has_mrkr = [&](::string const& mrkr) {
		FileTag t = FileInfo(repo_root_s+mrkr).tag() ;
		return is_dir_name(mrkr) ? t==FileTag::Dir : t>=FileTag::Target ;
	} ;
	//
	for(; repo_root_s!="/" ; repo_root_s=dir_name_s(repo_root_s) ) {
		if ( ::any_of( action.root_mrkrs , has_mrkr ) ) candidates_s.push_back(repo_root_s) ;
		if ( !action.cd_root                          ) break ;
	}
	switch (candidates_s.size()) {
		case 0 : throw cat("cannot find any of ",action.root_mrkrs) ;
		case 1 : repo_root_s = candidates_s[0] ; break ;
		default : {
			::vector_s candidates2_s ;
			for( ::string const& c_s : candidates_s ) if (FileInfo(c_s+AdminDirS).tag()==FileTag::Dir) candidates2_s.push_back(c_s) ;
			switch (candidates2_s.size()) {
				case 0 : {
					::string msg = "ambiguous root dir, consider 1 of :\n" ;
					for( ::string const& c_s : candidates_s ) msg <<"\tmkdir "<<c_s<<AdminDirS<<rm_slash <<'\n' ;
					throw msg ;
				}
				case 1 : repo_root_s = ::move(candidates2_s[0]) ; break ;
				default : {
					::string msg = cat("ambiguous root dir, consider ",candidates2_s.size()-1," of :\n") ;
					for( ::string const& c_s : candidates2_s ) msg <<"\trm -r "<< c_s+AdminDirS<<rm_slash <<'\n' ;
					throw msg ;
				}
			}
		}
	}
	SearchRootResult res { .top_s=repo_root_s , .sub_s=candidates_s[0].substr(repo_root_s.size()) , .startup_s=from_dir_s.substr(repo_root_s.size()) } ;
	//
	if ( +res.startup_s && ::chdir(g_repo_root_s->c_str())!=0 ) exit( Rc::System , "cannot chdir to ",*g_repo_root_s,rm_slash ) ;
	//
	return res ;
}
