// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "autodep_support.hh"

ENUM(Key,None)
ENUM(Flag
,	NoError
,	NoEssential
,	NoRequired
,	Verbose
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::NoError     , { .short_name='e' , .has_arg=false , .doc="accept that deps are in error"               } }
	,	{ Flag::NoRequired  , { .short_name='r' , .has_arg=false , .doc="accept that deps cannot be generated"        } }
	,	{ Flag::NoEssential , { .short_name='s' , .has_arg=false , .doc="ask that deps not be seen in graphical flow" } }
	,	{ Flag::Verbose     , { .short_name='v' , .has_arg=false , .doc="write dep crcs on stdout"                    } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv }            ;
	bool              verbose  = cmd_line.flags[Flag::Verbose] ;
	DFlags            flags    = StaticDFlags                  ;
	if (cmd_line.flags[Flag::NoError    ]) flags &= ~DFlag::Error     ;
	if (cmd_line.flags[Flag::NoRequired ]) flags &= ~DFlag::Required  ;
	if (cmd_line.flags[Flag::NoEssential]) flags &= ~DFlag::Essential ;
	//
	JobExecRpcReply reply = AutodepSupport(New).req( JobExecRpcReq( verbose?JobExecRpcProc::DepInfos:JobExecRpcProc::Deps , cmd_line.args , flags , verbose/*sync*/ ) ) ;
	//
	if (!verbose) return 0 ;
	SWEAR(reply.infos.size()==cmd_line.args.size()) ;
	//
	bool err = false ;
	for( size_t i=0 ; i<cmd_line.args.size() ; i++ ) {
		switch (reply.infos[i].first) {
			case Yes   : ::cout << "ok  " ;              break ;
			case Maybe : ::cout << "??? " ; err = true ; break ;
			case No    : ::cout << "err " ; err = true ; break ;
			default : FAIL(reply.infos[i].first) ;
		}
		::cout << ::string(reply.infos[i].second) <<' '<< cmd_line.args[i] <<'\n' ;
	}
	//
	return err ? 1 : 0 ;
}
