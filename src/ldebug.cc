// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be first because Python.h must be first

#include "disk.hh"
#include "trace.hh"

#include "repo.hh"
#include "client.hh"

using namespace Disk ;
using namespace Py   ;

::string keys() {
	Gil gil ;
	try {
		Ptr<>    py_cfg_data = py_eval(AcFd(PRIVATE_ADMIN_DIR_S "config_data.py").read()) ;
		Object&  py_cfg      = py_cfg_data->as_a<Dict>().get_item("config")               ;
		Object&  py_dbgs     = py_cfg     . as_a<Dict>().get_item("debug" )               ;
		size_t   wk          = 0                                                          ;
		::string res         ;
		for( auto const& [py_k,_   ] : py_dbgs.as_a<Dict>() ) wk = ::max( wk , ::string(py_k.as_a<Str>()).size() ) ;
		for( auto const& [py_k,py_v] : py_dbgs.as_a<Dict>() ) res <<'\t'<< widen(::string(py_k.as_a<Str>()),wk) <<" : "<< ::string(py_v.as_a<Str>()) <<'\n' ;
		return res ;
	} catch (::string const&) { return {} ; } // dont list keys if we cannot gather them
}

int main( int argc , char* argv[] ) {
	repo_app_init({.read_only_ok=false}) ;
	Py::init(*g_lmake_root_s)            ;
	Trace trace("main") ;
	//
	ReqSyntax syntax {{
		{ ReqFlag::Key    , { .short_name='k' , .has_arg=true  , .doc="entry into config.debug to specify debug method\n"                 } }
	,	{ ReqFlag::NoExec , { .short_name='n' , .has_arg=false , .doc="dont execute, just generate files"                                 } }
	,	{ ReqFlag::StdTmp , { .short_name='t' , .has_arg=false , .doc="use standard tmp dir (LMAKE/debug/<job_id>/tmp) for job execution" } }
	,	{ ReqFlag::TmpDir , { .short_name='T' , .has_arg=true  , .doc="tmp provided dir for job execution"                                } }
	}} ;
	syntax.flags[+ReqFlag::Key]->doc <<' '<< keys() ; // add available keys to usage
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//
	if ( cmd_line.args.size()<1 ) syntax.usage(    "need a target to debug"                                ) ;
	if ( cmd_line.args.size()>1 ) syntax.usage(cat("cannot debug ",cmd_line.args.size()," targets at once")) ;
	//
	::vector_s script_files ;
	//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Rc rc = out_proc( script_files , ReqProc::Debug , false/*read_only*/ , false/*refresh_makefiles*/ , syntax , cmd_line ) ;
	//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (+rc) exit(rc) ;
	SWEAR( script_files.size()==1 , script_files ) ;
	::string& script_file = script_files[0] ;
	//
	char* exec_args[] = { script_file.data() , nullptr } ;
	//
	if (cmd_line.flags[ReqFlag::NoExec]) {
		Fd::Stderr.write(cat("script file : ",script_file,'\n')) ;
	} else {
		// START_OF_NO_COV coverage info are gathered upon exit, hence they are not collected if calling ::execv()
		Fd::Stderr.write(cat("executing : ",script_file,'\n')) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		::execv(script_file.c_str(),exec_args) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		exit(Rc::System,"could not run ",script_file) ;
		// END_OF_NO_COV
	}
}
