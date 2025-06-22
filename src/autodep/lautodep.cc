// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be first because Python.h must be first

#include "app.hh"
#include "disk.hh"
#include "time.hh"

#include "gather.hh"

using namespace Disk ;
using namespace Py   ;
using namespace Time ;

enum class CmdKey  : uint8_t { None } ;
enum class CmdFlag : uint8_t {
	AutoMkdir
,	AutodepMethod
,	ChrootDir
,	Cwd
,	Env
,	KeepTmp
,	LinkSupport
,	LmakeView
,	Out
,	ReaddirOk
,	RepoView
,	SourceDirs
,	TmpDir
,	TmpView
,   Views
,	WorkDir
} ;

static ::vmap_s<JobSpace::ViewDescr> _mk_views(::string const& views) {
	::vmap_s<JobSpace::ViewDescr> res ;
	Gil                           gil ;
	if (+views) {
		Ptr<> py_views = py_eval(views) ;                         // hold objet in a Ptr
		for( auto const& [py_k,py_v] : py_views->as_a<Dict>() ) {
			res.emplace_back( ::string(py_k.as_a<Str>()) , JobSpace::ViewDescr() ) ;
			JobSpace::ViewDescr& descr = res.back().second ;
			if (py_v.is_a<Str>()) {
				descr.phys.push_back(py_v.as_a<Str>()) ;
			} else if (py_v.is_a<Dict>()) {
				Dict& py_dct = py_v.as_a<Dict>() ;
				descr.phys.push_back(py_dct.get_item("upper").as_a<Str>()) ;
				for( Object const& py_l  : py_dct.get_item("lower").as_a<Sequence>() )
					descr.phys.push_back(py_l.as_a<Str>()) ;
				if (py_dct.contains("copy_up"))
					for( Object const& py_cu : py_dct.get_item("copy_up").as_a<Sequence>() )
						descr.copy_up.push_back(py_cu.as_a<Str>()) ;
			}
		}
	}
	return res ;
}

static ::vector_s _mk_src_dirs_s(::string const& src_dirs) {
	::vector_s res ;
	Gil        gil ;
	if (+src_dirs) {
		Ptr<> py_src_dirs = py_eval(src_dirs) ;                          // keep python object alive during iteration
		for( Object const&  py_src_dir : py_src_dirs->as_a<Sequence>() )
			res.push_back(with_slash(py_src_dir.as_a<Str>())) ;
	}
	return res ;
}

static ::vmap_ss _mk_env( ::string const& env ) {
	::uset_s  seen ;
	::vmap_ss res  ;
	Gil       gil  ;
	// use an intermediate variable (py_env) to keep python object alive during iteration
	if (+env) {
		Ptr<> py_env = py_eval(env) ;                           // hold in Ptr<> while iterating over
		for( Object const&  py_k : py_env->as_a<Sequence>() ) {
			::string k = py_k.as_a<Str>() ;
			if (has_env(k)) {
				throw_if( seen.contains(k) , "cannot keep ",k," twice" ) ;
				res.emplace_back(k,get_env(k)) ;
				seen.insert(k) ;
			}
		}
	}
	return res ;
}

