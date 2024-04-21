// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "gather.hh"

using namespace Disk ;
using namespace Hash ;

ENUM( CmdKey , None )
ENUM( CmdFlag
,	AutodepMethod
,	AutoMkdir
,	IgnoreStat
,	LinkSupport
,	Out
,	TmpView
)

int main( int argc , char* argv[] ) {
	app_init(false/*cd_root*/) ;
	//
	Syntax<CmdKey,CmdFlag,false/*OptionsAnywhere*/> syntax{{
		{ CmdFlag::AutodepMethod , { .short_name='m' , .has_arg=true  , .doc="method used to detect deps (none, ld_audit, ld_preload, ld_preload_jemalloc, ptrace)" } } // PER_AUTODEP_METHOD : doc
	,	{ CmdFlag::AutoMkdir     , { .short_name='d' , .has_arg=false , .doc="automatically create dir upon chdir"                                                  } }
	,	{ CmdFlag::IgnoreStat    , { .short_name='i' , .has_arg=false , .doc="stat-like syscalls do not trigger dependencies"                                       } }
	,	{ CmdFlag::LinkSupport   , { .short_name='s' , .has_arg=true  , .doc="level of symbolic link support (none, file, full), default=full"                      } }
	,	{ CmdFlag::Out           , { .short_name='o' , .has_arg=true  , .doc="output file"                                                                          } }
	}} ;
	CmdLine<CmdKey,CmdFlag> cmd_line{syntax,argc,argv} ;
	//
	if (!cmd_line.flags[CmdFlag::AutodepMethod]) syntax.usage("must have both autodep-method and link-support options") ;
	//
	Gather gather ;
	//
	try {
		/**/                                      gather.method                  = mk_enum<AutodepMethod>(cmd_line.flag_args[+CmdFlag::AutodepMethod]) ;
		/**/                                      gather.autodep_env.auto_mkdir  = cmd_line.flags[CmdFlag::AutoMkdir ]                                 ;
		/**/                                      gather.autodep_env.ignore_stat = cmd_line.flags[CmdFlag::IgnoreStat]                                 ;
		if (cmd_line.flags[CmdFlag::LinkSupport]) gather.autodep_env.lnk_support = mk_enum<LnkSupport>(cmd_line.flag_args[+CmdFlag::LinkSupport])      ;
		/**/                                      gather.autodep_env.root_dir    = *g_root_dir                                                         ;
		/**/                                      gather.autodep_env.tmp_dir     = get_env("TMPDIR",P_tmpdir)                                          ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	Status status ;
	//                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try                       { status = gather.exec_child( cmd_line.args ) ; }
	//                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch (::string const& e) { exit(Rc::System,e) ;                          }
	//
	::ostream* ds       ;
	OFStream   user_out ;
	if (cmd_line.flags[CmdFlag::Out]) { user_out.open(cmd_line.flag_args[+CmdFlag::Out]) ; ds = &user_out ; }
	else                              {                                                    ds = &::cerr   ; }
	::ostream& deps_stream = *ds ;
	deps_stream << "targets :\n" ;
	for( auto const& [target,ai] : gather.accesses ) {
		if (ai.digest.write==No) continue ;
		deps_stream << ( ai.digest.write==Maybe? "? " : "  " ) ;
		deps_stream << target << '\n'                          ;
	}
	deps_stream << "deps :\n" ;
	::string prev_dep         ;
	bool     prev_parallel    = false ;
	NodeIdx  prev_parallel_id = 0     ;
	auto send = [&]( ::string const& dep={} , NodeIdx parallel_id=0 ) {                               // process deps with a delay of 1 because we need next entry for ascii art
		bool parallel = parallel_id && parallel_id==prev_parallel_id ;
		if (+prev_dep) {
			if      ( !prev_parallel && !parallel ) deps_stream << "  "  ;
			else if ( !prev_parallel &&  parallel ) deps_stream << "/ "  ;
			else if (  prev_parallel &&  parallel ) deps_stream << "| "  ;
			else                                    deps_stream << "\\ " ;
			deps_stream << prev_dep << '\n' ;
		}
		prev_parallel_id = parallel_id ;
		prev_parallel    = parallel    ;
		prev_dep         = dep         ;
	} ;
	for( auto const& [dep,ai] : gather.accesses ) if (ai.digest.write==No) send(dep,ai.parallel_id) ;
	/**/                                                                   send(                  ) ; // send last
	return status!=Status::Ok ;
}
