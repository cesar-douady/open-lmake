// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "rpc_client.hh"
#include "trace.hh"

int main( int argc , char* argv[] ) {
	app_init() ;
	Trace trace("main") ;
	//
	ReqSyntax syntax{{
		{ ReqKey::Cmd        , { .short_name='c' , .doc="show cmd"                                   } }
	,	{ ReqKey::Deps       , { .short_name='d' , .doc="show existing deps"                         } }
	,	{ ReqKey::Env        , { .short_name='E' , .doc="show envionment variables to execute job"   } }
	,	{ ReqKey::ExecScript , { .short_name='s' , .doc="show a sh-executable script"                } }
	,	{ ReqKey::Info       , { .short_name='i' , .doc="show info about jobs leading to files"      } }
	,	{ ReqKey::InvDeps    , { .short_name='D' , .doc="show dependents"                            } }
	,	{ ReqKey::Stderr     , { .short_name='e' , .doc="show stderr"                                } }
	,	{ ReqKey::Stdout     , { .short_name='o' , .doc="show stdout"                                } }
	,	{ ReqKey::Targets    , { .short_name='t' , .doc="show targets of jobs leading to files"      } }
	},{
		{ ReqFlag::Debug      , { .short_name='u' , .has_arg=true  , .doc="generate debug executable script with arg as debug directory" } }
	,	{ ReqFlag::Graphic    , { .short_name='g' , .has_arg=false , .doc="use GUI"                                                      } }
	,	{ ReqFlag::Porcelaine , { .short_name='p' , .has_arg=false , .doc="generate output as an easy to parse python dict"              } }
	,	{ ReqFlag::Verbose    , { .short_name='v' , .has_arg=false , .doc="generate info for non-existent deps/targts"                   } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	bool may_verbose = cmd_line.key==ReqKey::Deps || cmd_line.key==ReqKey::Targets || cmd_line.key==ReqKey::Stderr ;
	//
	if ( cmd_line.key==ReqKey::ExecScript && cmd_line.args.size()!=1             ) syntax.usage("must have a single argument to generate an executable script") ;
	if ( cmd_line.flags[ReqFlag::Verbose   ] && !may_verbose                     ) syntax.usage("verbose is only for showing deps, targets or stderr"         ) ;
	if ( cmd_line.flags[ReqFlag::Debug     ] && cmd_line.key!=ReqKey::ExecScript ) syntax.usage("debug is only for showing executable script"                 ) ;
	if ( cmd_line.flags[ReqFlag::Job       ] && cmd_line.key==ReqKey::InvDeps    ) syntax.usage("dependents cannot be shown for jobs"                         ) ;
	if ( cmd_line.flags[ReqFlag::Porcelaine] && cmd_line.key!=ReqKey::Info       ) syntax.usage("porcelaine output is only valid with --info"                 ) ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ::cout , ReqProc::Show , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	exit(mk_rc(ok)) ;
}
