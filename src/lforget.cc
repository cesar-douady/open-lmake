// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "rpc_client.hh"

int main( int argc , char* argv[] ) {
	app_init(true/*search_root*/,true/*cd_root*/) ;
	Trace trace("main") ;
	//
	ReqSyntax syntax{{
		{ ReqKey::None      , { .short_name=0   , .doc="rerun files provided in arguments"                   } }
	,	{ ReqKey::Error     , { .short_name='e' , .doc="rerun jobs in error, even if up to date"             } }
	,	{ ReqKey::Resources , { .short_name='r' , .doc="rerun jobs with new resources, even if not in error" } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	switch (cmd_line.key) {
		case ReqKey::Error     : if (!cmd_line.args.empty()) syntax.usage("must not have targets when forgetting resources") ; break ;
		case ReqKey::Resources : if (!cmd_line.args.empty()) syntax.usage("must not have targets when forgetting errors"   ) ; break ;
		default : ;
	}
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ReqProc::Forget , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mk_rc(ok) ;
}
