// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "rpc_job.hh"

#include "job_support.hh"
#include "record.hh"

enum class Key : uint8_t { None } ;

enum class Flag : uint8_t {
	Code
,	Table
,	Context
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::Code    , { .short_name='c' , .has_arg=true , .doc="code to retreive associated value from"               } }
	,	{ Flag::Table   , { .short_name='t' , .has_arg=true , .doc="table storing code-value associations"                } }
	,	{ Flag::Context , { .short_name='x' , .has_arg=true , .doc="context used within file to retreive value from code" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	//
	if (!cmd_line.flags[Flag::Code   ]) syntax.usage("must have code to retrieve associated value"   ) ;
	if (!cmd_line.flags[Flag::Table  ]) syntax.usage("must have file to retrieve associated value"   ) ;
	if (!cmd_line.flags[Flag::Context]) syntax.usage("must have context to retrieve associated value") ;
	//
	try {
		auto&    fa    = cmd_line.flag_args                                                                                   ;
		::string reply = JobSupport::decode( ::move(fa[+Flag::Table]) , ::move(fa[+Flag::Context]) , ::move(fa[+Flag::Code]) ) ;
		Fd::Stdout.write(reply) ;
	} catch (::string const& e) {
		exit(Rc::Fail,e) ;
	}
}
