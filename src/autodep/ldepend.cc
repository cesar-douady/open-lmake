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
		{ Flag::FollowSymlinks , { .short_name='L' , .doc="Logical view, follow symolic links" } }
	,	{ Flag::List           , { .short_name='l' , .doc="list deps"                          } }
	,	{ Flag::Read           , { .short_name='R' , .doc="report a read"                      } }
	,	{ Flag::Regexpr        , { .short_name='X' , .doc="args are regexprs"                  } }
	//
	,	{ Flag::Critical      , { .short_name=DflagChars     [+Dflag     ::Critical   ].second , .doc="report critical deps"                    } }
	,	{ Flag::Direct        , { .short_name=ExtraDflagChars[+ExtraDflag::Direct     ].second , .doc="suspend job until deps are up-to-date"   } }
	,	{ Flag::Essential     , { .short_name=DflagChars     [+Dflag     ::Essential  ].second , .doc="ask that deps be seen in graphical flow" } }
	,	{ Flag::IgnoreError   , { .short_name=DflagChars     [+Dflag     ::IgnoreError].second , .doc="ignore if deps are in error"             } }
	,	{ Flag::Ignore        , { .short_name=ExtraDflagChars[+ExtraDflag::Ignore     ].second , .doc="ignore deps"                             } }
	,	{ Flag::NoExcludeStar , { .short_name=ExtraDflagChars[+ExtraDflag::NoStar     ].second , .doc="accept regexpr-based flags"              } }
	,	{ Flag::NoRequired    , { .short_name=DflagChars     [+Dflag     ::Required   ].second , .doc="ignore if deps cannot be built"          } }
	,	{ Flag::ReaddirOk     , { .short_name=ExtraDflagChars[+ExtraDflag::ReaddirOk  ].second , .doc="allow readdir"                           } }
	,	{ Flag::Verbose       , { .short_name=DflagChars     [+Dflag     ::Verbose    ].second , .doc="write dep checksums on stdout"           } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax , argc , argv } ;
	int               rc       = 0                      ;
	::string          out      ;
	//
	if (cmd_line.flags[Flag::List]) {
		//
		if (+cmd_line.args             ) syntax.usage("cannot list deps with args"                         ) ;
		if ( cmd_line.flags!=Flag::List) syntax.usage("the --list/-l flag is exclusive with any other flag") ;
		::vector_s deps = JobSupport::list( {New,Yes/*enabled*/} , No/*write*/ ) ;
		for( ::string const& d : deps ) out << d <<'\n' ;
		//
	} else {
		//
		if (!cmd_line.args) return 0 ;                                                                 // fast path : depends on nothing
		for( ::string const& f : cmd_line.args ) if (!f) syntax.usage("cannot depend on empty file") ;
		//
		bool         no_follow = !cmd_line.flags[Flag::FollowSymlinks]                                  ;
		AccessDigest ad        { .flags{.dflags=DflagsDfltDepend,.extra_dflags=ExtraDflagsDfltDepend} } ;
		//
		if (cmd_line.flags[Flag::Read         ]) ad.accesses            = ~Accesses()              ;
		if (cmd_line.flags[Flag::Critical     ]) ad.flags.dflags       |=  Dflag     ::Critical    ;
		if (cmd_line.flags[Flag::Essential    ]) ad.flags.dflags       |=  Dflag     ::Essential   ;
		if (cmd_line.flags[Flag::Ignore       ]) ad.flags.extra_dflags |=  ExtraDflag::Ignore      ;
		if (cmd_line.flags[Flag::IgnoreError  ]) ad.flags.dflags       |=  Dflag     ::IgnoreError ;
		if (cmd_line.flags[Flag::NoRequired   ]) ad.flags.dflags       &= ~Dflag     ::Required    ;
		if (cmd_line.flags[Flag::ReaddirOk    ]) ad.flags.extra_dflags |=  ExtraDflag::ReaddirOk   ;
		if (cmd_line.flags[Flag::NoExcludeStar]) ad.flags.extra_dflags &= ~ExtraDflag::NoStar      ;
		if (cmd_line.flags[Flag::Verbose      ]) ad.flags.dflags       |=  Dflag     ::Verbose     ;
		if (cmd_line.flags[Flag::Direct       ]) ad.flags.extra_dflags |=  ExtraDflag::Direct      ;
		//
		bool                                     verbose   = ad.flags.dflags      [Dflag     ::Verbose] ;
		bool                                     direct    = ad.flags.extra_dflags[ExtraDflag::Direct ] ;
		bool                                     sync      = verbose || direct                          ;
		::pair<::vector<VerboseInfo>,bool/*ok*/> dep_infos ;
		try                       { dep_infos = JobSupport::depend( {New,Yes/*enabled*/} , ::copy(cmd_line.args) , ad , no_follow , cmd_line.flags[Flag::Regexpr] ) ; }
		catch (::string const& e) { exit(Rc::Usage,e) ;                                                                                                               }
		//
		if (!sync) return 0 ;
		//
		SWEAR(!( verbose && direct )) ;
		if (direct) {
			rc = dep_infos.second ? 0 : 1 ;
		} else {
			SWEAR( dep_infos.first.size()==cmd_line.args.size() , dep_infos.first.size() , cmd_line.args.size() ) ;
			size_t   w_ok  = 0 ;
			size_t   w_crc = 3 ;
			for( VerboseInfo vi : dep_infos.first ) {
				const char* ok_str ;
				switch (vi.ok) {
					case Yes   : ok_str = "ok"    ; break ;
					case Maybe : ok_str = "???"   ; break ;
					case No    : ok_str = "error" ; break ;
				DF}                                                                                    // NO_COV
				w_ok  = ::max( w_ok  , ::strlen(ok_str)       ) ;
				w_crc = ::max( w_crc , ::string(vi.crc).size()) ;
			}
			for( size_t i : iota(dep_infos.first.size()) ) {
				VerboseInfo const& vi = dep_infos.first[i] ;
				switch (vi.ok) {
					case Yes   : out << widen("ok"   ,w_ok) ;          break ;
					case Maybe : out << widen("???"  ,w_ok) ; rc = 1 ; break ;
					case No    : out << widen("error",w_ok) ; rc = 1 ; break ;
				DF}                                                                                    // NO_COV
				out <<' '<< widen(::string(vi.crc),w_crc) ;
				out <<' '<< cmd_line.args[i]              ;
				out <<'\n'                                ;
			}
			if (cmd_line.flags[Flag::IgnoreError]) rc = 0 ;
		}
		//
	}
	if (+out) Fd::Stdout.write(out) ;
	return rc ;
}
