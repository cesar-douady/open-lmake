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
}

static void _push_str( ::vector_s& v , PyObject* o) {
	if (!PyObject_IsTrue(o)) return ;
	PyObject* s = PyObject_Str(o) ;
	if (!s) throw "cannot convert argument to str"s ;
	v.emplace_back(PyUnicode_AsUTF8(s)) ;
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
	ssize_t  n_kw_args = kw ? PyDict_Size(kw) : 0 ;
	bool     verbose   = false                    ;
	bool     no_follow = false                    ;
	Accesses accesses  = Accesses::All            ;
	Dflags   dflags    = DfltDflags               ;
	if (n_kw_args) {
		if ( PyObject* py_v = PyDict_GetItemString(kw,"verbose"        ) ) { n_kw_args-- ; verbose   =  PyObject_IsTrue(py_v) ; }
		if ( PyObject* py_v = PyDict_GetItemString(kw,"follow_symlinks") ) { n_kw_args-- ; no_follow = !PyObject_IsTrue(py_v) ; }
		for( Access a : Access::N )
			if (PyObject* py_v = PyDict_GetItemString(kw,mk_snake(a).c_str())) {
				n_kw_args-- ;
				if (PyObject_IsTrue(py_v)) accesses |=  a ;
				else                       accesses &= ~a ;
			}
		for( Dflag df=Dflag::HiddenMin ; df<Dflag::HiddenMax1 ; df++ )
			if (PyObject* py_v = PyDict_GetItemString(kw,mk_snake(df).c_str())) {
				n_kw_args-- ;
				if (PyObject_IsTrue(py_v)) dflags |=  df ;
				else                       dflags &= ~df ;
			}
		if (n_kw_args) { PyErr_SetString(PyExc_TypeError,"unexpected keyword arg") ; return nullptr ; }
	}
	::vector_s files ;
	try                       { files = _get_files(args) ;                                    }
	catch (::string const& e) { PyErr_SetString(PyExc_TypeError,e.c_str()) ; return nullptr ; }
	//
	if (verbose) {
		if (files.empty()) return PyDict_New() ;                               // fast path : depend on no files
		//
		JobExecRpcReq   jerr  = JobExecRpcReq( Proc::DepInfos , ::move(files) , accesses , dflags , no_follow , "depend" ) ;
		JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                               ;
		SWEAR(reply.infos.size()==jerr.files.size()) ;
		PyObject* res = PyDict_New() ;
		for( size_t i=0 ; i<reply.infos.size() ; i++ ) {
			PyObject* v = PyTuple_New(2) ;
			switch (reply.infos[i].first) {
				case Yes   : Py_INCREF(Py_True ) ; PyTuple_SET_ITEM(v,0,Py_True ) ; break ;
				case Maybe : Py_INCREF(Py_None ) ; PyTuple_SET_ITEM(v,0,Py_None ) ; break ;
				case No    : Py_INCREF(Py_False) ; PyTuple_SET_ITEM(v,0,Py_False) ; break ;
				default : FAIL(reply.infos[i].first) ;
			}
			// answer returned value, even if dep is out-of-date, as if crc turns out to be correct, the job will not be rerun
			PyTuple_SET_ITEM( v , 1 , PyUnicode_FromString(::string(reply.infos[i].second).c_str()) ) ;
			PyDict_SetItemString( res , jerr.files[i].first.c_str() , v ) ;
			Py_DECREF(v) ;
		}
		return res ;
	} else {
		_g_autodep_support.req( JobExecRpcReq( Proc::Access , ::move(files) , {.accesses=accesses,.dflags=dflags} , no_follow , "depend" ) ) ;
		Py_RETURN_NONE ;
	}
}

static PyObject* target( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_kw_args  = kw ? PyDict_Size(kw) : 0 ;
	bool    unlink     = false                    ;
	bool    no_follow  = false                    ;
	Tflags  neg_tflags ;
	Tflags  pos_tflags ;
	if (n_kw_args) {
		if ( PyObject* py_v = PyDict_GetItemString(kw,"unlink"         ) ) { n_kw_args-- ; unlink    =  PyObject_IsTrue(py_v) ; }
		if ( PyObject* py_v = PyDict_GetItemString(kw,"follow_symlinks") ) { n_kw_args-- ; no_follow = !PyObject_IsTrue(py_v) ; }
		for( Tflag tf=Tflag::HiddenMin ; tf<Tflag::HiddenMax1 ; tf++ )
			if (PyObject* py_v = PyDict_GetItemString(kw,mk_snake(tf).c_str())) {
				n_kw_args-- ;
				if (PyObject_IsTrue(py_v)) pos_tflags |= tf ;
				else                       neg_tflags |= tf ;
			}
		if (n_kw_args) { PyErr_SetString(PyExc_TypeError,"unexpected keyword arg") ; return nullptr ; }
	}
	if ( unlink && (+neg_tflags||+pos_tflags) ) { PyErr_SetString(PyExc_TypeError,"cannot unlink and set target flags") ; return nullptr ; }
	::vector_s files ;
	try                       { files = _get_files(args) ;                                    }
	catch (::string const& e) { PyErr_SetString(PyExc_TypeError,e.c_str()) ; return nullptr ; }
	//
	JobExecRpcReq   jerr  = JobExecRpcReq( Proc::Access , ::move(files) , {.write=!unlink,.neg_tflags=neg_tflags,.pos_tflags=pos_tflags,.unlink=unlink} , no_follow , "target" ) ;
	JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                                                                                         ;
	//
	Py_RETURN_NONE ;
}

