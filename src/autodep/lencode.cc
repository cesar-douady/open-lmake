// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "record.hh"

#include "rpc_job.hh"

using namespace Disk ;

ENUM(Key,None)
ENUM(Flag
,	File
,	Context
,	MinLen
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::File    , { .short_name='f' , .has_arg=true , .doc="file storing code-value associations"                     } }
	,	{ Flag::Context , { .short_name='x' , .has_arg=true , .doc="context used within file to store code-value association" } }
	,	{ Flag::MinLen  , { .short_name='l' , .has_arg=true , .doc="min length of generated code from value"                  } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	//
	if (!cmd_line.flags[Flag::File   ]) syntax.usage("must have file to store code-value association"   ) ;
	if (!cmd_line.flags[Flag::Context]) syntax.usage("must have context to store code-value association") ;
	//
	uint8_t min_len = 1 ;
	try {
		if (cmd_line.flags[Flag::MinLen]) {
			min_len = from_chars<uint8_t>(cmd_line.flag_args[+Flag::MinLen]) ;
			if (min_len>MaxCodecBits) throw to_string("min len (",min_len,") cannot be larger max allowed code bits (",MaxCodecBits,')') ;
		}
	} catch (::string const& e) {
		syntax.usage(to_string("bad min len value : ",e)) ;
	}
	JobExecRpcReply reply = Record(New).direct(JobExecRpcReq(
		JobExecRpcProc::Encode
	,	::move(cmd_line.flag_args[+Flag::File   ])
	,	to_string(::cin.rdbuf())
	,	::move(cmd_line.flag_args[+Flag::Context])
	,	min_len
	)) ;
	if (reply.ok==Yes) { ::cout<<reply.txt<<'\n' ; return 0 ; }
	else               { ::cerr<<reply.txt       ; return 1 ; }
}
