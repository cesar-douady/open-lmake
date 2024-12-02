// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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

ENUM( CmdKey , None )
ENUM( CmdFlag
,	AutodepMethod
,	AutoMkdir
,	ChrootDir
,	Cwd
,	Env
,	IgnoreStat
,	Job
,	KeepEnv
,	KeepTmp
,	LinkSupport
,	Out
,	RootView
,	SourceDirs
,	TmpDir
,	TmpSizeMb
,	TmpView
,   Views
,	WorkDir
)

static ::vmap_s<JobSpace::ViewDescr> _mk_views(::string const& views) {
	::vmap_s<JobSpace::ViewDescr> res ;
	Gil                           gil ;
	if (+views) {
		Ptr<Object> py_views = py_eval(views) ;                   // hold objet in a Ptr
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
		Ptr<Object> py_src_dirs = py_eval(src_dirs) ;                    // keep Python object alive during iteration
		for( Object const&  py_src_dir : py_src_dirs->as_a<Sequence>() )
			res.push_back(with_slash(py_src_dir.as_a<Str>())) ;
	}
	return res ;
}

static ::vmap_ss _mk_env( ::string const& keep_env , ::string const& env ) {
	::uset_s  seen ;
	::vmap_ss res  ;
	Gil       gil  ;
	// use an intermediate variable (py_keep_env and py_env) to keep Python object alive during iteration
	if (+keep_env) {
		Ptr<Object> py_keep_env = py_eval(keep_env) ;                // hold in Ptr<> while iterating over
		for( Object const&  py_k : py_keep_env->as_a<Sequence>() ) {
			::string k = py_k.as_a<Str>() ;
			if (has_env(k)) {
				throw_if( seen.contains(k) , "cannot keep ",k," twice" ) ;
				res.emplace_back(k,get_env(k)) ;
				seen.insert(k) ;
			}
		}
	}
	if (+env) {
		Ptr<Object> py_env = py_eval(env) ;
		for( auto const& [py_k,py_v] : py_env->as_a<Dict>() ) {
			::string k = py_k.as_a<Str>() ;
			throw_if( seen.contains(k) ,  "cannot keep ",k," and provide it" ) ;
			res.emplace_back(k,py_v.as_a<Str>()) ;
		}
	}
	return res ;
}

