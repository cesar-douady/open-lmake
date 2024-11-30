// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be first because Python.h must be first

#include "app.hh"
#include "disk.hh"
#include "client.hh"
#include "rpc_client.hh"
#include "trace.hh"

using namespace Disk ;
using namespace Py   ;

::string keys() {
	Gil gil ;
	try {
		Ptr<Object> py_cfg_data = py_eval(AcFd(ADMIN_DIR_S "lmake/config_data.py").read()) ;
		Object&     py_cfg      = py_cfg_data->as_a<Dict>().get_item("config")             ;
		Object&     py_dbgs     = py_cfg     . as_a<Dict>().get_item("debug" )             ;
		::string res   = "(" ;
		First    first ;
		for( auto const& [py_k,py_v] : py_dbgs.as_a<Dict>() ) res <<first("",",")<< ::string(py_k.as_a<Str>()) ;
		res += ')' ;
		return res ;
	} catch (::string const&) { return {} ; } // dont list keys if we cannot gather them
}

int main( int argc , char* argv[] ) {
	app_init(false/*read_only_ok*/) ;
	Py::init(*g_lmake_dir_s       )   ;
	Trace trace("main") ;
	//
	ReqSyntax syntax{{},{
		{ ReqFlag::Key     , { .short_name='k' , .has_arg=true  , .doc="entry into config.debug to specify debug method" } }
	,	{ ReqFlag::NoExec  , { .short_name='n' , .has_arg=false , .doc="dont execute, just generate files"               } }
	,	{ ReqFlag::KeepTmp , { .short_name='t' , .has_arg=false , .doc="keep tmp dir after job execution"                } }
	}} ;
	syntax.flags[+ReqFlag::Key].doc <<' '<< keys() ;                // add available keys to usage
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	if (has_env("TMPDIR")) {                                        // provide TMPDIR env var in case job specifies TMPDIR as ...
		cmd_line.flags                       |= ReqFlag::TmpDir   ;
		cmd_line.flag_args[+ReqFlag::TmpDir]  = get_env("TMPDIR") ;
	}
	//
	if ( cmd_line.args.size()<1 ) syntax.usage("need a target to debug"                                ) ;
	if ( cmd_line.args.size()>1 ) syntax.usage("cannot debug "s+cmd_line.args.size()+" targets at once") ;
	//
	::vector_s script_files ;
	//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( script_files , ReqProc::Debug , false/*read_only*/ , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( Rc rc=mk_rc(ok) ; +rc ) exit(rc) ;
	SWEAR(script_files.size()==1,script_files) ;
	::string& script_file = script_files[0] ;
	//
	char* exec_args[] = { script_file.data() , nullptr } ;
	//
	if (cmd_line.flags[ReqFlag::NoExec]) {
		Fd::Stdout.write("script file : "s+script_file+'\n') ;
	} else {
		Fd::Stderr.write("executing : "s+script_file+'\n') ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		::execv(script_file.c_str(),exec_args) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//
		exit(Rc::System,"could not run ",script_file) ;
	}
}
