// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "rpc_client.hh"
#include "trace.hh"

int main( int argc , char* argv[] ) {
	bool read_only = app_init(true/*read_only_ok*/) ;
	Trace trace("main") ;
	//
	ReqSyntax syntax{{
		{ ReqKey::Bom        , { .short_name='b' , .doc="show necessary sources"                   } }
	,	{ ReqKey::Cmd        , { .short_name='c' , .doc="show cmd"                                 } }
	,	{ ReqKey::Deps       , { .short_name='d' , .doc="show existing deps"                       } }
	,	{ ReqKey::Env        , { .short_name='E' , .doc="show envionment variables to execute job" } }
	,	{ ReqKey::Info       , { .short_name='i' , .doc="show info about jobs leading to files"    } }
	,	{ ReqKey::InvDeps    , { .short_name='D' , .doc="show dependents"                          } }
	,	{ ReqKey::InvTargets , { .short_name='T' , .doc="show producing jobs"                      } }
	,	{ ReqKey::Running    , { .short_name='r' , .doc="show running jobs"                        } }
	,	{ ReqKey::Stderr     , { .short_name='e' , .doc="show stderr"                              } }
	,	{ ReqKey::Stdout     , { .short_name='o' , .doc="show stdout"                              } }
	,	{ ReqKey::Targets    , { .short_name='t' , .doc="show targets of jobs leading to files"    } }
	},{
		{ ReqFlag::Porcelaine , { .short_name='p' , .has_arg=false , .doc="generate output as an easy to parse python dict" } }
	,	{ ReqFlag::Verbose    , { .short_name='v' , .has_arg=false , .doc="generate info for non-existent deps/targts"      } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	bool may_verbose = false ;
	switch (cmd_line.key) {
		case ReqKey::Bom     :
		case ReqKey::Deps    :
		case ReqKey::Targets :
		case ReqKey::Running :
		case ReqKey::Stderr  : may_verbose = true ;
		default : ;
	}
	//
	if ( cmd_line.flags[ReqFlag::Verbose   ] && !may_verbose                     ) syntax.usage("verbose is only for showing deps, targets or stderr") ;
	if ( cmd_line.flags[ReqFlag::Job       ] && cmd_line.key==ReqKey::InvDeps    ) syntax.usage("dependents cannot be shown for jobs"                ) ;
	if ( cmd_line.flags[ReqFlag::Job       ] && cmd_line.key==ReqKey::InvTargets ) syntax.usage("producing jobs cannot be shown for jobs"            ) ;
	if ( cmd_line.flags[ReqFlag::Porcelaine] && cmd_line.key!=ReqKey::Info       ) syntax.usage("porcelaine output is only valid with --info"        ) ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ReqProc::Show , read_only , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	exit(mk_rc(ok)) ;
}
