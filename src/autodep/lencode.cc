// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "disk.hh"
#include "hash.hh"

#include "rpc_job.hh"

#include "job_support.hh"
#include "record.hh"

using namespace Disk ;
using namespace Hash ;

enum class Key : uint8_t { None } ;

enum class Flag : uint8_t {
	File
,	Context
,	MinLen
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
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
	if (cmd_line.flags[Flag::MinLen]) {
		try                       { min_len = from_string<uint8_t>(cmd_line.flag_args[+Flag::MinLen]) ; }
		catch (::string const& e) { syntax.usage("bad min len value : "+e) ;                            }
	}
	//
	Py::init() ;
	//
	try {
		auto&    fa    = cmd_line.flag_args                                                                                      ;
		::string reply = JobSupport::encode( ::move(fa[+Flag::File]) , ::move(fa[+Flag::Context]) , Fd::Stdin.read() , min_len ) ;
		Fd::Stdout.write(::move(reply)+'\n') ;
	} catch (::string const& e) {
		exit(Rc::Fail,e) ;
	}
}
