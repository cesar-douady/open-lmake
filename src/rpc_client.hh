// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "config.hh"

#include "app.hh"
#include "serialize.hh"

ENUM_1( ReqProc                        // PER_CMD : add a value that represents your command, above or below HasArgs as necessary
,	HasArgs = Debug                    // >=HasArgs means command has arguments
,	None                               // must stay first
,	Close
,	Kill
,	Debug
,	Forget
,	Make
,	Mark
,	Show
)

ENUM( ReqKey       // PER_CMD : add key as necessary (you may share with other commands) : there must be a single key on the command line
,	None           // must stay first
,	Add            // if proc==Mark
,	Clear          // if proc==Mark
,	Cmd            // if proc==Show
,	Delete         // if proc==Mark
,	Deps           // if proc==Show
,	Env            // if proc==Show
,	Error          // if proc==Forget, forget previous error, i.e. rerun targets in error that appear up-to-date
,	ExecScript     // if proc==Show
,	Info           // if proc==Show
,	InvDeps        // if proc==Show
,	List           // if proc==Mark
,	Resources      // if proc==Forget, redo everything that were not redone when resources changed, to ensure reproducibility
,	Stderr         // if proc==Show
,	Stdout         // if proc==Show
,	Targets        // if proc==Show
)
static inline bool is_mark_glb(ReqKey key) {
	switch (key) {
		case ReqKey::Clear  :
		case ReqKey::List   : return true  ;
		case ReqKey::Add    :
		case ReqKey::Delete : return false ;
		default : FAIL(key) ;
	}
}

ENUM( ReqFlag                          // PER_CMD : add flags as necessary (you may share with other commands) : there may be 0 or more flags on the command line
,	Archive                            // if proc==                  Make               , all intermediate files are generated
,	Backend                            // if proc==                  Make               , send argument to backends
,	Debug                              // if proc==                                Show , generate debug executable script
,	Deps                               // if proc==         Forget                      , forget deps
,	Force                              // if proc==                         Mark        , act if doable, even if awkward
,	ForgetOldErrors                    // if proc==                  Make               , assume old errors are transcient
,	Freeze                             // if proc==                         Mark        , prevent job rebuild
,	Graphic                            // if proc== Debug                          Show , use GUI to show debug script
,	Job                                //                                                 interpret (unique) arg as job name
,	Jobs                               // if proc==                  Make               , max number of jobs
,	KeepTmp                            // if proc==                  Make               , keep tmp dir after job execution
,	LiveOut                            // if proc==                  Make               , generate live output for last job
,	Local                              // if proc==                  Make               , lauch all jobs locally
,	ManualOk                           // if proc==                  Make | Mark        , allow lmake to overwrite manual files
,	NoTrigger                          // if proc==                         Mark        , prevent lmake from rebuilding dependent jobs
,	Porcelaine                         //                                                 generate easy to parse output
,	Quiet                              //                                                 do not generate user oriented messages
,	Rule                               //                                                 rule name when interpreting arg as job name
,	SourceOk                           // if proc==                  Make               , allow lmake to overwrite source files
,	Targets                            // if proc==         Forget                      , forget targets
,	Verbose                            // if proc==                  Make        | Show , generate generous output
)
using ReqFlags = BitMap<ReqFlag> ;

struct ReqSyntax : Syntax<ReqKey,ReqFlag> {
	ReqSyntax() = default ;
	ReqSyntax(                                    ::umap<ReqFlag,FlagSpec> const& fs    ) : ReqSyntax{{},fs} {}
	ReqSyntax( ::umap<ReqKey,KeySpec> const& ks , ::umap<ReqFlag,FlagSpec> const& fs={} ) : Syntax   {ks,fs} {
		// add standard options
		flags[+ReqFlag::Quiet] = { .short_name='q' , .has_arg=false , .doc="do not generate user oriented messages"  } ;
		flags[+ReqFlag::Job  ] = { .short_name='J' , .has_arg=false , .doc="interpret (unique) arg as a job name"    } ;
		flags[+ReqFlag::Rule ] = { .short_name='R' , .has_arg=true  , .doc="force rule when interpreting arg as job" } ;
	}

} ;

using ReqCmdLine = CmdLine<ReqKey,ReqFlag> ;

static constexpr char ServerMrkr[] = "server" ;

struct ReqOptions {
	friend ::ostream& operator<<( ::ostream& , ReqOptions const& ) ;
	// cxtors & casts
	ReqOptions() = default ;
	//
	ReqOptions( ::string const& sds , Bool3 rv , ReqKey k , ReqFlags f={} , ::array_s<+ReqFlag::N> const& fa={} ) :
		startup_dir_s { sds }
	,	reverse_video { rv  }
	,	key           { k   }
	,	flags         { f   }
	,	flag_args     { fa  }
	{}
	ReqOptions( Bool3 rv , ReqCmdLine cl ) :
		startup_dir_s { *g_startup_dir_s }
	,	reverse_video { rv               }
	,	key           { cl.key           }
	,	flags         { cl.flags         }
	,	flag_args     { cl.flag_args     }
	{}
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,startup_dir_s) ;
		::serdes(s,reverse_video) ;
		::serdes(s,key          ) ;
		::serdes(s,flags        ) ;
		::serdes(s,flag_args    ) ;
	}
	// data
	::string               startup_dir_s ;
	Bool3                  reverse_video = Maybe        ;  // if Maybe <=> not a terminal, do not colorize
	ReqKey                 key           = ReqKey::None ;
	ReqFlags               flags         ;
	::array_s<+ReqFlag::N> flag_args     ;
} ;

struct ReqRpcReq {
	friend ::ostream& operator<<( ::ostream& , ReqRpcReq const& ) ;
	using Proc = ReqProc ;
	// cxtors & casts
	ReqRpcReq() = default ;
	ReqRpcReq( Proc p                                               ) : proc{p}                           { SWEAR(proc< Proc::HasArgs) ; }
	ReqRpcReq( Proc p , ::vector_s const& fs , ReqOptions const& ro ) : proc{p} , files{fs} , options{ro} { SWEAR(proc>=Proc::HasArgs) ; }
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,proc) ;
		if (proc>=Proc::HasArgs) {
			::serdes(s,files  ) ;
			::serdes(s,options) ;
		}
	}
	// data
	Proc       proc    = ReqProc::None ;
	::vector_s files   ;
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
	ReqRpcReply(               ) = default ;
	ReqRpcReply(bool       ok_ ) : kind{Kind::Status} , ok {ok_ }         {}
	ReqRpcReply(::string&& txt_) : kind{Kind::Txt   } , txt{::move(txt_)} {}
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