static PyObject* search_sub_root_dir( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	if (PyTuple_GET_SIZE(args)>1) {
		PyErr_SetString(PyExc_TypeError,"expect at most a single argument") ;
		return nullptr ;
	}
	ssize_t n_kw_args = kw ? PyDict_Size(kw) : 0 ;
	bool    no_follow = false                    ;
	if (n_kw_args) {
		if ( PyObject* py_v = PyDict_GetItemString(kw,"no_follow") ) { n_kw_args-- ; no_follow = PyObject_IsTrue(py_v) ;                            }
		if ( n_kw_args                                             ) { PyErr_SetString(PyExc_TypeError,"unexpected keyword arg") ; return nullptr ; }
	}
	::vector_s views = _get_files(args) ;
	if (views.size()==0) views.push_back(cwd()) ;
	SWEAR(views.size()==1) ;
	::string const& view = views[0] ;
	if (view.empty()) return PyUnicode_FromString("") ;
	//
	RealPath::SolveReport solve_report = RealPath({.lnk_support=_g_autodep_support.lnk_support}).solve(view,no_follow) ;
	//
	switch (solve_report.kind) {
		case Kind::Root :
			return PyUnicode_FromString("") ;
		case Kind::Repo :
			try {
				::string abs_path         = mk_abs(solve_report.real,_g_autodep_support.root_dir+'/') ;
				::string abs_sub_root_dir = search_root_dir(abs_path).first                           ; abs_sub_root_dir += '/' ;
				return PyUnicode_FromString( abs_sub_root_dir.c_str()+_g_autodep_support.root_dir.size()+1 ) ;
			} catch (::string const&e) {
				PyErr_SetString(PyExc_ValueError,e.c_str()) ;
				return nullptr ;
			}
		default :
			PyErr_SetString(PyExc_ValueError,"cannot find sub root dir in repository") ;
			return nullptr ;
	}
}

static PyObject* has_backend( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	if ( PyTuple_GET_SIZE(args)!=1 || kw ) {
		PyErr_SetString(PyExc_TypeError,"expect exactly a single positional argument") ;
		return nullptr ;
	}
	PyObject* py_be = PyTuple_GET_ITEM(args,0) ;
	if (!PyUnicode_Check(py_be)) {
		PyErr_SetString(PyExc_TypeError,"argument must be a str") ;
		return nullptr ;
	}
	const char* be  = PyUnicode_AsUTF8(py_be) ;
	BackendTag  tag = BackendTag::Unknown     ;
	try {
		tag = mk_enum<BackendTag>(be) ;
	} catch (::string const& e) {
		PyErr_SetString(PyExc_ValueError,("unknown backend "s+be).c_str()) ;
		return nullptr ;
	}
	switch (tag) {
		case BackendTag::Local : Py_RETURN_TRUE                    ;
		case BackendTag::Slurm : return PyBool_FromLong(HAS_SLURM) ;
		default : FAIL(tag) ;
	} ;
}

static PyMethodDef funcs[] = {
	{	"check_deps"
	,	reinterpret_cast<PyCFunction>(chk_deps)
	,	METH_VARARGS|METH_KEYWORDS
	,	"check_deps(verbose=False). Ensure that all previously seen deps are up-to-date."
	}
,	{	"depend"
	,	reinterpret_cast<PyCFunction>(depend)
	,	METH_VARARGS|METH_KEYWORDS
	,	"depend(dep1,dep2,...,verbose=False,follow_symlinks=True,critical=False,ignore_error=False,essential=False). Mark all arguments as parallel dependencies."
	}
,	{	"target"
	,	reinterpret_cast<PyCFunction>(target)
	,	METH_VARARGS|METH_KEYWORDS
	,	"target(target1,target2,...,unlink=False,follow_symlinks=True,<flags>=<leave as is>). Mark all arguments as targets."
	}
,	{	"search_sub_root_dir"
	,	reinterpret_cast<PyCFunction>(search_sub_root_dir)
	,	METH_VARARGS|METH_KEYWORDS
	,	"search_sub_root_dir(cwd=os.getcwd()). Return the nearest hierarchical root dir relative to the actual root dir."
	}
,	{	"has_backend"
	,	reinterpret_cast<PyCFunction>(has_backend)
	,	METH_VARARGS|METH_KEYWORDS
	,	"has_backend(backend). Return true if the corresponding backend is implememented."
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
	if (!has_env("LMAKE_AUTODEP_ENV")) _g_autodep_support.root_dir = search_root_dir().first ;
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