int main( int argc , char* argv[] ) {
    block_sigs({SIGCHLD}) ;
	app_init(true/*read_only_ok*/,false/*cd_root*/) ;
	Py::init(*g_lmake_dir_s) ;
	::string dbg_dir_s = *g_root_dir_s+AdminDirS+"debug/" ;
	mk_dir_s(dbg_dir_s) ;
	AcFd lock_fd { no_slash(dbg_dir_s) } ;
	if (::flock(lock_fd,LOCK_EX|LOCK_NB)!=0) {                                                                                // because we have no small_id, we can only run a single instance
		if (errno==EWOULDBLOCK) exit(Rc::Fail  ,"cannot run several debug jobs simultaneously"            ) ;
		else                    exit(Rc::System,"cannot lock ",no_slash(dbg_dir_s)," : ",::strerror(errno)) ;
	}
	//
	Syntax<CmdKey,CmdFlag,false/*OptionsAnywhere*/> syntax{{
		// PER_AUTODEP_METHOD : complete doc on line below
		{ CmdFlag::AutoMkdir     , { .short_name='a' , .has_arg=false , .doc="automatically create dir upon chdir"                                                                       } }
	,	{ CmdFlag::ChrootDir     , { .short_name='c' , .has_arg=true  , .doc="dir which to chroot to before execution"                                                                   } }
	,	{ CmdFlag::Cwd           , { .short_name='d' , .has_arg=true  , .doc="current working directory in which to execute job"                                                         } }
	,	{ CmdFlag::Env           , { .short_name='e' , .has_arg=true  , .doc="environment variables to set, given as a python dict"                                                      } }
	,	{ CmdFlag::IgnoreStat    , { .short_name='i' , .has_arg=false , .doc="stat-like syscalls do not trigger dependencies"                                                            } }
	,	{ CmdFlag::Job           , { .short_name='j' , .has_arg=true  , .doc="job  index keep tmp dir if mentioned"                                                                      } }
	,	{ CmdFlag::KeepEnv       , { .short_name='k' , .has_arg=true  , .doc="list of environment variables to keep, given as a python tuple/list"                                       } }
	,	{ CmdFlag::LinkSupport   , { .short_name='l' , .has_arg=true  , .doc="level of symbolic link support (none, file, full), default=full"                                           } }
	,	{ CmdFlag::AutodepMethod , { .short_name='m' , .has_arg=true  , .doc="method used to detect deps (none, ld_audit, ld_preload, ld_preload_jemalloc, ptrace)"                      } }
	,	{ CmdFlag::Out           , { .short_name='o' , .has_arg=true  , .doc="output accesses file"                                                                                      } }
	,	{ CmdFlag::RootView      , { .short_name='r' , .has_arg=true  , .doc="name under which repo top-level dir is seen"                                                               } }
	,	{ CmdFlag::SourceDirs    , { .short_name='s' , .has_arg=true  , .doc="source dirs given as a python tuple/list, all elements must end with /"                                    } }
	,	{ CmdFlag::TmpSizeMb     , { .short_name='S' , .has_arg=true  , .doc="size of tmp dir"                                                                                           } }
	,	{ CmdFlag::TmpView       , { .short_name='t' , .has_arg=true  , .doc="name under which tmp dir is seen"                                                                          } }
	,	{ CmdFlag::KeepTmp       , { .short_name='T' , .has_arg=false , .doc="keep tmp dir after execution"                                                                              } }
	,	{ CmdFlag::Views         , { .short_name='v' , .has_arg=true  , .doc="view mapping given as a python dict mapping views to dict {'upper':upper,'lower':lower,'copy_up':copy_up}" } }
	}} ;
	CmdLine<CmdKey,CmdFlag> cmd_line { syntax , argc , argv } ;
	//
	JobStartRpcReply start_info  ;
	JobSpace  &      job_space   = start_info.job_space   ;
	AutodepEnv&      autodep_env = start_info.autodep_env ;
	::map_ss         cmd_env     ;
	Gather           gather      ;
	//
	try {
		throw_if( !cmd_line.args                                                                          , "no exe to launch"                                                       ) ;
		throw_if(  cmd_line.flags[CmdFlag::ChrootDir] && !is_abs(cmd_line.flag_args[+CmdFlag::ChrootDir]) , "chroot dir must be absolute : ",cmd_line.flag_args[+CmdFlag::ChrootDir] ) ;
		throw_if(  cmd_line.flags[CmdFlag::RootView ] && !is_abs(cmd_line.flag_args[+CmdFlag::RootView ]) , "root view must be absolute : " ,cmd_line.flag_args[+CmdFlag::RootView ] ) ;
		throw_if(  cmd_line.flags[CmdFlag::TmpView  ] && !is_abs(cmd_line.flag_args[+CmdFlag::TmpView  ]) , "tmp view must be absolute : "  ,cmd_line.flag_args[+CmdFlag::TmpView  ] ) ;
		//
		if (cmd_line.flags[CmdFlag::Cwd          ]) start_info.cwd_s        = with_slash            (cmd_line.flag_args[+CmdFlag::Cwd          ]) ;
		/**/                                        start_info.keep_tmp     =                        cmd_line.flags    [ CmdFlag::KeepTmp      ]  ;
		/**/                                        start_info.key          =                        "debug"                                      ;
		if (cmd_line.flags[CmdFlag::AutodepMethod]) start_info.method       = mk_enum<AutodepMethod>(cmd_line.flag_args[+CmdFlag::AutodepMethod]) ;
		if (cmd_line.flags[CmdFlag::TmpSizeMb    ]) start_info.tmp_sz_mb    = from_string<size_t>   (cmd_line.flag_args[+CmdFlag::TmpSizeMb    ]) ;
		if (cmd_line.flags[CmdFlag::ChrootDir    ]) job_space.chroot_dir_s  = with_slash            (cmd_line.flag_args[+CmdFlag::ChrootDir    ]) ;
		if (cmd_line.flags[CmdFlag::RootView     ]) job_space.root_view_s   = with_slash            (cmd_line.flag_args[+CmdFlag::RootView     ]) ;
		if (cmd_line.flags[CmdFlag::TmpView      ]) job_space.tmp_view_s    = with_slash            (cmd_line.flag_args[+CmdFlag::TmpView      ]) ;
		/**/                                        autodep_env.auto_mkdir  =                        cmd_line.flags    [ CmdFlag::AutoMkdir    ]  ;
		/**/                                        autodep_env.ignore_stat =                        cmd_line.flags    [ CmdFlag::IgnoreStat   ]  ;
		if (cmd_line.flags[CmdFlag::LinkSupport  ]) autodep_env.lnk_support = mk_enum<LnkSupport>   (cmd_line.flag_args[+CmdFlag::LinkSupport]) ;
		/**/                                        autodep_env.views       = job_space.flat_phys()                                          ;
		//
		try { start_info.env         = _mk_env       (cmd_line.flag_args[+CmdFlag::KeepEnv],cmd_line.flag_args[+CmdFlag::Env]) ; } catch (::string const& e) { throw "bad env format : "        +e ; }
		try { job_space.views        = _mk_views     (cmd_line.flag_args[+CmdFlag::Views     ]                               ) ; } catch (::string const& e) { throw "bad views format : "      +e ; }
		try { autodep_env.src_dirs_s = _mk_src_dirs_s(cmd_line.flag_args[+CmdFlag::SourceDirs]                               ) ; } catch (::string const& e) { throw "bad source_dirs format : "+e ; }
		//
		(void)start_info.enter(
			::ref(::vmap_s<MountAction>()) , cmd_env , ::ref(::string())/*phy_tmp_dir_s*/ , ::ref(::vmap_ss())/*dynamic_env*/ // outs
		,	gather.first_pid , from_string<JobIdx>(cmd_line.flag_args[+CmdFlag::Job]) , *g_root_dir_s , 0                     // ins
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
		gather.cwd_s             = start_info.cwd_s      ;
		gather.env               = &cmd_env              ;
		gather.method            = start_info.method     ;
		//       vvvvvvvvvvvvvvvvvvv
		status = gather.exec_child() ;
		//       ^^^^^^^^^^^^^^^^^^^
	} catch (::string const& e) { exit(Rc::System,e) ; }
	//
	try                       { start_info.exit() ;  }
	catch (::string const& e) { exit(Rc::System,e) ; }
	//
	::string deps = "targets :\n" ;
	for( auto const& [target,ai] : gather.accesses ) switch(ai.digest.write) {
		case No    :                               break ;
		case Maybe : deps <<"? "<< target <<'\n' ; break ;
		case Yes   : deps <<"  "<< target <<'\n' ; break ;
	}
	deps += "deps :\n" ;
	::string prev_dep         ;
	bool     prev_parallel    = false ;
	Pdate    prev_first_read  ;
	auto send = [&]( ::string const& dep={} , Pdate first_read={} ) {                                                         // process deps with a delay of 1 because we need next entry for ascii art
		bool parallel = +first_read && first_read==prev_first_read ;
		if (+prev_dep) {
			if      ( !prev_parallel && !parallel ) deps += "  "  ;
			else if ( !prev_parallel &&  parallel ) deps += "/ "  ;
			else if (  prev_parallel &&  parallel ) deps += "| "  ;
			else                                    deps += "\\ " ;
			deps << prev_dep << '\n' ;
		}
		prev_first_read = first_read ;
		prev_parallel   = parallel   ;
		prev_dep        = dep        ;
	} ;
	for( auto const& [dep,ai] : gather.accesses ) if (ai.digest.write==No) send(dep,ai.first_read().first) ;
	/**/                                                                   send(                         ) ;                  // send last
	//
	if (cmd_line.flags[CmdFlag::Out]) Fd(cmd_line.flag_args[+CmdFlag::Out],Fd::Write).write(deps) ;
	else                              Fd::Stdout                                     .write(deps) ;
	return status!=Status::Ok ;
}
