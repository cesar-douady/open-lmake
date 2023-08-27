// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "autodep_support.hh"

ENUM(Key,None)
ENUM(Flag
,	Unlink
,	NoFollow
,	Crc                                // generate a crc for this target (compulsery if Match)
,	Dep                                // reads not followed by writes trigger dependencies
,	Essential                          // show when generating user oriented graphs
,	ManualOk                           // ok to overwrite manual files
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
		{ Flag::Unlink      , { .short_name='u' , .has_arg=false , .doc="report an unlink"                                                      } }
	,	{ Flag::NoFollow    , { .short_name='P' , .has_arg=false , .doc="Physical view, do noe follow symbolic links."                          } }
	,	{ Flag::Crc         , { .short_name='c' , .has_arg=false , .doc="generate a crc for this target (compulsery if Match)"                  } }
	,	{ Flag::Dep         , { .short_name='d' , .has_arg=false , .doc="reads not followed by writes trigger dependencies"                     } }
	,	{ Flag::Essential   , { .short_name='e' , .has_arg=false , .doc="show when generating user oriented graphs"                             } }
	,	{ Flag::ManualOk    , { .short_name='m' , .has_arg=false , .doc="ok to overwrite manual files"                                          } }
	,	{ Flag::Phony       , { .short_name='f' , .has_arg=false , .doc="phony, file is deemed to exist even if unlinked"                       } }
	,	{ Flag::SourceOk    , { .short_name='s' , .has_arg=false , .doc="ok to overwrite source files"                                          } }
	,	{ Flag::Stat        , { .short_name='t' , .has_arg=false , .doc="inode accesses (stat-like) are not ignored"                            } }
	,	{ Flag::Write       , { .short_name='w' , .has_arg=false , .doc="writes are allowed (possibly followed by reads which are ignored)"     } }
	,	{ Flag::NoCrc       , { .short_name='C' , .has_arg=false , .doc="do not generate a crc for this target (compulsery if Match)"           } }
	,	{ Flag::NoDep       , { .short_name='D' , .has_arg=false , .doc="reads not followed by writes do not trigger dependencies"              } }
	,	{ Flag::NoEssential , { .short_name='E' , .has_arg=false , .doc="do not show when generating user oriented graphs"                      } }
	,	{ Flag::NoManualOk  , { .short_name='M' , .has_arg=false , .doc="not ok to overwrite manual files"                                      } }
	,	{ Flag::NoPhony     , { .short_name='F' , .has_arg=false , .doc="not phony, file is not deemed to exist if unlinked"                    } }
	,	{ Flag::NoSourceOk  , { .short_name='S' , .has_arg=false , .doc="not ok to overwrite source files"                                      } }
	,	{ Flag::NoStat      , { .short_name='T' , .has_arg=false , .doc="inode accesses (stat-like) are ignored"                                } }
	,	{ Flag::NoWrite     , { .short_name='W' , .has_arg=false , .doc="writes are not allowed (possibly followed by reads which are ignored)" } }
	}} ;
	CmdLine<Key,Flag> cmd_line  { syntax,argc,argv } ;
	bool              unlink    = false              ;
	TFlags            neg_flags ;
	TFlags            pos_flags ;
	//
	if (cmd_line.flags[Flag::Unlink]) unlink = true ;
	//
	if (cmd_line.flags[Flag::Crc        ]) pos_flags |= TFlag::Crc       ;
	if (cmd_line.flags[Flag::Dep        ]) pos_flags |= TFlag::Dep       ;
	if (cmd_line.flags[Flag::Essential  ]) pos_flags |= TFlag::Essential ;
	if (cmd_line.flags[Flag::ManualOk   ]) pos_flags |= TFlag::ManualOk  ;
	if (cmd_line.flags[Flag::Phony      ]) pos_flags |= TFlag::Phony     ;
	if (cmd_line.flags[Flag::SourceOk   ]) pos_flags |= TFlag::SourceOk  ;
	if (cmd_line.flags[Flag::Stat       ]) pos_flags |= TFlag::Stat      ;
	if (cmd_line.flags[Flag::Write      ]) pos_flags |= TFlag::Write     ;
	//
	if (cmd_line.flags[Flag::NoCrc      ]) neg_flags |= TFlag::Crc       ;
	if (cmd_line.flags[Flag::NoDep      ]) neg_flags |= TFlag::Dep       ;
	if (cmd_line.flags[Flag::NoEssential]) neg_flags |= TFlag::Essential ;
	if (cmd_line.flags[Flag::NoManualOk ]) neg_flags |= TFlag::ManualOk  ;
	if (cmd_line.flags[Flag::NoPhony    ]) neg_flags |= TFlag::Phony     ;
	if (cmd_line.flags[Flag::NoSourceOk ]) neg_flags |= TFlag::SourceOk  ;
	if (cmd_line.flags[Flag::NoStat     ]) neg_flags |= TFlag::Stat      ;
	if (cmd_line.flags[Flag::NoWrite    ]) neg_flags |= TFlag::Write     ;
	//
	if ( +(neg_flags&pos_flags)             ) syntax.usage(to_string("cannot set and reset flags simultaneously : ",neg_flags&pos_flags)) ;
	if ( unlink && (+neg_flags||+pos_flags) ) syntax.usage(          "cannot unlink and set/reset flags"s                               ) ;
	//
	JobExecRpcReply reply = AutodepSupport(New).req( JobExecRpcReq(
		::move(cmd_line.args)
	,	{.write=!unlink,.neg_tfs=neg_flags,.pos_tfs=pos_flags,.unlink=unlink}
	,	cmd_line.flags[Flag::NoFollow]
	,	"ltarget"
	) ) ;
	//
	return 0 ;
}
