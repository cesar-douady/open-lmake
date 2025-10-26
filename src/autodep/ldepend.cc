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

enum class Key : uint8_t { None } ;

enum class Flag : uint8_t {
	Dir
,	FollowSymlinks
,	List
,	Read
,	Regexpr
//
,	Critical
,	Direct
,	Essential
,	Ignore
,	IgnoreError
,	NoExcludeStar
,	NoRequired
,	ReaddirOk
,	Verbose
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::Dir            , { .short_name='z' , .has_arg=true , .doc="dir in which to list deps"             } }
	,	{ Flag::FollowSymlinks , { .short_name='L' ,                 .doc="Logical view, follow symolic links"    } }
	,	{ Flag::List           , { .short_name='l' ,                 .doc="list deps"                             } }
	,	{ Flag::Read           , { .short_name='R' ,                 .doc="report a read"                         } }
	,	{ Flag::Regexpr        , { .short_name='X' ,                 .doc="args are regexprs"                     } }
	,	{ Flag::Direct         , { .short_name='d' ,                 .doc="suspend job until deps are up-to-date" } }
	//
	,	{ Flag::Critical      , { .short_name=DflagChars     [+Dflag     ::Critical   ].second , .doc="report critical deps"                    } }
	,	{ Flag::Essential     , { .short_name=DflagChars     [+Dflag     ::Essential  ].second , .doc="ask that deps be seen in graphical flow" } }
	,	{ Flag::IgnoreError   , { .short_name=DflagChars     [+Dflag     ::IgnoreError].second , .doc="ignore if deps are in error"             } }
	,	{ Flag::Ignore        , { .short_name=ExtraDflagChars[+ExtraDflag::Ignore     ].second , .doc="ignore deps"                             } }
	,	{ Flag::NoExcludeStar , { .short_name=ExtraDflagChars[+ExtraDflag::NoStar     ].second , .doc="accept regexpr-based flags"              } }
	,	{ Flag::NoRequired    , { .short_name=DflagChars     [+Dflag     ::Required   ].second , .doc="ignore if deps cannot be built"          } }
	,	{ Flag::ReaddirOk     , { .short_name=ExtraDflagChars[+ExtraDflag::ReaddirOk  ].second , .doc="allow readdir"                           } }
	,	{ Flag::Verbose       , { .short_name=DflagChars     [+Dflag     ::Verbose    ].second , .doc="write dep checksums on stdout"           } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv } ;
	Rc                rc       = Rc::Ok                 ;
	::string          out      ;
	//
	if (cmd_line.flags[Flag::List]) {
		//
		if ( cmd_line.args.size() > cmd_line.flags[Flag::Regexpr]                    ) syntax.usage("cannot list deps with args other than a single regexpr"                     ) ;
		if ( +( cmd_line.flags & ~BitMap<Flag>(Flag::Dir,Flag::List,Flag::Regexpr) ) ) syntax.usage("the --list flag is exclusive with any other flag except --dir and --regexpr") ;
		//
		::optional_s dir     ; if ( cmd_line.flags[Flag::Dir]                       ) dir     = cmd_line.flag_args[+Flag::Dir] ;
		::optional_s regexpr ; if ( cmd_line.flags[Flag::Regexpr] && +cmd_line.args ) regexpr = cmd_line.args[0]               ;
		//
		try                       { for( ::string const& d : JobSupport::list( No/*write*/ , ::move(dir) , ::move(regexpr) ) ) out << d <<'\n' ; }
		catch (::string const& e) { exit(Rc::System,e) ;                                                                                         }
		//
	} else {
		//
		if (!cmd_line.args) return 0 ;                                                                 // fast path : depends on nothing
		for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot depend on empty file") ;
		//
		AccessDigest ad      { .flags{.dflags=DflagsDfltDepend,.extra_dflags=ExtraDflagsDfltDepend} } ;
		bool         verbose = cmd_line.flags[Flag::Verbose]                                          ;
		bool         direct  = cmd_line.flags[Flag::Direct ]                                          ;
		//
		if (cmd_line.flags[Flag::Read         ]) ad.accesses            = DataAccesses             ;
		if (cmd_line.flags[Flag::Critical     ]) ad.flags.dflags       |=  Dflag     ::Critical    ;
		if (cmd_line.flags[Flag::Essential    ]) ad.flags.dflags       |=  Dflag     ::Essential   ;
		if (cmd_line.flags[Flag::Ignore       ]) ad.flags.extra_dflags |=  ExtraDflag::Ignore      ;
		if (cmd_line.flags[Flag::IgnoreError  ]) ad.flags.dflags       |=  Dflag     ::IgnoreError ;
		if (cmd_line.flags[Flag::NoRequired   ]) ad.flags.dflags       &= ~Dflag     ::Required    ;
		if (cmd_line.flags[Flag::ReaddirOk    ]) ad.flags.extra_dflags |=  ExtraDflag::ReaddirOk   ;
		if (cmd_line.flags[Flag::NoExcludeStar]) ad.flags.extra_dflags &= ~ExtraDflag::NoStar      ;
		if (verbose                            ) ad.flags.dflags       |=  Dflag     ::Verbose     ;
		::pair<::vector<VerboseInfo>,bool/*ok*/> dep_infos ;
		try                       { dep_infos = JobSupport::depend( ::copy(cmd_line.args) , ad , !cmd_line.flags[Flag::FollowSymlinks] , cmd_line.flags[Flag::Regexpr] , direct ) ; }
		catch (::string const& e) { exit(Rc::Usage,e) ;                                                                                                                             }
		//
		if (direct) {
			rc = dep_infos.second ? Rc::Ok : Rc::Fail ;
		} else if (verbose) {
			SWEAR( dep_infos.first.size()==cmd_line.args.size() , dep_infos.first.size() , cmd_line.args.size() ) ;
			auto ok_str = [](Bool3 ok)->const char* {
				switch (ok) {
					case Yes   : return "ok"    ;
					case Maybe : return "???"   ;
					case No    : return "error" ;
				DF}                                                                                    // NO_COV
			} ;
			size_t w_ok  = ::max<size_t>( dep_infos.first , [&](VerboseInfo vi) { return ::strlen(ok_str(vi.ok)) ; } ) ;
			size_t w_crc = ::max<size_t>( dep_infos.first , [&](VerboseInfo vi) { return ::string(vi.crc).size() ; } ) ;
			for( size_t i : iota(dep_infos.first.size()) ) {
				VerboseInfo vi = dep_infos.first[i] ;
				if (vi.ok!=Yes) rc = Rc::Fail ;
				out <<      widen(ok_str(vi.ok)   ,w_ok ) ;
				out <<' '<< widen(::string(vi.crc),w_crc) ;
				out <<' '<< cmd_line.args[i]              ;
				out <<'\n'                                ;
			}
			if (cmd_line.flags[Flag::IgnoreError]) rc = Rc::Ok ;
		}
	}
	if (+out) Fd::Stdout.write(out) ;
	exit(rc) ;
}
