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
,	FollowSymlinks
,	Verbose
,	NoRead
,	Essential
,	Critical
,	IgnoreError
,	NoRequired
,	Ignore
,	StatReadData
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::FollowSymlinks , { .short_name='L' , .has_arg=false , .doc="Logical view, follow symolic links" } }
	,	{ Flag::Verbose        , { .short_name='v' , .has_arg=false , .doc="write dep crcs on stdout"           } }
	,	{ Flag::NoRead         , { .short_name='R' , .has_arg=false , .doc="does not report a read, only flags" } }
	//
	,	{ Flag::Critical     , { .short_name=DflagChars     [+Dflag     ::Critical    ] , .has_arg=false , .doc="report critical deps"                            } }
	,	{ Flag::Essential    , { .short_name=DflagChars     [+Dflag     ::Essential   ] , .has_arg=false , .doc="ask that deps be seen in graphical flow"         } }
	,	{ Flag::IgnoreError  , { .short_name=DflagChars     [+Dflag     ::IgnoreError ] , .has_arg=false , .doc="accept that deps are in error"                   } }
	,	{ Flag::NoRequired   , { .short_name=DflagChars     [+Dflag     ::Required    ] , .has_arg=false , .doc="accept that deps cannot be built"                } }
	,	{ Flag::Ignore       , { .short_name=ExtraDflagChars[+ExtraDflag::Ignore      ] , .has_arg=false , .doc="ignore reads"                                    } }
	,	{ Flag::StatReadData , { .short_name=ExtraDflagChars[+ExtraDflag::StatReadData] , .has_arg=false , .doc="stat access implies access to full file content" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv } ;
	//
	if (!cmd_line.args) return 0 ;                                                                 // fast path : depends on nothing
	for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot depend on empty file") ;
	//
	bool         no_follow = !cmd_line.flags[Flag::FollowSymlinks] ;
	bool         verbose   =  cmd_line.flags[Flag::Verbose       ] ;
	AccessDigest ad        = { .accesses=~Accesses() }             ;
	bool         err       = false                                 ;
	if ( cmd_line.flags[Flag::NoRead      ]) ad.accesses      = {}                       ;
	if ( cmd_line.flags[Flag::Critical    ]) ad.dflags       |= Dflag     ::Critical     ;
	if ( cmd_line.flags[Flag::Essential   ]) ad.dflags       |= Dflag     ::Essential    ;
	if ( cmd_line.flags[Flag::IgnoreError ]) ad.dflags       |= Dflag     ::IgnoreError  ;
	if (!cmd_line.flags[Flag::NoRequired  ]) ad.dflags       |= Dflag     ::Required     ;
	if ( cmd_line.flags[Flag::Ignore      ]) ad.extra_dflags |= ExtraDflag::Ignore       ;
	if ( cmd_line.flags[Flag::StatReadData]) ad.extra_dflags |= ExtraDflag::StatReadData ;
	//
	if (verbose) {
		JobExecRpcReply reply = Record(New).direct(JobExecRpcReq( JobExecRpcProc::DepInfos , ::copy(cmd_line.args) , ad , no_follow , true/*sync*/ , "ldepend" )) ;
		//
		SWEAR( reply.dep_infos.size()==cmd_line.args.size() , reply.dep_infos.size() , cmd_line.args.size() ) ;
		//
		for( size_t i=0 ; i<reply.dep_infos.size() ; i++ ) {
			switch (reply.dep_infos[i].first) {
				case Yes   : ::cout << "ok  " ;              break ;
				case Maybe : ::cout << "??? " ; err = true ; break ;
				case No    : ::cout << "err " ; err = true ; break ;
			DF}
			::cout << ::string(reply.dep_infos[i].second) <<' '<< cmd_line.args[i] <<'\n' ;
		}
		//
	} else {
		Record(New,Yes/*enable*/).direct(JobExecRpcReq( JobExecRpcProc::Access , ::move(cmd_line.args) , ad , no_follow , "ldepend" )) ;
	}
	return err ? 1 : 0 ;
}
