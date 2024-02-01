// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "trace.hh"

#include "app.hh"
#include "process.hh"

using namespace Disk ;
using namespace Time ;

::string* g_lmake_dir     = nullptr ;
::string* g_startup_dir_s = nullptr ; // includes final /, relative to g_root_dir , dir from which command was launched
::string* g_root_dir      = nullptr ;

void crash_handler(int sig) {
	if (sig==SIGABRT) crash(4,sig,"aborted"               ) ;
	else              crash(2,sig,"caught ",strsignal(sig)) ;
}

void app_init( bool search_root , bool cd_root ) {
	sanitize(::cout) ;
	sanitize(::cerr) ;
	//
	for( int sig=1 ; sig<NSIG ; sig++ ) if (is_sig_sync(sig)) set_sig_handler(sig,crash_handler) ; // catch all synchronous signals so as to generate a backtrace
	//
	try {
		g_root_dir = new ::string{cwd()} ;
		if (search_root) {
			g_startup_dir_s = new ::string ;
			tie(*g_root_dir,*g_startup_dir_s) = search_root_dir(*g_root_dir) ;
		}
	} catch (::string const& e) { exit(2,e) ; }
	if (cd_root) {
		SWEAR(search_root) ;                                                                       // it is meaningless to cd to root dir if we do not search it
		if (::chdir(g_root_dir->c_str())!=0) exit(2,"cannot chdir to ",*g_root_dir) ;
	}
	//
	::string exe = read_lnk("/proc/self/exe") ;
	if (g_trace_file==nullptr) g_trace_file = new ::string{to_string(PrivateAdminDir,"/trace/",base_name(exe))} ;
	/**/                       g_lmake_dir  = new ::string{dir_name(dir_name(exe))                            } ;
	//
	Trace::s_start() ;
	Trace trace("app_init",g_startup_dir_s?*g_startup_dir_s:""s) ;
}

//
// env encoding
//

ENUM( EnvMrkr
,	Root
,	Lmake
)

static constexpr char LmakeMrkrData[2] = {0,+EnvMrkr::Lmake} ; static const ::string LmakeMrkr {LmakeMrkrData,2} ;
static constexpr char RootMrkrData [2] = {0,+EnvMrkr::Root } ; static const ::string RootMrkr  {RootMrkrData ,2} ;
::string env_encode(::string&& txt) {
	txt = glb_subst(::move(txt),*g_root_dir ,RootMrkr ) ;
	txt = glb_subst(::move(txt),*g_lmake_dir,LmakeMrkr) ;
	return txt ;
}

::string env_decode(::string&& txt) {
	txt = glb_subst(::move(txt),RootMrkr ,*g_root_dir ) ;
	txt = glb_subst(::move(txt),LmakeMrkr,*g_lmake_dir) ;
	return txt ;
}
