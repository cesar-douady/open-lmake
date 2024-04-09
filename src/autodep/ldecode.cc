// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "record.hh"

#include "rpc_job.hh"

ENUM(Key,None)
ENUM(Flag
,	Code
,	File
,	Context
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::Code    , { .short_name='c' , .has_arg=true , .doc="code to retreive associated value from"               } }
	,	{ Flag::File    , { .short_name='f' , .has_arg=true , .doc="file storing code-value associations"                 } }
	,	{ Flag::Context , { .short_name='x' , .has_arg=true , .doc="context used within file to retreive value from code" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	//
	if (!cmd_line.flags[Flag::Code   ]) syntax.usage("must have code to retrieve associated value"   ) ;
	if (!cmd_line.flags[Flag::File   ]) syntax.usage("must have file to retrieve associated value"   ) ;
	if (!cmd_line.flags[Flag::Context]) syntax.usage("must have context to retrieve associated value") ;
	//
	JobExecRpcReply reply = Record(New,Yes/*enable*/).direct(JobExecRpcReq(
		JobExecProc::Decode
	,	::move(cmd_line.flag_args[+Flag::File   ])
	,	::move(cmd_line.flag_args[+Flag::Code   ])
	,	::move(cmd_line.flag_args[+Flag::Context])
	)) ;
	if (reply.ok==Yes) { ::cout<<reply.txt ; return 0 ; }
	else               { ::cerr<<reply.txt ; return 1 ; }
}
