// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "rpc_client.hh"

int main( int argc , char* argv[] ) {
	app_init(false/*read_only_ok*/) ;
	Trace trace("main") ;
	//
	ReqSyntax syntax {{
		{ ReqKey::None      , { .short_name=0   , .doc="rerun files provided in arguments"                   } }
	,	{ ReqKey::Resources , { .short_name='r' , .doc="rerun jobs with new resources, even if not in error" } }
	},{
		{ ReqFlag::Deps    , { .short_name='d' , .doc="forget about deps"    } }
	,	{ ReqFlag::Targets , { .short_name='t' , .doc="forget about targets" } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	bool refresh = cmd_line.key==ReqKey::Resources ;
	if ( refresh && +cmd_line.args ) syntax.usage("must not have targets when forgetting resources" ) ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ReqProc::Forget , false/*read_only*/ , refresh , syntax , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	exit(mk_rc(ok)) ;
}
