// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "client.hh"
#include "rpc_client.hh"
#include "trace.hh"

using namespace Disk ;

int main( int argc , char* argv[] ) {
	app_init() ;
	Trace trace("main") ;
	//
	ReqSyntax syntax{{},{
		{ ReqFlag::Key    , { .short_name='k' , .has_arg=true  , .doc="entry into config.debug to specify debug method" } }
	,	{ ReqFlag::NoExec , { .short_name='n' , .has_arg=false , .doc="dont execute, just generate files"               } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	if ( cmd_line.args.size()<1 ) syntax.usage("need a target to debug"                                ) ;
	if ( cmd_line.args.size()>1 ) syntax.usage("cannot debug "s+cmd_line.args.size()+" targets at once") ;
	//
	::vector_s script_files ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( script_files , ReqProc::Debug , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( Rc rc=mk_rc(ok) ; +rc ) exit(rc) ;
	SWEAR(script_files.size()==1,script_files) ;
	::string& script_file = script_files[0] ;
	//
	char* exec_args[] = { script_file.data() , nullptr } ;
	//
	if (cmd_line.flags[ReqFlag::NoExec]) {
		::cout << "script file : " << script_file << endl ;
	} else {
		::cerr << "executing : " << script_file << endl ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		::execv(script_file.c_str(),exec_args) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//
		exit(Rc::System,"could not run ",script_file) ;
	}
}
