// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "caches/dir_cache.hh"

using namespace Caches ;

enum class Key  : uint8_t { None   } ;
enum class Flag : uint8_t { DryRun } ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::DryRun , { .short_name='n' , .doc="report actions but dont execute them" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	if (cmd_line.args.size()<1) syntax.usage("must provide a cache dir to repair") ;
	if (cmd_line.args.size()>1) syntax.usage("cannot repair several cache dirs"  ) ;
	//
	DirCache cache ;
	//
	try                       { cache.config( {{"dir",with_slash(cmd_line.args[0])}} ) ; }
	catch (::string const& e) { exit(Rc::Fail,"cannot configure cache : ",e) ;           }
	//
	cache.repair(cmd_line.flags[Flag::DryRun]) ;
}
