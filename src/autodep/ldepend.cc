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
	FollowSymlinks
,	Verbose
,	Read
,	ReaddirOk
,	Regexpr
,	Essential
,	Critical
,	IgnoreError
,	NoExcludeStar
,	NoRequired
,	Ignore
} ;

int main( int argc , char* argv[]) {
	Syntax<Key,Flag> syntax {{
		{ Flag::FollowSymlinks , { .short_name='L' , .doc="Logical view, follow symolic links" } }
	,	{ Flag::Verbose        , { .short_name='v' , .doc="write dep crcs on stdout"           } }
	,	{ Flag::Read           , { .short_name='R' , .doc="report a read"                      } }
	,	{ Flag::Regexpr        , { .short_name='X' , .doc="args are regexprs"                  } }
	//
	,	{ Flag::Critical      , { .short_name=DflagChars     [+Dflag     ::Critical   ].second , .doc="report critical deps"                    } }
	,	{ Flag::Essential     , { .short_name=DflagChars     [+Dflag     ::Essential  ].second , .doc="ask that deps be seen in graphical flow" } }
	,	{ Flag::IgnoreError   , { .short_name=DflagChars     [+Dflag     ::IgnoreError].second , .doc="ignore if deps are in error"             } }
	,	{ Flag::NoExcludeStar , { .short_name=ExtraDflagChars[+ExtraDflag::NoStar     ].second , .doc="accept regexpr based flags"              } }
	,	{ Flag::NoRequired    , { .short_name=DflagChars     [+Dflag     ::Required   ].second , .doc="ignore if deps cannot be built"          } }
	,	{ Flag::Ignore        , { .short_name=ExtraDflagChars[+ExtraDflag::Ignore     ].second , .doc="ignore deps"                             } }
	,	{ Flag::ReaddirOk     , { .short_name=ExtraDflagChars[+ExtraDflag::ReaddirOk  ].second , .doc="allow readdir"                           } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv } ;
	//
	if (!cmd_line.args) return 0 ;                                                                 // fast path : depends on nothing
	for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot depend on empty file") ;
	//
	bool         no_follow = !cmd_line.flags[Flag::FollowSymlinks]                                  ;
	bool         verbose   =  cmd_line.flags[Flag::Verbose       ]                                  ;
	AccessDigest ad        { .flags{.dflags=DflagsDfltDepend,.extra_dflags=ExtraDflagsDfltDepend} } ;
	//
	if (cmd_line.flags[Flag::Read          ]) ad.accesses            = ~Accesses()              ;
	//
	if (cmd_line.flags[Flag::Critical      ]) ad.flags.dflags       |=  Dflag     ::Critical    ;
	if (cmd_line.flags[Flag::Essential     ]) ad.flags.dflags       |=  Dflag     ::Essential   ;
	if (cmd_line.flags[Flag::Ignore        ]) ad.flags.extra_dflags |=  ExtraDflag::Ignore      ;
	if (cmd_line.flags[Flag::IgnoreError   ]) ad.flags.dflags       |=  Dflag     ::IgnoreError ;
	if (cmd_line.flags[Flag::NoRequired    ]) ad.flags.dflags       &= ~Dflag     ::Required    ;
	if (cmd_line.flags[Flag::ReaddirOk     ]) ad.flags.extra_dflags |=  ExtraDflag::ReaddirOk   ;
	if ( cmd_line.flags[Flag::NoExcludeStar]) ad.flags.extra_dflags &= ~ExtraDflag::NoStar      ;
	//
	::vector<DepVerboseInfo> dep_infos ;
	try                       { dep_infos = JobSupport::depend( {New,Yes/*enabled*/} , ::copy(cmd_line.args) , ad , no_follow , verbose , cmd_line.flags[Flag::Regexpr] ) ; }
	catch (::string const& e) { exit(Rc::Usage,e) ;                                                                                                                         }
	//
	if (!verbose) return 0 ;
	//
	SWEAR( dep_infos.size()==cmd_line.args.size() , dep_infos.size() , cmd_line.args.size() ) ;
	int      rc     = 0 ;
	::string out    ;
	size_t   w_ok   = 0 ;
	size_t   w_crc  = 3 ;
	size_t   w_rule = 0 ;
	for( DepVerboseInfo dvi : dep_infos ) {
		const char* ok_str ;
		switch (dvi.ok) {
			case Yes   : ok_str = "ok"    ; break ;
			case Maybe : ok_str = "???"   ; break ;
			case No    : ok_str = "error" ; break ;
		DF}                                                                                        // NO_COV
		/**/                   w_ok   = ::max( w_ok   , ::strlen(ok_str)                ) ;
		/**/                   w_crc  = ::max( w_crc  , ::string(dvi.crc)       .size() ) ;
		if      (+dvi.rule   ) w_rule = ::max( w_rule ,             dvi.rule    .size() ) ;
		else if (+dvi.special) w_rule = ::max( w_rule , ("special:"+dvi.special).size() ) ;
		else                   w_rule = ::max( w_rule , ::strlen("???")                 ) ;
	}
	for( size_t i : iota(dep_infos.size()) ) {
		DepVerboseInfo const& dvi = dep_infos[i] ;
		switch (dvi.ok) {
			case Yes   : out << widen("ok"   ,w_ok) ;          break ;
			case Maybe : out << widen("???"  ,w_ok) ; rc = 1 ; break ;
			case No    : out << widen("error",w_ok) ; rc = 1 ; break ;
		DF}                                                                                        // NO_COV
		/**/                   out <<' '<< widen(::string(dvi.crc),w_crc ) ;
		if      (+dvi.rule   ) out <<' '<< widen(dvi.rule         ,w_rule) ;
		else if (+dvi.special) out <<' '<< widen(dvi.special      ,w_rule) ;
		else                   out <<' '<< widen("???"            ,w_rule) ;
		/**/                   out <<' '<< cmd_line.args[i]                ;
		/**/                   out <<'\n'                                  ;
		size_t w_k = 0 ;
		for( auto const& [k,_] : dvi.stems ) w_k = ::max( w_k , k.size() ) ;
		for( auto const& [k,v] : dvi.stems ) out <<'\t'<< widen(k,w_k) <<' '<< v <<'\n' ;
	}
	Fd::Stdout.write(out) ;
	return rc ;
}
