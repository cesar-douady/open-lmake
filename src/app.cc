// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <thread>

#include "disk.hh"
#include "trace.hh"

#include "app.hh"

using namespace Disk ;
using namespace Time ;

using InitTab = ::vmap<int/*pass*/,void(*)()> ;

::string* g_lmake_dir     = nullptr ;
::string* g_startup_dir_s = nullptr ;                                          // includes final   /, relative to g_root_dir , dir from which command was launched

void crash_handler(int sig) {
	if (sig==SIGABRT) crash(4,sig,"aborted"               ) ;
	else              crash(2,sig,"caught ",strsignal(sig)) ;
}

static InitTab& _get_init_tab() {
	static InitTab* _init_tab = new InitTab ;                                  // embed static var and define it through new to be sure it is available during initialization/finalization
	return         *_init_tab ;
}
bool at_init( int pass , void(*func)() ) {
	_get_init_tab().emplace_back(pass,func) ;
	return true ;                                                              // return val is useless, just more practical to use
}

#pragma GCC visibility push(default)                                           // force visibility of functions defined hereinafter, until the corresponding pop
extern "C" {
	const char* __asan_default_options () { return "verify_asan_link_order=0,detect_leaks=0" ; }
	const char* __ubsan_default_options() { return "halt_on_error=1"                         ; }
	const char* __tsan_default_options () { return "report_signal_unsafe=0"                  ; }
}
#pragma GCC visibility pop

void app_init( bool search_root , bool cd_root ) {
	//
	for( int sig=1 ; sig<NSIG ; sig++ ) if (is_sig_sync(sig)) set_sig_handler(sig,crash_handler) ; // catch all synchronous signals so as to generate a backtrace
	//
	try {
		::string root_dir = ::string{cwd()} ;
		if (search_root) {
			g_startup_dir_s = new ::string ;
			tie(root_dir,*g_startup_dir_s) = search_root_dir(root_dir) ;
		}
		lib_init(root_dir) ;
	} catch (::string const& e) { exit(2,e) ; }
	if (cd_root) {
		SWEAR(search_root) ;                                                   // it is meaningless to cd to root dir if we do not search it
		if (::chdir(g_root_dir->c_str())!=0) exit(2,"cannot chdir to ",*g_root_dir) ;
	}
	//
	::string exe = read_lnk("/proc/self/exe") ;
	if (g_trace_file==nullptr) g_trace_file = new ::string{to_string(AdminDir,"/trace/",base_name(exe))} ;
	/**/                       g_lmake_dir  = new ::string{dir_name(dir_name(exe))                     } ;
	//
	Trace::s_start() ;
	Trace trace("app_init",g_startup_dir_s?*g_startup_dir_s:""s) ;
	InitTab& tab = _get_init_tab() ;
	::sort(tab) ;
	for( auto const& [pass,f] : tab ) f() ;
}
