// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmakeserver/core.hh" // must be first to include Python.h first

#include "app.hh"
#include "disk.hh"
#include "lmakeserver/makefiles.hh"

using namespace Disk ;

using namespace Engine ;

int main( int argc , char* /*argv*/[] ) {
	::string admin_dir_s = AdminDirS ;
	//
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	g_trace_file = new ::string() ;                                // no trace as we are repairing admin_dir_s in which traces are made
	block_sigs({SIGCHLD}) ;
	app_init(false/*read_only_ok*/) ;
	Py::init(*g_lmake_dir_s) ;
	if (+*g_startup_dir_s) {
		g_startup_dir_s->pop_back() ;
		FAIL("lrepair must be started from repo root, not from ",*g_startup_dir_s) ;
	}
	if (is_target(ServerMrkr)) exit(Rc::Format,"after having ensured no lmakeserver is running, consider : rm ",ServerMrkr) ;
	//
	::string backup_admin_dir_s = no_slash(admin_dir_s)+".bck/"s ; // rename in same dir to be sure not to break sym links that can be inside (e.g. lmake/local_admin_dir and lmake/remote_admin_dir)
	::string repair_mrkr      = admin_dir_s+"repairing"          ;
	if (FileInfo(repair_mrkr).tag()>=FileTag::Reg) unlnk(no_slash(admin_dir_s),true/*dir_ok*/) ;        // if last lrepair was interrupted, admin_dir_s contains no useful information
	if (is_dir(no_slash(backup_admin_dir_s))) {
		if      (is_dir(no_slash(admin_dir_s))                                                  ) exit(Rc::Format,"backup already exists, consider : rm -r ",no_slash(backup_admin_dir_s)) ;
	} else {
		if      (!is_dir(PrivateAdminDirS+"local_admin/job_data"s)                              ) exit(Rc::Fail  ,"nothing to repair"                                                    ) ;
		else if (::rename(no_slash(admin_dir_s).c_str(),no_slash(backup_admin_dir_s).c_str())!=0) exit(Rc::System,"backup failed to ",no_slash(backup_admin_dir_s)                       ) ;
	}
	if ( AcFd fd { dir_guard(repair_mrkr) , Fd::Write } ; !fd ) exit(Rc::System,"cannot create ",repair_mrkr) ; // create marker
	g_writable = true ;
	::string msg ;
	msg << "the repair process is starting, if something goes wrong :"                                                                                             << '\n' ;
	msg << "to restore old state,                    consider : rm -r "<<no_slash(admin_dir_s)<<" ; mv "<<no_slash(backup_admin_dir_s)<<' '<<no_slash(admin_dir_s) << '\n' ;
	msg << "to restart the repair process,           consider : lrepair"                                                                                           << '\n' ;
	msg << "to continue with what has been repaired, consider : rm "<<repair_mrkr<<" ; rm -r "<<no_slash(backup_admin_dir_s)                                       << '\n' ;
	Fd::Stdout.write(msg) ;
	try                       { chk_version( false/*may_init*/ , backup_admin_dir_s ) ; }
	catch (::string const& e) { exit(Rc::Format,e) ;                                    }
	//
	mk_dir_s(PrivateAdminDirS) ;
	//
	try {
		::string msg = Makefiles::refresh(false/*crashed*/,true/*refresh*/) ;
		if (+msg) Fd::Stderr.write(ensure_nl(msg)) ;
	} catch (::string const& e) { exit(Rc::Format,e) ; }
	//
	Trace::s_new_trace_file( g_config->local_admin_dir_s + "trace/" + base_name(read_lnk("/proc/self/exe")) ) ;
	for( AncillaryTag tag : iota(All<AncillaryTag>) ) dir_guard(Job().ancillary_file(tag)) ;
	//
	//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	RepairDigest digest = Persistent::repair(backup_admin_dir_s+PRIVATE_ADMIN_SUBDIR_S+"local_admin/job_data") ;
	//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	chk_version(true/*may_init*/) ;
	unlnk(repair_mrkr) ;
	msg.clear() ;
	msg << "repo has been satisfactorily repaired "<<digest.n_repaired<<'/'<<digest.n_processed<<" jobs"                                                                             << '\n' ;
	msg << "to clean up after having ensured everything runs smoothly, consider : rm -r "<<no_slash(backup_admin_dir_s)                                                              << '\n' ;
	msg << "to restore old state,                                      consider : rm -r "<<no_slash(admin_dir_s)<<" ; mv "<<no_slash(backup_admin_dir_s)<<' '<<no_slash(admin_dir_s) << '\n' ;
	msg << "to restart the repair process,                             consider : rm -r "<<no_slash(admin_dir_s)<<" ; lrepair"                                                       << '\n' ;
	Fd::Stdout.write(msg) ;
	return 0 ;
}
