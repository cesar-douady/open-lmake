// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "rpc_job.hh"

#include "job_support.hh"
#include "record.hh"

using namespace Disk ;

ENUM(Key,None)
ENUM(Flag
,	FollowSymlinks
,	Verbose
,	Read
,	Essential
,	Critical
,	IgnoreError
,	NoRequired
,	Ignore
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::FollowSymlinks , { .short_name='L' , .has_arg=false , .doc="Logical view, follow symolic links" } }
	,	{ Flag::Verbose        , { .short_name='v' , .has_arg=false , .doc="write dep crcs on stdout"           } }
	,	{ Flag::Read           , { .short_name='R' , .has_arg=false , .doc="report a read"                      } }
	//
	,	{ Flag::Critical    , { .short_name=DflagChars     [+Dflag     ::Critical   ].second , .has_arg=false , .doc="report critical deps"                    } }
	,	{ Flag::Essential   , { .short_name=DflagChars     [+Dflag     ::Essential  ].second , .has_arg=false , .doc="ask that deps be seen in graphical flow" } }
	,	{ Flag::Ignore      , { .short_name=ExtraDflagChars[+ExtraDflag::Ignore     ].second , .has_arg=false , .doc="ignore deps"                             } }
	,	{ Flag::IgnoreError , { .short_name=DflagChars     [+Dflag     ::IgnoreError].second , .has_arg=false , .doc="ignore if deps are in error"             } }
	,	{ Flag::NoRequired  , { .short_name=DflagChars     [+Dflag     ::Required   ].second , .has_arg=false , .doc="ignore if deps cannot be built"          } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv } ;
	//
	if (!cmd_line.args) return 0 ;                                                                 // fast path : depends on nothing
	for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot depend on empty file") ;
	//
	bool         no_follow = !cmd_line.flags[Flag::FollowSymlinks] ;
	bool         verbose   =  cmd_line.flags[Flag::Verbose       ] ;
	AccessDigest ad        { .dflags=DflagsDfltDepend }            ;
	if (cmd_line.flags[Flag::Critical   ]) ad.dflags       |=  Dflag     ::Critical    ;
	if (cmd_line.flags[Flag::Essential  ]) ad.dflags       |=  Dflag     ::Essential   ;
	if (cmd_line.flags[Flag::Ignore     ]) ad.extra_dflags |=  ExtraDflag::Ignore      ;
	if (cmd_line.flags[Flag::IgnoreError]) ad.dflags       |=  Dflag     ::IgnoreError ;
	if (cmd_line.flags[Flag::NoRequired ]) ad.dflags       &= ~Dflag     ::Required    ;
	if (cmd_line.flags[Flag::Read       ]) ad.accesses      = ~Accesses()              ;
	//
	::vector<pair<Bool3/*ok*/,Hash::Crc>> dep_infos ;
	try                       { dep_infos = JobSupport::depend( {New,Yes/*enabled*/} , ::copy(cmd_line.args) , ad , no_follow , verbose ) ; }
	catch (::string const& e) { exit(Rc::Usage,e) ;                                                                                         }
	//
	if (!verbose) return 0 ;
	//
	SWEAR( dep_infos.size()==cmd_line.args.size() , dep_infos.size() , cmd_line.args.size() ) ;
	int      rc  = 0 ;
	::string out ;
	for( size_t i : iota(dep_infos.size()) ) {
		switch (dep_infos[i].first) {
			case Yes   : out += "ok  " ;          break ;
			case Maybe : out += "??? " ; rc = 1 ; break ;
			case No    : out += "err " ; rc = 1 ; break ;
		DF}
		out << ::string(dep_infos[i].second) <<' '<< cmd_line.args[i] <<'\n' ;
	}
	Fd::Stdout.write(out) ;
	return rc ;
}
