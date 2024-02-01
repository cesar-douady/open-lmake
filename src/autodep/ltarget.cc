// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "record.hh"

#include "rpc_job.hh"

ENUM(Key,None)
ENUM(Flag
,	Unlink
,	NoFollow
,	Essential
,	Incremental
,	ManualOk
,	NoUniquify
,	NoWarning
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::Unlink      , { .short_name='U'                             , .has_arg=false , .doc="report an unlink"                                                       } }
	,	{ Flag::NoFollow    , { .short_name='P'                             , .has_arg=false , .doc="Physical view, do not follow symbolic links."                           } }
	//
	,	{ Flag::Essential   , { .short_name=TflagChars[+Tflag::Essential  ] , .has_arg=false , .doc="show when generating user oriented graphs"                              } }
	,	{ Flag::Incremental , { .short_name=TflagChars[+Tflag::Incremental] , .has_arg=false , .doc="do not rm file before job execution"                                    } }
	,	{ Flag::ManualOk    , { .short_name=TflagChars[+Tflag::ManualOk   ] , .has_arg=false , .doc="accept if target has been manually modified"                            } }
	,	{ Flag::NoUniquify  , { .short_name=TflagChars[+Tflag::NoUniquify ] , .has_arg=false , .doc="do not uniquify target if incremental and several links point to it"    } }
	,	{ Flag::NoWarning   , { .short_name=TflagChars[+Tflag::NoWarning  ] , .has_arg=false , .doc="do not warn user if uniquified or rm'ed while generated by another job" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	//
	if (!cmd_line.args) return 0 ;                                                                                               // fast path : declare no targets
	for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot declare empty file as target") ;
	//
	AccessDigest ad        ;
	bool         no_follow = cmd_line.flags[Flag::NoFollow] ;
	//
	ad.unlink = cmd_line.flags[Flag::Unlink  ] ;
	//
	if (cmd_line.flags[Flag::Essential  ]) ad.tflags |= Tflag::Essential   ;
	if (cmd_line.flags[Flag::Incremental]) ad.tflags |= Tflag::Incremental ;
	if (cmd_line.flags[Flag::ManualOk   ]) ad.tflags |= Tflag::ManualOk    ;
	if (cmd_line.flags[Flag::NoUniquify ]) ad.tflags |= Tflag::NoUniquify  ;
	if (cmd_line.flags[Flag::NoWarning  ]) ad.tflags |= Tflag::NoWarning   ;
	//
	ad.write = !ad.unlink ;
	Record record{New} ;
	record.direct(JobExecRpcReq( JobExecRpcProc::Access  , ::move(cmd_line.args) , ad , no_follow , false/*sync*/ , true/*ok*/ , "ltarget" )) ; // ok=true to signal it is ok to write to
	record.direct(JobExecRpcReq( JobExecRpcProc::Confirm , false/*unlink*/ , true/*ok*/                                                    )) ;
	//
	return 0 ;
}
