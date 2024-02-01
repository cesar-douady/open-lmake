// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "record.hh"

ENUM(Key,None)
ENUM(Flag
,	Verbose
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::Verbose , { .short_name='v' , .has_arg=false , .doc="return in error if deps are out of date" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv }        ;
	bool              verbose  = cmd_line.flags[Flag::Verbose] ;
	if (cmd_line.args.size()!=0 ) syntax.usage("must have no argument") ;
	JobExecRpcReply reply = Record(New).direct( JobExecRpcReq( JobExecRpcProc::ChkDeps , cmd_line.flags[Flag::Verbose]/*sync*/ ) ) ;
	return verbose && reply.ok!=Yes ? 1 : 0 ;
}
