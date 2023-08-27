// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "autodep_support.hh"

ENUM(Key,None)
ENUM(Flag
,	NoFollow
,	Critical
,	NoError
,	Essential
,	Required
,	Verbose
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::NoFollow  , { .short_name='P' , .has_arg=false , .doc="Physical view, do not follow symolic links"  } }
	,	{ Flag::Critical  , { .short_name='c' , .has_arg=false , .doc="report critical deps"                        } }
	,	{ Flag::NoError   , { .short_name='e' , .has_arg=false , .doc="accept that deps are in error"               } }
	,	{ Flag::Required  , { .short_name='r' , .has_arg=false , .doc="accept that deps cannot be generated"        } }
	,	{ Flag::Essential , { .short_name='s' , .has_arg=false , .doc="ask that deps not be seen in graphical flow" } }
	,	{ Flag::Verbose   , { .short_name='v' , .has_arg=false , .doc="write dep crcs on stdout"                    } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv }            ;
	bool              verbose  = cmd_line.flags[Flag::Verbose] ;
	DFlags            flags    = DfltDFlags                    ;
	if (cmd_line.flags[Flag::Critical ]) flags |= DFlag::Critical    ;
	if (cmd_line.flags[Flag::NoError  ]) flags |= DFlag::IgnoreError ;
	if (cmd_line.flags[Flag::Required ]) flags |= DFlag::Required    ;
	if (cmd_line.flags[Flag::Essential]) flags |= DFlag::Essential   ;
	//
	if (verbose) {
		JobExecRpcReq   jerr  = JobExecRpcReq( JobExecRpcProc::DepInfos , ::move(cmd_line.args) , flags ) ;
		JobExecRpcReply reply = AutodepSupport(New).req(jerr)                                             ;
		//
		SWEAR(reply.infos.size()==jerr.files.size()) ;
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
		AutodepSupport(New).req( JobExecRpcReq( ::move(cmd_line.args) , {.dfs=flags} , cmd_line.flags[Flag::NoFollow] , "ldepend") ) ;
		return 0 ;
	}
}
