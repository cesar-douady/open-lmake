// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

const char* _to_c_str(PyObject* u) {
	return reinterpret_cast<const char*>(PyUnicode_1BYTE_DATA(u)) ;
}

static PyObject* critical_barrier( PyObject* /*null*/ , PyObject* /*null*/ ) {
	JobExecRpcReply reply = _g_autodep_support.req(JobExecRpcReq(Proc::CriticalBarrier,true/*sync*/)) ; // we must be sync to be sure that subsequent deps are after this call
	Py_RETURN_NONE ;
}

static PyObject* chk_deps( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_args = PyTuple_GET_SIZE(args) + (kw?PyDict_Size(kw):0) ;
	if (n_args>1) { PyErr_SetString(PyExc_TypeError,"too many args") ; return nullptr ; }
	bool verbose = false ;
	if (n_args) {
		PyObject* py_verbose = PyTuple_GET_SIZE(args) ? PyTuple_GET_ITEM(args,0) : PyDict_GetItemString(kw,"verbose") ;
		if (!py_verbose) { PyErr_SetString(PyExc_TypeError,"unexpected keyword arg") ; return nullptr ; }
		verbose = PyObject_IsTrue(py_verbose) ;
	}
	JobExecRpcReply reply = _g_autodep_support.req(JobExecRpcReq(Proc::ChkDeps,verbose/*sync*/)) ;
	if (!verbose) Py_RETURN_NONE ;
	switch (reply.ok) {
		case Yes   :                                                                   Py_RETURN_TRUE  ;
		case Maybe : PyErr_SetString(PyExc_RuntimeError,"some deps are out-of-date") ; return nullptr  ;
		case No    :                                                                   Py_RETURN_FALSE ;
		default : FAIL(reply.ok) ;
	}
	if (reply.ok==Maybe) { PyErr_SetString(PyExc_RuntimeError,"some deps are out-of-date") ; return nullptr ; }
	Py_RETURN_NONE ;
}

static void _push_str( ::vector_s& v , PyObject* o) {
	if (!PyObject_IsTrue(o)) return ;
	PyObject* s = PyObject_Str(o) ;
	if (!s) throw "cannot convert argument to str"s ;
	v.emplace_back(_to_c_str(s)) ;
	Py_DECREF(s) ;
}
static ::vector_s _get_files( PyObject* args ) {
	::vector_s res ;
	ssize_t n_args = PyTuple_GET_SIZE(args) ;
	if (n_args==1) {
		args = PyTuple_GET_ITEM(args,0) ;
		if      (PyTuple_Check(args)) { ssize_t n = PyTuple_Size(args) ; res.reserve(n) ; for( ssize_t i=0 ; i<n ; i++ ) _push_str( res , PyTuple_GET_ITEM(args,i) ) ; }
		else if (PyList_Check (args)) { ssize_t n = PyList_Size (args) ; res.reserve(n) ; for( ssize_t i=0 ; i<n ; i++ ) _push_str( res , PyList_GET_ITEM (args,i) ) ; }
		else                          {                                  res.reserve(1) ;                                _push_str( res ,                  args    ) ; }
	} else {
		/**/                          { ssize_t n = PyTuple_Size(args) ; res.reserve(n) ; for( ssize_t i=0 ; i<n ; i++ ) _push_str( res , PyTuple_GET_ITEM(args,i) ) ; }
	}
	return res ;
}

static PyObject* depend( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_kw_args  = kw ? PyDict_Size(kw) : 0 ;
	bool    verbose    = false                    ;
	DFlags  flags      = StaticDFlags             ;
	if (n_kw_args) {
		if ( PyObject* py_v = PyDict_GetItemString(kw,"verbose") ) {
			n_kw_args-- ;
			verbose = PyObject_IsTrue(py_v) ;
		}
		for( DFlag df : DFlag::N) {
			if (df>=DFlag::Private) break ;
			if (PyObject* py_v = PyDict_GetItemString(kw,mk_snake(df).c_str())) {
				n_kw_args-- ;
				if (PyObject_IsTrue(py_v)) flags |=  df ;
				else                       flags &= ~df ;
			}
		}
		if (n_kw_args) { PyErr_SetString(PyExc_TypeError,"unexpected keyword arg") ; return nullptr ; }
	}
	::vector_s files ;
	try                       { files = _get_files(args) ;                                    }
	catch (::string const& e) { PyErr_SetString(PyExc_TypeError,e.c_str()) ; return nullptr ; }
	JobExecRpcReq   jerr  = JobExecRpcReq( verbose?Proc::DepInfos:Proc::Deps , files , flags , verbose/*sync*/ ) ;
	JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                        ;
	//
	if (!verbose) Py_RETURN_NONE ;
	//
	SWEAR(reply.infos.size()==files.size()) ;
	PyObject* res = PyDict_New() ;
	for( size_t i=0 ; i<files.size() ; i++ ) {
		PyObject* v = PyTuple_New(2) ;
		switch (reply.infos[i].first) {
			case Yes   : Py_INCREF(Py_True ) ; PyTuple_SET_ITEM(v,0,Py_True ) ; break ;
			case Maybe : Py_INCREF(Py_None ) ; PyTuple_SET_ITEM(v,0,Py_None ) ; break ;
			case No    : Py_INCREF(Py_False) ; PyTuple_SET_ITEM(v,0,Py_False) ; break ;
			default : FAIL(reply.infos[i].first) ;
		}
		if (reply.ok==Maybe) { PyErr_SetString(PyExc_RuntimeError,"some deps are out-of-date") ; return nullptr ; }
		PyTuple_SET_ITEM(v,1,PyLong_FromLong(+reply.infos[i].second)) ;
		PyDict_SetItemString( res , files[i].c_str() , v ) ;
		Py_DECREF(v) ;
	}
	return res ;
}

