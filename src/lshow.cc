// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "trace.hh"

#include "client.hh"
#include "repo.hh"
#include "rpc_client.hh"

int main( int argc , char* argv[] ) {
	bool read_only = repo_app_init() ;
	Trace trace("main") ;
	//
	ReqSyntax syntax {{
		{ ReqKey::Bom        , { .short_name='b' , .doc="show necessary sources"                        } }
	,	{ ReqKey::Cmd        , { .short_name='c' , .doc="show cmd"                                      } }
	,	{ ReqKey::Deps       , { .short_name='d' , .doc="show existing deps"                            } }
	,	{ ReqKey::Env        , { .short_name='E' , .doc="show envionment variables to execute job"      } }
	,	{ ReqKey::Info       , { .short_name='i' , .doc="show info about jobs leading to files"         } }
	,	{ ReqKey::InvDeps    , { .short_name='D' , .doc="show dependents"                               } }
	,	{ ReqKey::InvTargets , { .short_name='T' , .doc="show producing jobs"                           } }
	,	{ ReqKey::Running    , { .short_name='r' , .doc="show running jobs"                             } }
	,	{ ReqKey::Stderr     , { .short_name='e' , .doc="show stderr"                                   } }
	,	{ ReqKey::Stdout     , { .short_name='o' , .doc="show stdout"                                   } }
	,	{ ReqKey::Targets    , { .short_name='t' , .doc="show targets of jobs leading to files"         } }
	,	{ ReqKey::Trace      , { .short_name='u' , .doc="show execution trace of jobs leading to files" } }
	},{
		{ ReqFlag::Porcelaine , { .short_name='p' , .doc="generate output as an easy to parse python object" } }
	}} ;
	ReqCmdLine cmd_line { syntax , argc , argv } ;
	//
	if ( cmd_line.flags[ReqFlag::Job] && cmd_line.key==ReqKey::InvDeps    ) syntax.usage("dependents cannot be shown for jobs"    ) ;
	if ( cmd_line.flags[ReqFlag::Job] && cmd_line.key==ReqKey::InvTargets ) syntax.usage("producing jobs cannot be shown for jobs") ;
	//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Rc rc = out_proc( ReqProc::Show , read_only , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	exit(rc) ;
}
