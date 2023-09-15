// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "autodep_support.hh"

#include "rpc_job.hh"

ENUM(Key,None)
ENUM(Flag
,	Unlink
,	NoFollow
,	Crc                                // generate a crc for this target (compulsery if Match)
,	Dep                                // reads not followed by writes trigger dependencies
,	Essential                          // show when generating user oriented graphs
,	Phony                              // unlinks are allowed (possibly followed by reads which are ignored)
,	SourceOk                           // ok to overwrite source files
,	Stat                               // inode accesses (stat-like) are not ignored
,	Write                              // writes are allowed (possibly followed by reads which are ignored)
,	NoCrc
,	NoDep
,	NoEssential
,	NoManualOk
,	NoPhony
,	NoSourceOk
,	NoStat
,	NoWrite
)

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax{{
		{ Flag::Unlink      , { .short_name='u'                                            , .has_arg=false , .doc="report an unlink"                                                      } }
	,	{ Flag::NoFollow    , { .short_name='P'                                            , .has_arg=false , .doc="Physical view, do not follow symbolic links."                          } }
	//
	,	{ Flag::Crc         , { .short_name=               TFlagChars[+TFlag::Crc      ]   , .has_arg=false , .doc="generate a crc for this target (compulsery if Match)"                  } }
	,	{ Flag::Dep         , { .short_name=               TFlagChars[+TFlag::Dep      ]   , .has_arg=false , .doc="reads not followed by writes trigger dependencies"                     } }
	,	{ Flag::Essential   , { .short_name=               TFlagChars[+TFlag::Essential]   , .has_arg=false , .doc="show when generating user oriented graphs"                             } }
	,	{ Flag::Phony       , { .short_name=               TFlagChars[+TFlag::Phony    ]   , .has_arg=false , .doc="phony, file is deemed to exist even if unlinked"                       } }
	,	{ Flag::SourceOk    , { .short_name=               TFlagChars[+TFlag::SourceOk ]   , .has_arg=false , .doc="ok to overwrite source files"                                          } }
	,	{ Flag::Stat        , { .short_name=               TFlagChars[+TFlag::Stat     ]   , .has_arg=false , .doc="inode accesses (stat-like) are not ignored"                            } }
	,	{ Flag::Write       , { .short_name=               TFlagChars[+TFlag::Write    ]   , .has_arg=false , .doc="writes are allowed (possibly followed by reads which are ignored)"     } }
	,	{ Flag::NoCrc       , { .short_name=char(::toupper(TFlagChars[+TFlag::Crc      ])) , .has_arg=false , .doc="do not generate a crc for this target (compulsery if Match)"           } }
	,	{ Flag::NoDep       , { .short_name=char(::toupper(TFlagChars[+TFlag::Dep      ])) , .has_arg=false , .doc="reads not followed by writes do not trigger dependencies"              } }
	,	{ Flag::NoEssential , { .short_name=char(::toupper(TFlagChars[+TFlag::Essential])) , .has_arg=false , .doc="do not show when generating user oriented graphs"                      } }
	,	{ Flag::NoPhony     , { .short_name=char(::toupper(TFlagChars[+TFlag::Phony    ])) , .has_arg=false , .doc="not phony, file is not deemed to exist if unlinked"                    } }
	,	{ Flag::NoSourceOk  , { .short_name=char(::toupper(TFlagChars[+TFlag::SourceOk ])) , .has_arg=false , .doc="not ok to overwrite source files"                                      } }
	,	{ Flag::NoStat      , { .short_name=char(::toupper(TFlagChars[+TFlag::Stat     ])) , .has_arg=false , .doc="inode accesses (stat-like) are ignored"                                } }
	,	{ Flag::NoWrite     , { .short_name=char(::toupper(TFlagChars[+TFlag::Write    ])) , .has_arg=false , .doc="writes are not allowed (possibly followed by reads which are ignored)" } }
	}} ;
	CmdLine<Key,Flag> cmd_line   { syntax,argc,argv } ;
	bool              unlink     = false              ;
	bool              no_follow  = false              ;
	TFlags            neg_tflags ;
	TFlags            pos_tflags ;
	//
	if (cmd_line.flags[Flag::Unlink  ]) unlink    = true ;
	if (cmd_line.flags[Flag::NoFollow]) no_follow = true ;
	//
	if (cmd_line.flags[Flag::Crc        ]) pos_tflags |= TFlag::Crc       ;
	if (cmd_line.flags[Flag::Dep        ]) pos_tflags |= TFlag::Dep       ;
	if (cmd_line.flags[Flag::Essential  ]) pos_tflags |= TFlag::Essential ;
	if (cmd_line.flags[Flag::Phony      ]) pos_tflags |= TFlag::Phony     ;
	if (cmd_line.flags[Flag::SourceOk   ]) pos_tflags |= TFlag::SourceOk  ;
	if (cmd_line.flags[Flag::Stat       ]) pos_tflags |= TFlag::Stat      ;
	if (cmd_line.flags[Flag::Write      ]) pos_tflags |= TFlag::Write     ;
	//
	if (cmd_line.flags[Flag::NoCrc      ]) neg_tflags |= TFlag::Crc       ;
	if (cmd_line.flags[Flag::NoDep      ]) neg_tflags |= TFlag::Dep       ;
	if (cmd_line.flags[Flag::NoEssential]) neg_tflags |= TFlag::Essential ;
	if (cmd_line.flags[Flag::NoPhony    ]) neg_tflags |= TFlag::Phony     ;
	if (cmd_line.flags[Flag::NoSourceOk ]) neg_tflags |= TFlag::SourceOk  ;
	if (cmd_line.flags[Flag::NoStat     ]) neg_tflags |= TFlag::Stat      ;
	if (cmd_line.flags[Flag::NoWrite    ]) neg_tflags |= TFlag::Write     ;
	//
	if ( +(neg_tflags&pos_tflags)             ) syntax.usage(to_string("cannot set and reset flags simultaneously : ",neg_tflags&pos_tflags)) ;
	if ( unlink && (+neg_tflags||+pos_tflags) ) syntax.usage(          "cannot unlink and set/reset flags"s                                 ) ;
	//
	JobExecRpcReply reply = AutodepSupport(New).req( JobExecRpcReq(
		JobExecRpcProc::Access
	,	::move(cmd_line.args)
	,	{.write=!unlink,.neg_tflags=neg_tflags,.pos_tflags=pos_tflags,.unlink=unlink}
	,	no_follow
	,	"ltarget"
	) ) ;
	//
	return 0 ;
}
