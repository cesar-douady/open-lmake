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

::string* g_lmake_dir_s   = nullptr ;
::string* g_root_dir_s    = nullptr ;
::string* g_startup_dir_s = nullptr ; // relative to g_root_dir_s , dir from which command was launched
::string* g_exe_name      = nullptr ;

void crash_handler(int sig) {
	if (sig==SIGABRT) crash(4,sig,"aborted"               ) ;
	else              crash(2,sig,"caught ",::strsignal(sig)) ;
}

bool/*read_only*/ app_init( bool read_only_ok , Bool3 chk_version_ , bool cd_root ) {
	sanitize(::cout) ;
	sanitize(::cerr) ;
	//
	for( int sig : iota(1,NSIG) ) if (is_sig_sync(sig)) set_sig_handler(sig,crash_handler) ; // catch all synchronous signals so as to generate a backtrace
	//
	if (!g_startup_dir_s) g_startup_dir_s = new ::string ;
	if (!g_root_dir_s   ) {
		try {
			g_root_dir_s                        = new ::string{cwd_s()}            ;
			tie(*g_root_dir_s,*g_startup_dir_s) = search_root_dir_s(*g_root_dir_s) ;
		} catch (::string const& e) { exit(Rc::Usage,e) ; }
		if ( cd_root && +*g_startup_dir_s && ::chdir(no_slash(*g_root_dir_s).c_str())!=0 ) exit(Rc::System,"cannot chdir to ",no_slash(*g_root_dir_s)) ;
	}
	::string exe = read_lnk("/proc/self/exe") ;
	/**/               g_exe_name    = new ::string{base_name(exe)                        } ;
	if (!g_trace_file) g_trace_file  = new ::string{PrivateAdminDirS+"trace/"s+*g_exe_name} ;
	/**/               g_lmake_dir_s = new ::string{dir_name_s(dir_name_s(exe))           } ;
	#if PROFILING
		set_env( "GMON_OUT_PREFIX" , dir_guard(*g_root_dir_s+GMON_DIR_S+*g_exe_name) ) ;           // ensure unique gmon data file in a non-intrusive (wrt autodep) place
	#endif
	//
	bool read_only = ::access(no_slash(*g_root_dir_s).c_str(),W_OK) ;
	if (read_only>read_only_ok) exit(Rc::Perm,"cannot run in read-only repository") ;
	//
	if (chk_version_!=No)
		try                       { chk_version( !read_only && chk_version_==Maybe ) ; }
		catch (::string const& e) { exit(Rc::Format,e) ;                               }
	//
    t_thread_key = '=' ;                                                                           // we are the main thread
	if (!read_only)
		try                       { Trace::s_start() ; }
		catch (::string const& e) { exit(Rc::Perm,e) ; }
	Trace trace("app_init",chk_version_,STR(cd_root),g_startup_dir_s?*g_startup_dir_s:""s) ;
	return read_only ;
}

void chk_version( bool may_init , ::string const& admin_dir_s ) {
	::string   version_file = admin_dir_s+"version"    ;
	::vector_s stored       = read_lines(version_file) ;
	if (+stored) {
		if (stored.size()!=1u     ) throw "bad version file "+version_file     ;
		if (stored[0]!=VersionMrkr) throw "version mismatch, "+git_clean_msg() ;
	} else {
		throw_unless( may_init , "repo not initialized, consider : lmake" ) ;
		write_lines( dir_guard(version_file) , {VersionMrkr} ) ;
	}
}
