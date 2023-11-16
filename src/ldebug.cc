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

	app_init(true/*search_root*/,true/*cd_root*/) ;
	Trace trace("main") ;

	ReqSyntax syntax{{},{
		{ ReqFlag::Graphic , { .short_name='g' , .doc="use GUI" } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;

	if (cmd_line.args.size()<1) syntax.usage(          "need a target to debug"                                ) ;
	if (cmd_line.args.size()>1) syntax.usage(to_string("cannot debug ",cmd_line.args.size()," targets at once")) ;
	cmd_line.flags |= ReqFlag::Debug ;

	::ostringstream script_file_stream ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( script_file_stream , ReqProc::Debug , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	::string script_file = script_file_stream.str() ; script_file.pop_back() ;                         // remove \n at end
	if ( int rc=mk_rc(ok) ) exit(rc,script_file) ;

	char* exec_args[] = { script_file.data() , nullptr } ;

	::cerr << "executing : " << script_file << endl ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::execv(script_file.c_str(),exec_args) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

	exit(2,"could not run ",script_file) ;
}
