// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "job_support.hh"
#include "record.hh"

ENUM(Key,None)
ENUM(Flag
,	Sync
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::Sync , { .short_name='s' , .has_arg=false , .doc="wait for server reply that previous deps are up-to-date with no error" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv }                                ; if (cmd_line.args.size()!=0 ) syntax.usage("must have no argument") ;
	bool              sync     = cmd_line.flags[Flag::Sync]                            ;
	Bool3             ok       = JobSupport::check_deps( {New,Yes/*enabled*/} , sync ) ;
	if (!sync) return 0       ;
	else       return ok!=Yes ;
}
