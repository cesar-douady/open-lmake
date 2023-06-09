// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "config.hh"

#include "app.hh"
#include "serialize.hh"

ENUM( ReqProc      // PER_CMD : add a value that represents your command
,	None           // must stay first
,	Close
,	Forget
,	Freeze
,	Kill
,	Make
,	Show
)

ENUM( ReqKey       // PER_CMD : add key as necessary (you may share with other commands) : there must be a single key on the command line
,	None           // must stay first
,	AllDeps        // if proc==Show
,	Deps           // if proc==Show
,	Env            // if proc==Show
,	ExecScript     // if proc==Show
,	Info           // if proc==Show
,	InvDeps        // if proc==Show
,	Script         // if proc==Show
,	Stderr         // if proc==Show
,	Stdout         // if proc==Show
,	Targets        // if proc==Show
,	Add            // if proc==Freeze
,	Delete         // if proc==Freeze
,	DeleteAll      // if proc==Freeze
,	List           // if proc==Freeze
,	Error          // if proc==Forget, forget previous error, i.e. rerun targets in error that appear up-to-date
,	Resources      // if proc==Forget, redo everything that were not redone when resources changed, to ensure reproducibility
)

ENUM( ReqFlag       // PER_CMD : add flags as necessary (you may share with other commands) : there may be 0 or more flags on the command line
,	Archive         // if proc==Make  , all intermediate files are generated
,	ForgetOldErrors // if proc==Make  , assume old errors are transcient
,	KeepTmp         // if proc==Make  , keep tmp dir after job execution
,	LiveOut         // if proc==Make  , generate live output for last job
,	ManualOk        // if proc==Make  , allow lmake to overwrite manual files
,	SourceOk        // if proc==Make  , allow lmake to overwrite source files
,	Force           // if proc==Freeze, act if doable, even if awkward
,	Job             // if proc==Freeze, Job  oriented action
,	File            // if proc==Freeze, File oriented action
)
using ReqFlags = BitMap<ReqFlag> ;

using ReqSyntax  = Syntax <ReqKey,ReqFlag> ;
using ReqCmdLine = CmdLine<ReqKey,ReqFlag> ;

static constexpr char ServerMrkr[] = "server" ;

struct ReqOptions {
	friend ::ostream& operator<<( ::ostream& , ReqOptions const& ) ;
	// cxtors & casts
	ReqOptions(                          ) = default ;
	ReqOptions( Bool3 rv , ReqCmdLine cl ) : startup_dir_s{*g_startup_dir_s} , reverse_video{rv} , key{cl.key} , flags{cl.flags} {}
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,startup_dir_s) ;
		::serdes(s,reverse_video) ;
		::serdes(s,key          ) ;
		::serdes(s,flags        ) ;
	}
	// data
	::string startup_dir_s ;
	Bool3    reverse_video = Maybe        ;                // if Maybe <=> not a terminal, do not colorize
	ReqKey   key           = ReqKey::None ;                // if proc==          Freeze           || Show
	ReqFlags flags         ;                               // if proc==Forget || Freeze   || Make
} ;

struct ReqRpcReq {
	friend ::ostream& operator<<( ::ostream& , ReqRpcReq const& ) ;
	using Proc = ReqProc ;
	// cxtors & casts
	ReqRpcReq() = default ;
	ReqRpcReq( Proc p                                               ) : proc{p}                             { SWEAR(!has_options()) ; }
	ReqRpcReq( Proc p , ::vector_s const& ts , ReqOptions const& ro ) : proc{p} , targets{ts} , options{ro} { SWEAR( has_options()) ; }
	bool has_options() const {
		switch (proc) {
			case Proc::Kill  : return false ;
			case Proc::Close : return false ;
			default          : return true  ;
		}
	}
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,proc   ) ;
		::serdes(s,targets) ;
		::serdes(s,options) ;
	}
	// data
	Proc       proc    = ReqProc::None ;
	::vector_s targets ;
	ReqOptions options ;
} ;

ENUM( ReqKind
,	None
,	Txt
,	Status
)

struct ReqRpcReply {
	friend ::ostream& operator<<( ::ostream& , ReqRpcReply const& ) ;
	using Kind = ReqKind ;
	// cxtors & casts
	ReqRpcReply(                      ) = default ;
	ReqRpcReply(bool            ok_   ) : kind{Kind::Status} , ok {ok_ } {}
	ReqRpcReply(::string const& txt_  ) : kind{Kind::Txt   } , txt{txt_} {}
	//
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = ReqRpcReply() ;
		::serdes(s,kind) ;
		switch (kind) {
			case Kind::None   :                   break ;
			case Kind::Status : ::serdes(s,ok ) ; break ;
			case Kind::Txt    : ::serdes(s,txt) ; break ;
			default : FAIL(kind) ;
		}
	}
	// data
	Kind     kind = Kind::None ;
	bool     ok   = false      ;
	::string txt  ;
} ;
