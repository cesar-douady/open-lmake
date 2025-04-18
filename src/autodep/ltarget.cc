// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "rpc_job.hh"

#include "job_support.hh"
#include "record.hh"

ENUM(Key,None)
ENUM(Flag
,	Write
,	Essential
,	Incremental
,	NoWarning
,	Phony
,	Ignore
,	NoAllow
,	SourceOk
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::Write , { .short_name='W' , .has_arg=false , .doc="report a write, in addition to flags" } }
	//
	,	{ Flag::Essential   , { .short_name=TflagChars     [+Tflag     ::Essential  ].second , .has_arg=false , .doc="show when generating user oriented graphs"                              } }
	,	{ Flag::Incremental , { .short_name=TflagChars     [+Tflag     ::Incremental].second , .has_arg=false , .doc="do not rm file before job execution"                                    } }
	,	{ Flag::NoWarning   , { .short_name=TflagChars     [+Tflag     ::NoWarning  ].second , .has_arg=false , .doc="do not warn user if uniquified or rm'ed while generated by another job" } }
	,	{ Flag::Phony       , { .short_name=TflagChars     [+Tflag     ::Phony      ].second , .has_arg=false , .doc="accept that target is not physically generated on disk"                 } }
	,	{ Flag::Ignore      , { .short_name=ExtraTflagChars[+ExtraTflag::Ignore     ].second , .has_arg=false , .doc="ignore writes"                                                          } }
	,	{ Flag::NoAllow     , { .short_name=ExtraTflagChars[+ExtraTflag::Allow      ].second , .has_arg=false , .doc="do not force target to be accepted, just inform writing to it"          } }
	,	{ Flag::SourceOk    , { .short_name=ExtraTflagChars[+ExtraTflag::SourceOk   ].second , .has_arg=false , .doc="accept if target is actually a source"                                  } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	//
	if (!cmd_line.args) return 0 ;                                                                            // fast path : declare no targets
	for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot declare empty file as target"   ) ;
	//
	AccessDigest ad { .write=No|cmd_line.flags[Flag::Write] } ;
	//
	if ( cmd_line.flags[Flag::Essential  ]) ad.tflags       |= Tflag     ::Essential   ;
	if ( cmd_line.flags[Flag::Incremental]) ad.tflags       |= Tflag     ::Incremental ;
	if ( cmd_line.flags[Flag::NoWarning  ]) ad.tflags       |= Tflag     ::NoWarning   ;
	if ( cmd_line.flags[Flag::Phony      ]) ad.tflags       |= Tflag     ::Phony       ;
	if ( cmd_line.flags[Flag::Ignore     ]) ad.extra_tflags |= ExtraTflag::Ignore      ;
	if (!cmd_line.flags[Flag::NoAllow    ]) ad.extra_tflags |= ExtraTflag::Allow       ;
	if ( cmd_line.flags[Flag::SourceOk   ]) ad.extra_tflags |= ExtraTflag::SourceOk    ;
	//
	try                       { JobSupport::target( {New,Yes/*enabled*/} , ::move(cmd_line.args) , ad ) ; }
	catch (::string const& e) { exit(Rc::Usage,e) ;                                                       }
	//
	return 0 ;
}
