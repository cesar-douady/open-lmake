// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <Python.h>

#include "lib.hh"
#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "autodep_support.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

using Proc = JobExecRpcProc ;

static AutodepSupport _g_autodep_support ;

static PyObject* _record( PyObject* args , Proc proc ) {
	Cmd const&    cmd   = g_proc_tab.at(proc)          ;
	size_t        n     = args? PyTuple_Size(args) : 0 ;
	JobExecRpcReq jerr  ;
	if (cmd.has_args) {
		if (n==1) {
			if (
				PyObject* first_arg = PyTuple_GET_ITEM(args,0) ;
				!PyUnicode_Check(first_arg) && PySequence_Check(first_arg)         // /!\ : strings are sequences in Python
			) {
				args = first_arg             ;                                     // called with a single sequence : this is the sequence of args
				n    = PySequence_Size(args) ;
			}
		}
		::vector_s files ; files.reserve(n) ;
		for( size_t i=0 ; i<n ; i++ ) {
			PyObject* item = PySequence_GetItem(args,i) ;
			if (!PyUnicode_Check(item)) {
				Py_DECREF(item) ;
				PyErr_SetString(PyExc_TypeError,"dependencies must be strings") ;
				return NULL ;
			}
			PyUnicode_READY(item) ;
			files.emplace_back(reinterpret_cast<char*>(PyUnicode_1BYTE_DATA(item))) ;
			Py_DECREF(item) ;
		}
		if (proc==Proc::Deps) jerr = JobExecRpcReq(proc,files,DepAccesses::All,cmd.sync) ;
		else                  jerr = JobExecRpcReq(proc,files,                 cmd.sync) ;
	} else if (!n) {
		jerr = JobExecRpcReq(proc,cmd.sync) ;
	} else {
		PyErr_SetString(PyExc_TypeError,"no argument supported") ;
		return NULL ;
	}
	//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvv
	JobExecRpcReply reply = _g_autodep_support.req(jerr) ;
	//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (cmd.sync) {
		if ( cmd.has_ok && !reply.ok ) {
			PyErr_SetString(PyExc_RuntimeError,"check failed") ;
			return NULL ;
		}
		if (cmd.has_crcs) {
			PyObject* res = PyTuple_New(n) ;
			SWEAR(reply.crcs.size()==n) ;
			for( size_t i=0 ; i<n ; i++ ) PyTuple_SET_ITEM(res,i,PyLong_FromLong(+reply.crcs[i])) ;
			return res ;
		}
	}
	Py_RETURN_NONE ;
}

static PyObject* critical_barrier( PyObject* /*null*/ , PyObject* args ) { return _record(args,Proc::CriticalBarrier) ; }
static PyObject* chk_deps        ( PyObject* /*null*/ , PyObject* args ) { return _record(args,Proc::ChkDeps        ) ; }
static PyObject* depend          ( PyObject* /*null*/ , PyObject* args ) { return _record(args,Proc::Deps           ) ; }
static PyObject* target          ( PyObject* /*null*/ , PyObject* args ) { return _record(args,Proc::Targets        ) ; }
static PyObject* unlink          ( PyObject* /*null*/ , PyObject* args ) { return _record(args,Proc::Unlinks        ) ; }
static PyObject* dep_crcs        ( PyObject* /*null*/ , PyObject* args ) { return _record(args,Proc::DepCrcs        ) ; }
//
static PyObject* search_sub_root_dir( PyObject* /*null*/ , PyObject* args ) {
	size_t sz = PyTuple_Size(args) ;
	if (sz>1) {
		PyErr_SetString(PyExc_TypeError,"expect at most a single argument") ;
		return NULL ;
	}
	::string view ;
	if (sz==0) {
		view = cwd() ;
	} else {
		PyObject* py_view = PyTuple_GET_ITEM(args,0) ;
		PyUnicode_READY(py_view) ;
		view = reinterpret_cast<char*>(PyUnicode_1BYTE_DATA(py_view)) ;
	}
	view += '/' ;
	if (!(view+'/').starts_with(_g_autodep_support.root_dir+'/')) {
		PyErr_SetString(PyExc_ValueError,"not in repository") ;
		return NULL ;
	}
	::string sub_root_dir = search_root_dir(view).first ;
	if (sub_root_dir.size()<_g_autodep_support.root_dir.size()) {
		PyErr_SetString(PyExc_RuntimeError,"root dir not found") ;
		return NULL ;
	}
	sub_root_dir += '/'                                                       ;
	sub_root_dir  = sub_root_dir.substr(_g_autodep_support.root_dir.size()+1) ;
	return PyUnicode_FromString(sub_root_dir.c_str()) ;
}

static PyMethodDef funcs[] = {
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	{ "check_deps"       , chk_deps         , METH_NOARGS  , "check_deps(). Ensure that all previously seen deps are up-to-date"                                 }
,	{ "critical_barrier" , critical_barrier , METH_NOARGS  , "critical_barrier(). Mark following deps as non-critical"                                           }
,	{ "dep_crcs"         , dep_crcs         , METH_VARARGS , "dep_crcs(dep1,dep2,...). Return crc of the deps as a tuple of int. Also mark them as dependencies" }
,	{ "depend"           , depend           , METH_VARARGS , "depend(dep1,dep2,...). Mark all arguments as dependencies in a parallel way"                       }
,	{ "target"           , target           , METH_VARARGS , "target(dep1,dep2,...). Mark all arguments as targets"                                              }
,	{ "unlink"           , unlink           , METH_VARARGS , "unlink(dep1,dep2,...). Mark all arguments as unlinked targets"                                     }
//
,	{ "search_sub_root_dir" , search_sub_root_dir , METH_VARARGS , "search_sub_root_dir(cwd=os.getcwd()). Return the nearest hierarchical root dir relative to the actual root dir" }
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
,	{nullptr,nullptr,0,nullptr}/*sentinel*/
} ;

static struct PyModuleDef lmake_module = {
	PyModuleDef_HEAD_INIT
,	"lmake"
,	nullptr
,	-1
,	funcs
,	nullptr
,	nullptr
,	nullptr
,	nullptr
} ;

#pragma GCC visibility push(default)
PyMODINIT_FUNC PyInit_clmake() {
	PyObject* mod = PyModule_Create(&lmake_module) ;
	//
	_g_autodep_support = New ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	PyModule_AddStringConstant( mod , "root_dir"       , _g_autodep_support.root_dir.c_str() ) ;
	PyObject_SetAttrString    ( mod , "has_ld_audit"   , HAS_LD_AUDIT ? Py_True : Py_False   ) ;
	PyObject_SetAttrString    ( mod , "has_ld_preload" ,                Py_True              ) ;
	PyObject_SetAttrString    ( mod , "has_ptrace"     , HAS_PTRACE   ? Py_True : Py_False   ) ;
	PyObject_SetAttrString    ( mod , "no_crc"         , PyLong_FromLong(+Crc::Unknown)      ) ;
	PyObject_SetAttrString    ( mod , "crc_no_file"    , PyLong_FromLong(+Crc::None   )      ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mod ;
}
#pragma GCC visibility pop