int main( int argc , char* argv[] ) {
    block_sigs({SIGCHLD}) ;
	app_init(true/*read_only_ok*/,Yes/*chk_version*/,Maybe/*cd_root*/) ;
	Py::init(*g_lmake_root_s) ;
	//
	Syntax<CmdKey,CmdFlag> syntax {{
		// PER_AUTODEP_METHOD : complete doc on line below
		{ CmdFlag::AutoMkdir     , { .short_name='a' , .has_arg=false , .doc="automatically create dir upon chdir"                                                                       } }
	,	{ CmdFlag::ChrootDir     , { .short_name='c' , .has_arg=true  , .doc="dir which to chroot to before execution"                                                                   } }
	,	{ CmdFlag::Cwd           , { .short_name='d' , .has_arg=true  , .doc="current working directory in which to execute job"                                                         } }
	,	{ CmdFlag::ReaddirOk     , { .short_name='D' , .has_arg=false , .doc="allow reading local non-ignored dirs"                                                                      } }
	,	{ CmdFlag::Env           , { .short_name='e' , .has_arg=true  , .doc="list of environment variables to keep, given as a python tuple/list"                                       } }
	,	{ CmdFlag::KeepTmp       , { .short_name='k' , .has_arg=false , .doc="dont clean tmp dir after execution"                                                                        } }
	,	{ CmdFlag::LinkSupport   , { .short_name='l' , .has_arg=true  , .doc="level of symbolic link support (none, file, full), default=full"                                           } }
	,	{ CmdFlag::AutodepMethod , { .short_name='m' , .has_arg=true  , .doc="method used to detect deps (none, ld_audit, ld_preload, ld_preload_jemalloc, ptrace)"                      } }
	,	{ CmdFlag::Out           , { .short_name='o' , .has_arg=true  , .doc="output accesses file"                                                                                      } }
	,	{ CmdFlag::SourceDirs    , { .short_name='s' , .has_arg=true  , .doc="source dirs given as a python tuple/list, all elements must end with /"                                    } }
	,	{ CmdFlag::TmpDir        , { .short_name='t' , .has_arg=true  , .doc="physical tmp dir"                                                                                          } }
	,	{ CmdFlag::LmakeView     , { .short_name='L' , .has_arg=true  , .doc="name under which open-lmake installation dir is seen"                                                      } }
	,	{ CmdFlag::RepoView      , { .short_name='R' , .has_arg=true  , .doc="name under which repo top-level dir is seen"                                                               } }
	,	{ CmdFlag::TmpView       , { .short_name='T' , .has_arg=true  , .doc="name under which tmp dir is seen"                                                                          } }
	,	{ CmdFlag::Views         , { .short_name='V' , .has_arg=true  , .doc="view mapping given as a python dict mapping views to dict {'upper':upper,'lower':lower,'copy_up':copy_up}" } }
	}} ;
	CmdLine<CmdKey,CmdFlag> cmd_line { syntax , argc , argv } ;
	//
	JobStartRpcReply jsrr        ;
	JobSpace  &      job_space   = jsrr.job_space   ;
	AutodepEnv&      autodep_env = jsrr.autodep_env ;
	::map_ss         cmd_env     ;
	Gather           gather      ;
	//
	try {
		::string tmp_dir = cmd_line.flags[CmdFlag::TmpDir] ? cmd_line.flag_args[+CmdFlag::TmpDir] : get_env("TMPDIR") ;
		throw_if( !cmd_line.args                                                                          , "no exe to launch"                                                       ) ;
		throw_if(  cmd_line.flags[CmdFlag::ChrootDir] && !is_abs(cmd_line.flag_args[+CmdFlag::ChrootDir]) , "chroot dir must be absolute : ",cmd_line.flag_args[+CmdFlag::ChrootDir] ) ;
		throw_if(  cmd_line.flags[CmdFlag::LmakeView] && !is_abs(cmd_line.flag_args[+CmdFlag::LmakeView]) , "lmake view must be absolute : ",cmd_line.flag_args[+CmdFlag::LmakeView] ) ;
		throw_if(  cmd_line.flags[CmdFlag::RepoView ] && !is_abs(cmd_line.flag_args[+CmdFlag::RepoView ]) , "root view must be absolute : " ,cmd_line.flag_args[+CmdFlag::RepoView ] ) ;
		throw_if(  cmd_line.flags[CmdFlag::TmpView  ] && !is_abs(cmd_line.flag_args[+CmdFlag::TmpView  ]) , "tmp view must be absolute : "  ,cmd_line.flag_args[+CmdFlag::TmpView  ] ) ;
		throw_if( !tmp_dir                                                                                , "tmp dir must be specified"                                              ) ;
		throw_if(                                        !is_abs(tmp_dir                                ) , "tmp dir must be absolute : "   ,tmp_dir                                 ) ;
		//
		/**/                                        jsrr.keep_tmp           =                        cmd_line.flags    [ CmdFlag::KeepTmp      ]  ;
		/**/                                        jsrr.key                =                        "debug"                                      ;
		if (cmd_line.flags[CmdFlag::AutodepMethod]) jsrr.method             = mk_enum<AutodepMethod>(cmd_line.flag_args[+CmdFlag::AutodepMethod]) ;
		if (cmd_line.flags[CmdFlag::ChrootDir    ]) job_space.chroot_dir_s  = with_slash            (cmd_line.flag_args[+CmdFlag::ChrootDir    ]) ;
		if (cmd_line.flags[CmdFlag::LmakeView    ]) job_space.lmake_view_s  = with_slash            (cmd_line.flag_args[+CmdFlag::LmakeView    ]) ;
		if (cmd_line.flags[CmdFlag::RepoView     ]) job_space.repo_view_s   = with_slash            (cmd_line.flag_args[+CmdFlag::RepoView     ]) ;
		if (cmd_line.flags[CmdFlag::TmpView      ]) job_space.tmp_view_s    = with_slash            (cmd_line.flag_args[+CmdFlag::TmpView      ]) ;
		/**/                                        autodep_env.auto_mkdir  =                        cmd_line.flags    [ CmdFlag::AutoMkdir    ]  ;
		if (cmd_line.flags[CmdFlag::Cwd          ]) autodep_env.sub_repo_s  = with_slash            (cmd_line.flag_args[+CmdFlag::Cwd          ]) ;
		if (cmd_line.flags[CmdFlag::LinkSupport  ]) autodep_env.lnk_support = mk_enum<LnkSupport>   (cmd_line.flag_args[+CmdFlag::LinkSupport  ]) ;
		/**/                                        autodep_env.views       = job_space.flat_phys()                                               ;
		//
		try { jsrr.env               = _mk_env       (cmd_line.flag_args[+CmdFlag::Env       ]) ; } catch (::string const& e) { throw "bad env format : "        +e ; }
		try { job_space.views        = _mk_views     (cmd_line.flag_args[+CmdFlag::Views     ]) ; } catch (::string const& e) { throw "bad views format : "      +e ; }
		try { autodep_env.src_dirs_s = _mk_src_dirs_s(cmd_line.flag_args[+CmdFlag::SourceDirs]) ; } catch (::string const& e) { throw "bad source_dirs format : "+e ; }
		//
		::string top_repo_root_s ;
		jsrr.enter(
			/*out*/::ref(::vmap_s<MountAction>())
		,	/*out*/cmd_env
		,	/*out*/::ref(::vmap_ss())/*dyn_env*/
		,	/*out*/gather.first_pid
		,	/*out*/top_repo_root_s
		,	       *g_lmake_root_s
		,	       *g_repo_root_s
		,	       with_slash(tmp_dir)
		,	       0/*small_id*/
		) ;
		//
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	Status status ;
	try {
		BlockedSig blocked{{SIGINT}} ;
		gather.autodep_env       = ::move(autodep_env)   ;
		gather.autodep_env.views = job_space.flat_phys() ;
		gather.cmd_line          = cmd_line.args         ;
		gather.env               = &cmd_env              ;
		gather.method            = jsrr.method           ;
		//       vvvvvvvvvvvvvvvvvvv
		status = gather.exec_child() ;
		//       ^^^^^^^^^^^^^^^^^^^
	} catch (::string const& e) { exit(Rc::System,e) ; }
	//
	try                       { jsrr.exit() ;        }
	catch (::string const& e) { exit(Rc::System,e) ; }
	//
	::string files = "targets :\n" ;
	for( auto const& [target,ai] : gather.accesses )
		if (ai.first_write()<Pdate::Future) files << target <<'\n' ;
	files += "deps :\n" ;
	::string prev_dep         ;
	bool     prev_parallel    = false ;
	Pdate    prev_first_read  ;
	auto send = [&]( ::string const& dep={} , Pdate first_read={} ) {                                              // process deps with a delay of 1 because we need next entry for ascii art
		bool parallel = +first_read && first_read==prev_first_read ;
		if (+prev_dep) {
			if      ( !prev_parallel && !parallel ) files += "  "  ;
			else if ( !prev_parallel &&  parallel ) files += "/ "  ;
			else if (  prev_parallel &&  parallel ) files += "| "  ;
			else                                    files += "\\ " ;
			files << prev_dep << '\n' ;
		}
		prev_first_read = first_read ;
		prev_parallel   = parallel   ;
		prev_dep        = dep        ;
	} ;
	for( auto const& [dep,ai] : gather.accesses ) if (ai.first_write()==Pdate::Future) send(dep,ai.first_read()) ;
	/**/                                                                               send(                   ) ; // send last
	//
	if (cmd_line.flags[CmdFlag::Out]) Fd(cmd_line.flag_args[+CmdFlag::Out],Fd::Write).write(files) ;
	else                              Fd::Stdout                                     .write(files) ;
	return status!=Status::Ok ;
}