static PyObject* target( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_kw_args  = kw ? PyDict_Size(kw) : 0 ;
	bool    unlink     = false                    ;
	if (n_kw_args) {
		if ( PyObject* py_v = PyDict_GetItemString(kw,"unlink") ) {
			n_kw_args-- ;
			unlink = PyObject_IsTrue(py_v) ;
		}
		if (n_kw_args) { PyErr_SetString(PyExc_TypeError,"unexpected keyword arg") ; return nullptr ; }
	}
	::vector_s files ;
	try                       { files = _get_files(args) ;                                    }
	catch (::string const& e) { PyErr_SetString(PyExc_TypeError,e.c_str()) ; return nullptr ; }
	JobExecRpcReq  jerr   = JobExecRpcReq( unlink?Proc::Unlinks:Proc::Targets , files , false/*sync*/ ) ;
	JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                     ;
	//
	Py_RETURN_NONE ;
}

static PyObject* search_sub_root_dir( PyObject* /*null*/ , PyObject* args ) {
	if (PyTuple_GET_SIZE(args)>1) {
		PyErr_SetString(PyExc_TypeError,"expect at most a single argument") ;
		return nullptr ;
	}
	::vector_s views = _get_files(args) ;
	if (views.size()==0) views.push_back(cwd()) ;
	SWEAR(views.size()==1) ;
	::string& view = views[0] ;
	SWEAR(!view.empty()) ;
	if (view[0]!='/'    ) view = to_string(cwd(),'/',view) ;
	if (view.back()=='/') view.pop_back() ;
	if (!(view+'/').starts_with(_g_autodep_support.root_dir+'/')) {
		PyErr_SetString(PyExc_ValueError,"not in repository") ;
		return nullptr ;
	}
	::string sub_root_dir = search_root_dir(view).first ;
	if (sub_root_dir.size()<_g_autodep_support.root_dir.size()) {
		PyErr_SetString(PyExc_RuntimeError,"root dir not found") ;
		return nullptr ;
	}
	sub_root_dir += '/'                                                       ;
	sub_root_dir  = sub_root_dir.substr(_g_autodep_support.root_dir.size()+1) ;
	return PyUnicode_FromString(sub_root_dir.c_str()) ;
}

static PyMethodDef funcs[] = {
	{	"critical_barrier"
	,	critical_barrier
	,	METH_NOARGS
	,	"critical_barrier(). Mark following deps as less critical than previous ones"
	}
,	{	"check_deps"
	,	(PyCFunction)chk_deps
	,	METH_VARARGS|METH_KEYWORDS
	,	"check_deps(verbose=False). Ensure that all previously seen deps are up-to-date"
	}
,	{	"depend"
	,	reinterpret_cast<PyCFunction>(depend)
	,	METH_VARARGS|METH_KEYWORDS
	,	"depend(dep1,dep2,...,verbose=False,essential=True,error=True,required=True). Mark all arguments as parallel dependencies"
	}
,	{	"target"
	,	reinterpret_cast<PyCFunction>(target)
	,	METH_VARARGS|METH_KEYWORDS
	,	"target(target1,target2,...,unlink=False). Mark all arguments as targets"
	}
,	{	"search_sub_root_dir"
	,	reinterpret_cast<PyCFunction>(search_sub_root_dir)
	,	METH_VARARGS
	,	"search_sub_root_dir(cwd=os.getcwd()). Return the nearest hierarchical root dir relative to the actual root dir"
	}
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
