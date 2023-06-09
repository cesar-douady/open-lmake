// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "autodep_support.hh"

ENUM(Key,None)
ENUM(Flag
,	Unlink
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::Unlink , { .short_name='u' , .has_arg=false , .doc="report an unlink" } }
	}} ;
	CmdLine<Key,Flag> cmd_line{syntax,argc,argv} ;
	//
	JobExecRpcReply reply = AutodepSupport(New).req( JobExecRpcReq( cmd_line.flags[Flag::Unlink]?JobExecRpcProc::Unlinks:JobExecRpcProc::Targets , cmd_line.args , false/*sync*/ ) ) ;
	//
	return 0 ;
}
