// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "py.hh"
#include "time.hh"

#include "gather.hh"

using namespace Disk ;
using namespace Py   ;
using namespace Time ;

ENUM( CmdKey , None )
ENUM( CmdFlag
,	AutodepMethod
,	AutoMkdir
,	ChrootDir
,	Cwd
,	Env
,	IgnoreStat
,	KeepEnv
,	LinkSupport
,	Out
,	RootView
,	SourceDirs
,	TmpView
,   Views
,	WorkDir
)

static ::vmap_s<::vector_s> _mk_views(::string const& views) {
	::vmap_s<::vector_s> res ;
	if (+views)
		for( auto const& [py_k,py_v] : py_eval(views)->as_a<Dict>() ) {
			res.emplace_back( ::string(py_k.as_a<Str>()) , ::vector_s() ) ;
			::vector_s& phys = res.back().second ;
			for( Object const& py_phy : py_v.as_a<Sequence>() )
				phys.push_back(py_phy.as_a<Str>()) ;
		}
	return res ;
}

static ::vector_s _mk_src_dirs_s(::string const& src_dirs) {
	::vector_s res ;
	if (+src_dirs) {
		Ptr<Object> py_src_dirs = py_eval(src_dirs) ;                    // keep Python object alive during iteration
		for( Object const&  py_src_dir : py_src_dirs->as_a<Sequence>() )
			res.push_back(with_slash(py_src_dir.as_a<Str>())) ;
	}
	return res ;
}

static ::map_ss _mk_env( ::string const& keep_env , ::string const& env ) {
	::map_ss res ;
	// use an intermediate variable (py_keep_env and py_env) to keep Python object alive during iteration
	if (+keep_env) { Ptr<Object> py_keep_env = py_eval(keep_env) ; for( Object const&  py_k       : py_keep_env->as_a<Sequence>() ) { ::string k = py_k.as_a<Str>() ; res[k] = get_env(k)       ; } }
	if (+env)      { Ptr<Object> py_env      = py_eval(env     ) ; for( auto   const& [py_k,py_v] : py_env     ->as_a<Dict    >() ) { ::string k = py_k.as_a<Str>() ; res[k] = py_v.as_a<Str>() ; } }
	return res ;
}

