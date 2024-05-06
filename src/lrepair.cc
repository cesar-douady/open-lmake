// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "lmakeserver/makefiles.hh"
#include "lmakeserver/core.hh"

using namespace Disk ;

using namespace Engine ;

int main( int argc , char* /*argv*/[] ) {
	//
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	bool has_admin_dir = is_dir(AdminDir) ;
	g_trace_file = new ::string() ;                      // no trace as we are repairing AdminDir in which traces are made
	block_sigs({SIGCHLD}) ;
	app_init(No/*chk_version*/) ;                        // lrepair must always be launched at root
	Py::init( *g_lmake_dir , true/*multi-thread*/ ) ;
	if (+*g_startup_dir_s) {
		g_startup_dir_s->pop_back() ;
		FAIL("lrepair must be started from repo root, not from ",*g_startup_dir_s) ;
	}
	if (is_target(ServerMrkr)) exit(Rc::Format,"after having ensured no lmakeserver is running, consider : rm ",ServerMrkr) ;
	//
	::string backup_admin_dir = AdminDir+".bck"s       ; // rename in same dir to be sure not to break sym links that can be inside (e.g. lmake/local_admin_dir and lmake/remote_admin_dir)
	::string repair_mrkr      = AdminDir+"/repairing"s ;
	if (FileInfo(repair_mrkr).tag()>=FileTag::Reg) unlnk(AdminDir,true/*dir_ok*/) ;                     // if last lrepair was interrupted, AdminDir contains no useful information
	if (is_dir(backup_admin_dir)) {
		if      (has_admin_dir                                    ) exit(Rc::Format,"backup already existing, consider : rm -r ",backup_admin_dir) ;
	} else {
		if      (!is_dir(PrivateAdminDir+"/local_admin/job_data"s)) exit(Rc::Fail  ,"nothing to repair"                                          ) ;
		else if (::rename(AdminDir,backup_admin_dir.c_str())!=0   ) exit(Rc::System,"backup failed to ",backup_admin_dir                         ) ;
	}
	if ( AutoCloseFd fd=open_write(repair_mrkr) ; !fd ) exit(Rc::System,"cannot create ",repair_mrkr) ; // create marker
	Persistent::writable = true ;
	::cout << "the repair process is starting, if something goes wrong :" << endl ;
	::cout << "to restore old state,                   consider : rm -r "<<AdminDir<<" ; mv "<<backup_admin_dir<<' '<<AdminDir << endl ;
	::cout << "to restart the repair process,          consider : lrepair"                                                     << endl ;
	::cout << "to continue with what has been repaired consider : rm "<<repair_mrkr<<" ; rm -r "<<backup_admin_dir             << endl ;
	try                       { chk_version( false/*may_init*/ , backup_admin_dir ) ; }
	catch (::string const& e) { exit(Rc::Format,e) ;                                  }
	//
	mk_dir(AdminDir       ) ;
	mk_dir(PrivateAdminDir) ;
	//
	try {
		::string msg = Makefiles::refresh(false/*crashed*/,true/*refresh*/) ;
		if (+msg) ::cerr << ensure_nl(msg) ;
	} catch (::string const& e) { exit(Rc::Format,e) ; }
	//
	for( AncillaryTag tag : All<AncillaryTag> ) dir_guard(Job().ancillary_file(tag)) ;
	//
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Persistent::repair(to_string(backup_admin_dir,'/',PRIVATE_ADMIN_SUBDIR,"/local_admin/job_data")) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	unlnk(repair_mrkr) ;
	::cout << "repo has been satisfactorily repaired" << endl ;
	::cout << "to clean up after having ensured everything runs smoothly, consider : rm -r "<<backup_admin_dir                                    << endl ;
	::cout << "to restore old state,                                      consider : rm -r "<<AdminDir<<" ; mv "<<backup_admin_dir<<' '<<AdminDir << endl ;
	::cout << "to restart the repair process,                             consider : rm -r "<<AdminDir<<" ; lrepair"                              << endl ;
	return 0 ;
}
