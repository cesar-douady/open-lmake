// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "rpc_client.hh"
#include "trace.hh"

int main( int argc , char* argv[] ) {
	app_init(true/*search_root*/,true/*cd_root*/) ;
	Trace trace("main") ;

	ReqSyntax syntax{{},{}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;

	if (cmd_line.args.size()<1) syntax.usage(          "need a target to debug"                                ) ;
	if (cmd_line.args.size()>1) syntax.usage(to_string("cannot debug ",cmd_line.args.size()," targets at once")) ;
	ReqCmdLine lshow_cmd_line ;
	lshow_cmd_line.key   = ReqKey::ExecScript                     ;
	lshow_cmd_line.flags = { ReqFlag::Debug , ReqFlag::ManualOk } ;
	lshow_cmd_line.args  = cmd_line.args                          ;
	::ostringstream dbg_script ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( dbg_script , ReqProc::Show , false/*refresh_makefiles*/ , lshow_cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( int rc=mk_rc(ok) ) {
		::cout << dbg_script.str() ;
		return rc ;
	}

	::string script      = "LMAKE/debug_script"   ;
	char*    c_script    = script.data()          ;
	char*    exec_args[] = { c_script , nullptr } ;

	OFStream(script) << dbg_script.str() ;
	::chmod(c_script,0755) ;                                                   // make script executable
	::execv(c_script,exec_args) ;
	exit(2,"could not launch ",exec_args[0]) ;
}