int main( int argc , char* argv[] ) {
	app_init(true/*read_only_ok*/,false/*cd_root*/) ;
	Py::init( *g_lmake_dir_s , true/*multi-thread*/ ) ;
	//
	Syntax<CmdKey,CmdFlag,false/*OptionsAnywhere*/> syntax{{
		{ CmdFlag::AutodepMethod , { .short_name='m' , .has_arg=true  , .doc="method used to detect deps (none, fuse, ld_audit, ld_preload, ld_preload_jemalloc, ptrace)" } } // PER_AUTODEP_METHOD
	,	{ CmdFlag::AutoMkdir     , { .short_name='a' , .has_arg=false , .doc="automatically create dir upon chdir"                                                        } }
	,	{ CmdFlag::ChrootDir     , { .short_name='c' , .has_arg=true  , .doc="dir which to chroot to before execution"                                                    } }
	,	{ CmdFlag::Cwd           , { .short_name='d' , .has_arg=true  , .doc="current working directory in which to execute job"                                          } }
	,	{ CmdFlag::Env           , { .short_name='e' , .has_arg=true  , .doc="environment variables to set, given as a python dict"                                       } }
	,	{ CmdFlag::IgnoreStat    , { .short_name='i' , .has_arg=false , .doc="stat-like syscalls do not trigger dependencies"                                             } }
	,	{ CmdFlag::KeepEnv       , { .short_name='k' , .has_arg=true  , .doc="list of environment variables to keep, given as a python tuple/list"                        } }
	,	{ CmdFlag::LinkSupport   , { .short_name='l' , .has_arg=true  , .doc="level of symbolic link support (none, file, full), default=full"                            } }
	,	{ CmdFlag::Out           , { .short_name='o' , .has_arg=true  , .doc="output file"                                                                                } }
	,	{ CmdFlag::RootView      , { .short_name='r' , .has_arg=true  , .doc="name under which repo top-level dir is seen"                                                } }
	,	{ CmdFlag::SourceDirs    , { .short_name='s' , .has_arg=true  , .doc="source dirs given as a python tuple/list, all elements must end with /"                     } }
	,	{ CmdFlag::TmpView       , { .short_name='t' , .has_arg=true  , .doc="name under which tmp dir is seen"                                                           } }
	,	{ CmdFlag::Views         , { .short_name='v' , .has_arg=true  , .doc="view mapping given as a python dict mapping views to tuple/list (upper,lower,...)"          } }
	,	{ CmdFlag::WorkDir       , { .short_name='w' , .has_arg=true  , .doc="work dir in which to prepare a chroot env if necessary"                                     } }
	}} ;
	CmdLine<CmdKey,CmdFlag> cmd_line { syntax , argc , argv } ;
	Gather                  gather   ;
	::map_ss                env      ;
	//
	try {
		::vector_s    src_dirs_s ;
		AutodepMethod method     = AutodepMethod::Dflt                    ; if (cmd_line.flags[CmdFlag::AutodepMethod]) method = mk_enum<AutodepMethod>(cmd_line.flag_args[+CmdFlag::AutodepMethod]) ;
		::string      tmp_dir_s  = with_slash(get_env("TMPDIR",P_tmpdir)) ;
		::string      root_dir_s = *g_root_dir_s                          ;
		//
		if (!cmd_line.args                                                                          ) throw "no exe to launch"s                                                      ;
		if ( cmd_line.flags[CmdFlag::ChrootDir] && !is_abs(cmd_line.flag_args[+CmdFlag::ChrootDir]) ) throw "chroot dir must be absolute : "+cmd_line.flag_args[+CmdFlag::ChrootDir] ;
		if ( cmd_line.flags[CmdFlag::RootView ] && !is_abs(cmd_line.flag_args[+CmdFlag::RootView ]) ) throw "root view must be absolute : " +cmd_line.flag_args[+CmdFlag::RootView ] ;
		if ( cmd_line.flags[CmdFlag::TmpView  ] && !is_abs(cmd_line.flag_args[+CmdFlag::TmpView  ]) ) throw "tmp view must be absolute : "  +cmd_line.flag_args[+CmdFlag::TmpView  ] ;
		if (                                       !is_abs(tmp_dir_s                              ) ) throw "$TMPDIR must be absolute : "   +no_slash(tmp_dir_s)                     ;
		//
		JobSpace job_space {
			.chroot_dir_s = cmd_line.flags[CmdFlag::ChrootDir] ? with_slash(cmd_line.flag_args[+CmdFlag::ChrootDir]) : ""
		,	.root_view_s  = cmd_line.flags[CmdFlag::RootView ] ? with_slash(cmd_line.flag_args[+CmdFlag::RootView ]) : ""
		,	.tmp_view_s   = cmd_line.flags[CmdFlag::TmpView  ] ? with_slash(cmd_line.flag_args[+CmdFlag::TmpView  ]) : ""
		} ;
		try { job_space.views = _mk_views     (cmd_line.flag_args[+CmdFlag::Views     ]                                  ) ; } catch (::string const& e) { throw "bad views format : "      +e ; }
		try { src_dirs_s      = _mk_src_dirs_s(cmd_line.flag_args[+CmdFlag::SourceDirs]                                  ) ; } catch (::string const& e) { throw "bad source_dirs format : "+e ; }
		try { env             = _mk_env       (cmd_line.flag_args[+CmdFlag::KeepEnv   ],cmd_line.flag_args[+CmdFlag::Env]) ; } catch (::string const& e) { throw "bad env format : "        +e ; }
		//
		job_space.enter( *g_root_dir_s , tmp_dir_s , 0 , with_slash(cmd_line.flag_args[+CmdFlag::WorkDir]) , src_dirs_s , method==AutodepMethod::Fuse ) ;
		//
		if (+job_space.root_view_s) root_dir_s = job_space.root_view_s ;
		if (+job_space.tmp_view_s ) tmp_dir_s  = job_space.tmp_view_s  ;
		/**/                       env["ROOT_DIR"] = no_slash(root_dir_s) ;
		/**/                       env["TMPDIR"  ] = no_slash(tmp_dir_s ) ;
		if (!env.contains("HOME")) env["HOME"    ] = env["TMPDIR"]        ;                                  // by default, set HOME to TMPDIR as this cannot be set from rule
		//
		/**/                                        gather.env                     = &env                                                            ;
		if (cmd_line.flags[CmdFlag::Cwd          ]) gather.cwd_s                   = with_slash(cmd_line.flag_args[+CmdFlag::Cwd])                   ;
		if (cmd_line.flags[CmdFlag::AutodepMethod]) gather.method                  = method                                                          ;
		/**/                                        gather.autodep_env.auto_mkdir  = cmd_line.flags[CmdFlag::AutoMkdir ]                             ;
		/**/                                        gather.autodep_env.ignore_stat = cmd_line.flags[CmdFlag::IgnoreStat]                             ;
		if (cmd_line.flags[CmdFlag::LinkSupport  ]) gather.autodep_env.lnk_support = mk_enum<LnkSupport>(cmd_line.flag_args[+CmdFlag::LinkSupport])  ;
		/**/                                        gather.autodep_env.root_dir_s  = root_dir_s                                                      ;
		/**/                                        gather.autodep_env.tmp_dir_s   = tmp_dir_s                                                       ;
		/**/                                        gather.autodep_env.views       = ::move(job_space.views)                                         ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	Status status ;
	try {
		BlockedSig blocked{{SIGCHLD,SIGINT}} ;
		gather.cmd_line = cmd_line.args ;
		//       vvvvvvvvvvvvvvvvvvv
		status = gather.exec_child() ;
		//       ^^^^^^^^^^^^^^^^^^^
	} catch (::string const& e) { exit(Rc::System,e) ; }
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
	Pdate    prev_first_read  ;
	auto send = [&]( ::string const& dep={} , Pdate first_read={} ) {                                        // process deps with a delay of 1 because we need next entry for ascii art
		bool parallel = +first_read && first_read==prev_first_read ;
		if (+prev_dep) {
			if      ( !prev_parallel && !parallel ) deps_stream << "  "  ;
			else if ( !prev_parallel &&  parallel ) deps_stream << "/ "  ;
			else if (  prev_parallel &&  parallel ) deps_stream << "| "  ;
			else                                    deps_stream << "\\ " ;
			deps_stream << prev_dep << '\n' ;
		}
		prev_first_read = first_read ;
		prev_parallel   = parallel   ;
		prev_dep        = dep        ;
	} ;
	for( auto const& [dep,ai] : gather.accesses ) if (ai.digest.write==No) send(dep,ai.first_read().first) ;
	/**/                                                                   send(                         ) ; // send last
	return status!=Status::Ok ;
}
