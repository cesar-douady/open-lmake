// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "version.hh"

#include "disk.hh"
#include "trace.hh"

#include "app.hh"
#include "process.hh"
#include "rpc_client.hh"

using namespace Disk ;
using namespace Time ;

::string* g_lmake_dir     = nullptr ;
::string* g_startup_dir_s = nullptr ; // includes final /, relative to g_root_dir , dir from which command was launched
::string* g_root_dir      = nullptr ;
::string* g_exe_name      = nullptr ;

void crash_handler(int sig) {
	if (sig==SIGABRT) crash(4,sig,"aborted"               ) ;
	else              crash(2,sig,"caught ",strsignal(sig)) ;
}

void app_init( Bool3 chk_version_ , bool cd_root ) {
	sanitize(::cout) ;
	sanitize(::cerr) ;
	//
	for( int sig=1 ; sig<NSIG ; sig++ ) if (is_sig_sync(sig)) set_sig_handler(sig,crash_handler) ; // catch all synchronous signals so as to generate a backtrace
	//
	if (!g_startup_dir_s) g_startup_dir_s = new ::string ;
	if (!g_root_dir     ) {
		try {
			g_root_dir                        = new ::string{cwd()}          ;
			tie(*g_root_dir,*g_startup_dir_s) = search_root_dir(*g_root_dir) ;
		} catch (::string const& e) { exit(Rc::Usage,e) ; }
		if ( cd_root && +*g_startup_dir_s && ::chdir(g_root_dir->c_str())!=0 ) exit(Rc::System,"cannot chdir to ",*g_root_dir) ;
	}
	//
	::string exe = read_lnk("/proc/self/exe") ;
	g_exe_name = new ::string{base_name(exe)} ;
	if (!g_trace_file) g_trace_file = new ::string{to_string(PrivateAdminDir,"/trace/",*g_exe_name)} ;
	/**/               g_lmake_dir  = new ::string{dir_name(dir_name(exe))                         } ;
	//
	if (chk_version_!=No)
		try                       { chk_version(chk_version_==Maybe) ; }
		catch (::string const& e) { exit(Rc::Format,e) ;               }
	//
	Trace::s_start() ;
	Trace trace("app_init",chk_version_,STR(cd_root),g_startup_dir_s?*g_startup_dir_s:""s) ;
}

void chk_version( bool may_init , ::string const& admin_dir ) {
	::string   version_file = to_string(admin_dir,"/version") ;
	::vector_s stored       = read_lines(version_file)        ;
	if (+stored) {
		if (stored.size()!=1u     ) throw to_string("bad version file ",version_file) ;
		if (stored[0]!=VersionMrkr) {
			throw "version mismatch, "+git_clean_msg() ;
		}
	} else {
		if (!may_init) throw "repo not initialized, consider : lmake"s ;
		write_lines( dir_guard(version_file) , {VersionMrkr} ) ;
	}
}
