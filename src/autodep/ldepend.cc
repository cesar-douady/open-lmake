// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "support.hh"

#include "rpc_job.hh"

using namespace Disk ;

ENUM(Key,None)
ENUM(Flag
,	NoFollow
,	Critical
,	Essential
,	IgnoreError
,	NoRequired
,	Verbose
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::Verbose  , { .short_name='v' , .has_arg=false , .doc="write dep crcs on stdout"                    } }
	,	{ Flag::NoFollow , { .short_name='P' , .has_arg=false , .doc="Physical view, do not follow symolic links"  } }
	//
	,	{ Flag::Critical    , { .short_name=DflagChars[+Dflag::Critical   ] , .has_arg=false , .doc="report critical deps"                    } }
	,	{ Flag::Essential   , { .short_name=DflagChars[+Dflag::Essential  ] , .has_arg=false , .doc="ask that deps be seen in graphical flow" } }
	,	{ Flag::IgnoreError , { .short_name=DflagChars[+Dflag::IgnoreError] , .has_arg=false , .doc="accept that deps are in error"           } }
	,	{ Flag::NoRequired  , { .short_name=DflagChars[+Dflag::Required   ] , .has_arg=false , .doc="accept that deps cannot be built"        } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	//
	if (!cmd_line.args) return 0 ;                                                           // fast path : depends on nothing
	for( ::string const& f : cmd_line.args ) if (!f) exit(2,"cannot depend on empty file") ;
	//
	bool              verbose   = cmd_line.flags[Flag::Verbose ] ;
	bool              no_follow = cmd_line.flags[Flag::NoFollow] ;
	Dflags            dflags    = DfltDflags                    ;
	if (cmd_line.flags[Flag::Critical   ]) dflags |=  Dflag::Critical    ;
	if (cmd_line.flags[Flag::Essential  ]) dflags |=  Dflag::Essential   ;
	if (cmd_line.flags[Flag::IgnoreError]) dflags |=  Dflag::IgnoreError ;
	if (cmd_line.flags[Flag::NoRequired ]) dflags &= ~Dflag::Required    ;
	//
	if (verbose) {
		JobExecRpcReq   jerr  = JobExecRpcReq( JobExecRpcProc::DepInfos , ::move(cmd_line.args) , Accesses::All , dflags , no_follow , "ldepend" ) ;
		JobExecRpcReply reply = AutodepSupport(New).req(jerr)                                                                                     ;
		//
		SWEAR( reply.infos.size()==jerr.files.size() , reply.infos.size() , jerr.files.size() ) ;
		//
		bool err = false ;
		for( size_t i=0 ; i<reply.infos.size() ; i++ ) {
			switch (reply.infos[i].first) {
				case Yes   : ::cout << "ok  " ;              break ;
				case Maybe : ::cout << "??? " ; err = true ; break ;
				case No    : ::cout << "err " ; err = true ; break ;
				default : FAIL(reply.infos[i].first) ;
			}
			::cout << ::string(reply.infos[i].second) <<' '<< jerr.files[i].first <<'\n' ;
		}
		//
		return err ? 1 : 0 ;
	} else {
		AutodepSupport(New).req( JobExecRpcReq( JobExecRpcProc::Access , ::move(cmd_line.args) , {.accesses=Accesses::All,.dflags=dflags} , no_follow , "ldepend" ) ) ;
		return 0 ;
	}
}
