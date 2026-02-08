// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "trace.hh"

#include "repo.hh"
#include "client.hh"

using namespace Disk ;

int main( int argc , char* argv[] ) {
	app_init({.read_only_ok=false}) ;
	Trace trace("main") ;
	//
	ReqSyntax syntax {{
		{ ReqKey::Add    , { .short_name='a' , .doc="mark args"              } }
	,	{ ReqKey::Delete , { .short_name='d' , .doc="delete marks of args"   } }
	,	{ ReqKey::Clear  , { .short_name='c' , .doc="clear all marks"        } }
	,	{ ReqKey::List   , { .short_name='l' , .doc="list marked jobs/files" } }
	},{
		{ ReqFlag::Force     , { .short_name='F' , .doc="force action if possible"                          } }
	,	{ ReqFlag::Freeze    , { .short_name='f' , .doc="freeze job : prevent rebuild and behave as source" } }
	,	{ ReqFlag::NoTrigger , { .short_name='t' , .doc="do not trigger rebuild of dependent jobs"          } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	if ( is_mark_glb(cmd_line.key) && +cmd_line.args                              ) syntax.usage("cannot have files when listing or deleting all") ;
	if ( cmd_line.flags[ReqFlag::Freeze] + cmd_line.flags[ReqFlag::NoTrigger] !=1 ) syntax.usage("need exactly one mark : freeze or no-trigger"  ) ;
	//
	//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Rc rc = out_proc( ReqProc::Mark , false/*read_only*/ , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	exit(rc) ;
}
