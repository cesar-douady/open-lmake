// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "types.hh"

#include "app.hh"
#include "serialize.hh"

ENUM_1( ReqProc     // PER_CMD : add a value that represents your command, above or below HasArgs as necessary
,	HasArgs = Debug // >=HasArgs means command has arguments
,	None            // must stay first
,	Close
,	Kill
,	Debug
,	Forget
,	Make
,	Mark
,	Show
)

ENUM( ReqKey   // PER_CMD : add key as necessary (you may share with other commands) : there must be a single key on the command line
,	None       // must stay first
,	Add        // if proc==Mark
,	Bom        // if proc==Show
,	Clear      // if proc==Mark
,	Cmd        // if proc==Show
,	Delete     // if proc==Mark
,	Deps       // if proc==Show
,	Env        // if proc==Show
,	Info       // if proc==Show
,	InvDeps    // if proc==Show
,	InvTargets // if proc==Show
,	List       // if proc==Mark
,	Resources  // if proc==Forget, redo everything that were not redone when resources changed, to ensure reproducibility
,	Running    // if proc==Show
,	Stderr     // if proc==Show
,	Stdout     // if proc==Show
,	Targets    // if proc==Show
,	Trace      // if proc==Show
)
inline bool is_mark_glb(ReqKey key) {
	switch (key) {
		case ReqKey::Clear  :
		case ReqKey::List   : return true  ;
		case ReqKey::Add    :
		case ReqKey::Delete : return false ;
	DF}
}

ENUM( ReqFlag       // PER_CMD : add flags as necessary (you may share with other commands) : there may be 0 or more flags on the command line
,	Archive         // if proc==                  Make               , all intermediate files are generated
,	Backend         // if proc==                  Make               , send argument to backends
,	Deps            // if proc==         Forget                      , forget deps
,	Force           // if proc==                         Mark        , act if doable, even if awkward
,	ForgetOldErrors // if proc==                  Make               , assume old errors are transient
,	Freeze          // if proc==                         Mark        , prevent job rebuild
,	Job             //                                                 interpret (unique) arg as job name
,	Jobs            // if proc==                  Make               , max number of jobs
,	KeepTmp         // if proc== Debug            Make               , keep tmp dir after job execution
,	Key             // if proc== Debug                               , key used to look up into config.debug to find helper module used to debug
,	LiveOut         // if proc==                  Make               , generate live output for last job
,	Local           // if proc==                  Make               , lauch all jobs locally
,	NoExec          // if proc== Debug                               , dont execute, just generate files
,	NoTrigger       // if proc==                         Mark        , prevent lmake from rebuilding dependent jobs
,	Porcelaine      //                                                 generate easy to parse output
,	Quiet           //                                                 do not generate user oriented messages
,	RetryOnError    // if proc==                  Make               , retry jobs in error
,	Rule            //                                                 rule name when interpreting arg as job name
,	SourceOk        // if proc==                  Make               , allow lmake to overwrite source files
,	Sync            //                                               , force synchronous operation (start server and wait for its end)
,	Targets         // if proc==         Forget                      , forget targets
,	TmpDir          // if proc== Debug                               , tmp dir to use in case TMPDIR is specified as ... in job
,	Verbose         // if proc==                  Make        | Show , generate generous output
,	Video           //                                               , assume output video : n(ormal), r(everse) or f(ile)
)
using ReqFlags = BitMap<ReqFlag> ;

ENUM( ReqRpcReplyProc
,	None
,	File
,	Status
,	Stdout
,	Stderr
)

struct ReqSyntax : Syntax<ReqKey,ReqFlag> {
	ReqSyntax() = default ;
	ReqSyntax(                                    ::umap<ReqFlag,FlagSpec> const& fs    ) : ReqSyntax{{},fs} {}
	ReqSyntax( ::umap<ReqKey,KeySpec> const& ks , ::umap<ReqFlag,FlagSpec> const& fs={} ) : Syntax   {ks,fs} {
		// add standard options
		flags[+ReqFlag::Quiet] = { .short_name='q' , .has_arg=false , .doc="do not generate user oriented messages"              } ;
		flags[+ReqFlag::Job  ] = { .short_name='J' , .has_arg=false , .doc="interpret (unique) arg as a job name"                } ;
		flags[+ReqFlag::Sync ] = { .short_name='S' , .has_arg=false , .doc="synchronous : start server and wait for its end"     } ;
		flags[+ReqFlag::Rule ] = { .short_name='R' , .has_arg=true  , .doc="force rule when interpreting arg as job"             } ;
		flags[+ReqFlag::Video] = { .short_name='V' , .has_arg=true  , .doc="assume output video : n(ormal), r(everse) or f(ile)" } ;
	}

} ;

using ReqCmdLine = CmdLine<ReqKey,ReqFlag> ;

static constexpr char ServerMrkr[] = ADMIN_DIR_S "server" ;

struct ReqOptions {
	friend ::string& operator+=( ::string& , ReqOptions const& ) ;
	// cxtors & casts
	ReqOptions() = default ;
	//
	ReqOptions( ::string const& sds , Bool3 rv , ReqKey k , ReqFlags f={} , ::array_s<N<ReqFlag>> const& fa={} ) :
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
	::string              startup_dir_s ;
	Bool3                 reverse_video = Maybe        ; // if Maybe <=> not a terminal, do not colorize
	ReqKey                key           = ReqKey::None ;
	ReqFlags              flags         ;
	::array_s<N<ReqFlag>> flag_args     ;
} ;

struct ReqRpcReq {
	friend ::string& operator+=( ::string& , ReqRpcReq const& ) ;
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

struct ReqRpcReply {
	friend ::string& operator+=( ::string& , ReqRpcReply const& ) ;
	using Proc = ReqRpcReplyProc ;
	// cxtors & casts
	ReqRpcReply(                          ) = default ;
	ReqRpcReply( Proc p , bool       ok_  ) : proc{p} , ok {ok_         } { SWEAR( p==Proc::Status                                     ) ; }
	ReqRpcReply( Proc p , ::string&& txt_ ) : proc{p} , txt{::move(txt_)} { SWEAR( p==Proc::File || p==Proc::Stderr || p==Proc::Stdout ) ; }
	//
	template<IsStream T> void serdes(T& s) {
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::None   :                   break ;
			case Proc::Status : ::serdes(s,ok ) ; break ;
			case Proc::File   :
			case Proc::Stderr :
			case Proc::Stdout : ::serdes(s,txt) ; break ;
		DF}
	}
	// data
	Proc     proc = Proc::None ;
	bool     ok   = false      ;
	::string txt  ;
} ;
