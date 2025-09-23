// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "rpc_job.hh"

#include "job_support.hh"
#include "record.hh"

enum class Key : uint8_t { None } ;

enum class Flag : uint8_t {
	Code
,	File
,	Context
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
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
	try {
		auto&                fa    = cmd_line.flag_args                                                                                                          ;
		::pair_s<bool/*ok*/> reply = JobSupport::decode( {New,Yes/*enabled*/} , ::move(fa[+Flag::File]) , ::move(fa[+Flag::Code]) , ::move(fa[+Flag::Context]) ) ;
		if (reply.second) { Fd::Stdout.write(reply.first) ; exit(Rc::Ok  ) ; }
		else              { Fd::Stderr.write(reply.first) ; exit(Rc::Fail) ; }
	} catch (::string const& e) { exit(Rc::Internal,e) ; }
}
