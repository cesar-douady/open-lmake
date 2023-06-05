// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "autodep_support.hh"

using namespace Disk ;
using namespace Hash ;

static const umap_s<JobExecRpcProc> _g_exe_tab = {
	{ "ldepend"           , JobExecRpcProc::Deps            }
,	{ "lunlink"           , JobExecRpcProc::Unlinks         }
,	{ "ltarget"           , JobExecRpcProc::Targets         }
,	{ "lcritical_barrier" , JobExecRpcProc::CriticalBarrier }
,	{ "lcheck_deps"       , JobExecRpcProc::ChkDeps         }
,	{ "ldep_crcs"         , JobExecRpcProc::DepCrcs         }
} ;

int main( int argc , char* argv[]) {
	const char* arg0 = strrchr(argv[0],'/') ;
	if (!arg0) arg0 = argv[0] ;
	else       arg0 = arg0+1  ;                                                // suppress leading /
	if (!_g_exe_tab.contains(arg0)) exit(2,"bad command name ",arg0) ;
	//
	JobExecRpcProc proc = _g_exe_tab.at(arg0) ;
	Cmd            cmd  = g_proc_tab.at(proc) ;
	JobExecRpcReq  jerr ;
	if (cmd.has_args) {
		::vector_s files ; files.reserve(argc-1) ;
		for( int i=1 ; i<argc ; i++ ) files.emplace_back(argv[i]) ;
		if (proc==JobExecRpcProc::Deps) jerr = JobExecRpcReq(proc,files,DepAccesses::All,cmd.sync) ;
		else                            jerr = JobExecRpcReq(proc,files,                 cmd.sync) ;
	} else if (argc==1) {
		jerr = JobExecRpcReq(proc,cmd.sync) ;
	} else {
		exit(2,arg0," takes no arguments") ;
	}
	//
	JobExecRpcReply reply = AutodepSupport(New).req(jerr) ;
	if (cmd.has_ok  ) { SWEAR(cmd.sync) ; if (!reply.ok) return 1 ;                                     } // cannot have crcs with async cmd
	if (cmd.has_crcs) { SWEAR(cmd.sync) ; for ( Crc crc : reply.crcs ) ::cout << ::string(crc) <<'\n' ; } // .
	return 0 ;
}
