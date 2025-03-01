// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <exception> // ::set_terminate

#include "version.hh"

#include "disk.hh"
#include "trace.hh"

#include "app.hh"
#include "process.hh"
#include "rpc_client.hh"

using namespace Disk ;
using namespace Time ;

::string* g_lmake_root_s  = nullptr ;
::string* g_repo_root_s   = nullptr ;
::string* g_startup_dir_s = nullptr ; // relative to g_repo_root_s , dir from which command was launched
::string* g_exe_name      = nullptr ;

void crash_handler( int sig , void* addr ) {
	if (sig==SIGABRT) crash(4,sig,"aborted"                         ) ;
	else              crash(2,sig,::strsignal(sig),"at address",addr) ;
}

static void _terminate() {
	try                          { ::rethrow_exception(::current_exception()) ;       }
	catch (::string    const& e) { crash(4,SIGABRT,"uncaught exception :",e       ) ; }
	catch (::exception const& e) { crash(4,SIGABRT,"uncaught exception :",e.what()) ; }
	catch (...                 ) { crash(4,SIGABRT,"uncaught exception"           ) ; }
}

bool/*read_only*/ app_init( bool read_only_ok , Bool3 chk_version_ , Bool3 cd_root ) {
	if (cd_root==No) SWEAR( chk_version_==No && read_only_ok ) ;                              // cannot check repo without a repo dir
	::set_terminate(_terminate) ;
	for( int sig : iota(1,NSIG) ) if (is_sig_sync(sig)) set_sig_handler<crash_handler>(sig) ; // catch all synchronous signals so as to generate a backtrace
	//
	if (!g_startup_dir_s) g_startup_dir_s = new ::string ;
	if (!g_repo_root_s  ) {
		try {
			SearchRootResult srr = search_root_s() ;
			g_repo_root_s    = new ::string{srr.top_s} ;
			*g_startup_dir_s = srr.startup_s           ;
			if ( cd_root==Yes && +*g_startup_dir_s && ::chdir(no_slash(*g_repo_root_s).c_str())!=0 ) exit(Rc::System,"cannot chdir to ",no_slash(*g_repo_root_s)) ;
		} catch (::string const& e) {
			if (cd_root!=No) exit(Rc::Usage,e) ;
		}
	}
	::string exe_path = get_exe() ;
	/**/               g_exe_name     = new ::string{base_name(exe_path)                   } ;
	if (!g_trace_file) g_trace_file   = new ::string{PrivateAdminDirS+"trace/"s+*g_exe_name} ;
	/**/               g_lmake_root_s = new ::string{dir_name_s(exe_path,2)                } ;
	#if PROFILING
		set_env( "GMON_OUT_PREFIX" , dir_guard(*g_repo_root_s+GMON_DIR_S+*g_exe_name) ) ;     // ensure unique gmon data file in a non-intrusive (wrt autodep) place
	#endif
	//
	bool read_only = !g_repo_root_s || ::access(no_slash(*g_repo_root_s).c_str(),W_OK) ;      // cannot modify repo if no repo
	if (read_only>read_only_ok) exit(Rc::Perm,"cannot run in read-only repository") ;
	//
	if (chk_version_!=No)
		try                       { chk_version( !read_only && chk_version_==Maybe ) ; }
		catch (::string const& e) { exit(Rc::Format,e) ;                               }
	//
    t_thread_key = '=' ;                                                                      // we are the main thread
	if (!read_only)
		try                       { Trace::s_start() ; }
		catch (::string const& e) { exit(Rc::Perm,e) ; }
	Trace trace("app_init",chk_version_,cd_root,g_startup_dir_s?*g_startup_dir_s:""s) ;
	return read_only ;
}

void chk_version( bool may_init , ::string const& admin_dir_s ) {
	::string version_file = admin_dir_s+"version" ;
	AcFd     version_fd   { version_file }        ;
	if (!version_fd) {
		throw_unless( may_init , "repo not initialized, consider : lmake" ) ;
		AcFd(dir_guard(version_file),Fd::Write).write(cat(VersionMrkr,'\n')) ;
	} else {
		::string stored = version_fd.read() ;
		throw_unless( +stored && stored.back()=='\n' , "bad version file" ) ;
		stored.pop_back() ;
		if (stored!=VersionMrkr) throw "version mismatch, "+git_clean_msg() ;
	}
}
