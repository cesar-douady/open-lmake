// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "client.hh"
#include "disk.hh"
#include "trace.hh"

#include "repo.hh"

using namespace Disk ;

int main( int argc , char* argv[] ) {
	ReqSyntax syntax {
		{	{ ReqFlag::DryRun , { .short_name='n' , .doc="report actions but dont execute them" } }
		}
	,	~ReqFlag::Job // passed args are typically dirs, jobs are non-sens
	} ;
	ReqCmdLine cmd_line { syntax , argc , argv } ;
	//
	app_init({.read_only_ok=false}) ;
	Trace trace("main") ;
	//
	//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Rc rc = out_proc( ReqProc::Collect , false/*read_only*/ , true/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	exit(rc) ;
}
