// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
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
	//
	ReqSyntax syntax{{
		{ ReqKey::AllDeps    , { .short_name='a' , .doc="show all deps, including non existing ones" } }
	,	{ ReqKey::Deps       , { .short_name='d' , .doc="show existing deps"                         } }
	,	{ ReqKey::InvDeps    , { .short_name='D' , .doc="show dependents"                            } }
	,	{ ReqKey::Env        , { .short_name='E' , .doc="show envionment variables to execute job"   } }
	,	{ ReqKey::Info       , { .short_name='i' , .doc="show info about jobs leading to files"      } }
	,	{ ReqKey::Script     , { .short_name='s' , .doc="show script"                                } }
	,	{ ReqKey::ExecScript , { .short_name='S' , .doc="show a sh-executable script"                } }
	,	{ ReqKey::Stderr     , { .short_name='e' , .doc="show stderr"                                } }
	,	{ ReqKey::Stdout     , { .short_name='o' , .doc="show stdout"                                } }
	,	{ ReqKey::Targets    , { .short_name='t' , .doc="show targets of jobs leading to files"      } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	if ( cmd_line.key==ReqKey::ExecScript && cmd_line.args.size()!=1 ) syntax.usage("must have a single argument to generate an executable script") ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ReqProc::Show , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mk_rc(ok) ;
}
