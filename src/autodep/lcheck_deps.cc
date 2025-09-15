// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "time.hh"

#include "job_support.hh"
#include "record.hh"

using namespace Time ;

enum class Key : uint8_t { None } ;

enum class Flag : uint8_t {
	Delay
,	Sync
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::Delay , { .short_name='d' , .has_arg=true , .doc="delay after which to check for out-of-date/error deps"                 } }
	,	{ Flag::Sync  , { .short_name='s' ,                 .doc="wait for server reply that previous deps are up-to-date with no error" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv }                                                       ; if (cmd_line.args.size()!=0 ) syntax.usage("must have no argument") ;
	Delay             delay    { from_string<double>( cmd_line.flag_args[+Flag::Delay] , true/*empty_ok*/ ) } ;
	bool              sync     = cmd_line.flags[Flag::Sync]                                                   ;
	try {
		Bool3 ok = JobSupport::check_deps( {New,Yes/*enabled*/} , delay , sync ) ;
		if (!sync) return 0       ;
		else       return ok!=Yes ;
	} catch(::string const&e) {
		exit(Rc::System,e) ;
	}
}
