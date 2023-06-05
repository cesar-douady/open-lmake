// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "disk.hh"

using namespace Disk ;

int main( int argc , char* argv[] ) {
	//
	app_init(true/*search_root*/,true/*cd_root*/) ;
	//
	ReqSyntax syntax{{
		{ ReqKey::Add       , { .short_name='a' ,                  .doc="make args behave as sources"     } }
	,	{ ReqKey::Delete    , { .short_name='d' ,                  .doc="delete frozen attribute of args" } }
	,	{ ReqKey::DeleteAll , { .short_name='D' ,                  .doc="delete all frozen attributes"    } }
	,	{ ReqKey::List      , { .short_name='l' ,                  .doc="list frozen jobs/files"          } }
	},{
		{ ReqFlag::Job      , { .short_name='j' , .has_arg=false , .doc="job oriented action"             } }
	,	{ ReqFlag::File     , { .short_name='f' , .has_arg=false , .doc="file oriented action"            } }
	,	{ ReqFlag::Force    , { .short_name='F' ,                  .doc="force action if possible"        } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	if ( cmd_line.flags[ReqFlag::Job] && cmd_line.flags[ReqFlag::File] ) syntax.usage("cannot process args as both jobs and files" ) ;
	switch (cmd_line.key) {
		case ReqKey::DeleteAll :
		case ReqKey::List      :
			if ( !cmd_line.args.empty()                                        ) syntax.usage("cannot have files when listing or deleting all"      ) ;
			if ( cmd_line.flags[ReqFlag::Job] || cmd_line.flags[ReqFlag::File] ) syntax.usage("cannot mention job/file when listing or deleting all") ;
		break ;
		default : ;
	}
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ReqProc::Freeze , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mk_rc(ok) ;
}
