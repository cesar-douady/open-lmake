// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <Python.h>

#include "lib.hh"
#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "support.hh"

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

static ::string _mk_str( PyObject* o , ::string const& arg_name={} ) {
	/**/                                  if (!o) throw to_string("missing argument"       ,+arg_name?" ":"",arg_name          ) ;
	PyObject* s   = PyObject_Str(o)     ; if (!s) throw to_string("cannot convert argument",+arg_name?" ":"",arg_name," to str") ;
	::string  res = PyUnicode_AsUTF8(s) ;
	Py_DECREF(s) ;
	return res ;
}

static uint8_t _mk_uint8( PyObject* o , ::string const& arg_name={} ) {
	if (!o) throw to_string("missing argument",+arg_name?" ":"",arg_name) ;
	int  overflow = 0/*garbage*/                          ;
	long val      = PyLong_AsLongAndOverflow(o,&overflow) ;
	if ( overflow || val<0 || val>::numeric_limits<uint8_t>::max() ) throw to_string("overflow for argument",+arg_name?" ":"",arg_name) ;
	return uint8_t(val) ;
}

static ::vector_s _get_files(PyObject* args) {
	::vector_s res  ;
	ssize_t  n_args = PyTuple_GET_SIZE(args) ;
	//
	auto push = [&](PyObject* o)->void {
		if (PyObject_IsTrue(o)) res.push_back(_mk_str(o)) ;
	} ;
	//
	if (n_args==1) {
		args = PyTuple_GET_ITEM(args,0) ;
		if      (PyTuple_Check(args)) { ssize_t n = PyTuple_Size(args) ; res.reserve(n) ; for( ssize_t i=0 ; i<n ; i++ ) push(PyTuple_GET_ITEM(args,i)) ; }
		else if (PyList_Check (args)) { ssize_t n = PyList_Size (args) ; res.reserve(n) ; for( ssize_t i=0 ; i<n ; i++ ) push(PyList_GET_ITEM (args,i)) ; }
		else                          {                                  res.reserve(1) ;                                push(                 args   ) ; }
	} else {
		/**/                            ssize_t n = PyTuple_Size(args) ; res.reserve(n) ; for( ssize_t i=0 ; i<n ; i++ ) push(PyTuple_GET_ITEM(args,i)) ;
	}
	for( size_t i=0 ; i<res.size() ; i++ ) SWEAR(+res[i]) ;
	return res ;
}

static PyObject* decode( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_args    =      PyTuple_GET_SIZE(args)     ;
	ssize_t n_kw_args = kw ? PyDict_Size     (kw  ) : 0 ;
	try {
		::string code = _mk_str( n_args>0 ? PyTuple_GET_ITEM(args,0) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"code")) : nullptr , "code" ) ;
		::string file = _mk_str( n_args>1 ? PyTuple_GET_ITEM(args,1) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"file")) : nullptr , "file" ) ;
		::string ctx  = _mk_str( n_args>2 ? PyTuple_GET_ITEM(args,2) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"ctx" )) : nullptr , "ctx"  ) ;
		//
		if (n_kw_args) throw "unexpected keyword arg"s ;
		//
		JobExecRpcReq   jerr  = JobExecRpcReq( Proc::Decode , ::move(file) , ::move(code) , ::move(ctx) ) ;
		JobExecRpcReply reply = _g_autodep_support.req(jerr)                                              ;
		if (reply.ok!=Yes) throw reply.txt ;
		return PyUnicode_FromString(reply.txt.c_str()) ;
	} catch (::string const& e) {
		if (!PyErr_Occurred()) PyErr_SetString(PyExc_TypeError,e.c_str()) ;
		return nullptr ;
	}
}

static PyObject* encode( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_args    =      PyTuple_GET_SIZE(args)     ;
	ssize_t n_kw_args = kw ? PyDict_Size     (kw  ) : 0 ;
	try {
		::string val     = _mk_str  ( n_args>0 ? PyTuple_GET_ITEM(args,0) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"value"  )) : nullptr , "code"    ) ;
		::string file    = _mk_str  ( n_args>1 ? PyTuple_GET_ITEM(args,1) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"file"   )) : nullptr , "file"    ) ;
		::string ctx     = _mk_str  ( n_args>2 ? PyTuple_GET_ITEM(args,2) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"ctx"    )) : nullptr , "ctx"     ) ;
		uint8_t  min_len = _mk_uint8( n_args>3 ? PyTuple_GET_ITEM(args,3) : kw ? (n_kw_args--,PyDict_GetItemString(kw,"min_len")) : nullptr , "min_len" ) ;
		//
		if (n_kw_args) throw "unexpected keyword arg"s ;
		if (min_len>MaxCodecBits) throw to_string("min_len (",min_len,") cannot be larger max allowed code bits (",MaxCodecBits,')') ;
		//
		JobExecRpcReq   jerr  = JobExecRpcReq( Proc::Encode , ::move(file) , ::move(val) , ::move(ctx) , min_len ) ;
		JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                       ;
		if (reply.ok!=Yes) throw reply.txt ;
		return PyUnicode_FromString(reply.txt.c_str()) ;
	} catch (::string const& e) {
		if (!PyErr_Occurred()) PyErr_SetString(PyExc_TypeError,e.c_str()) ;
		return nullptr ;
	}
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
		if (!files) return PyDict_New() ;                               // fast path : depend on no files
		//
		JobExecRpcReq   jerr  = JobExecRpcReq( Proc::DepInfos , ::move(files) , accesses , dflags , no_follow ) ;
		JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                    ;
		SWEAR( reply.dep_infos.size()==jerr.files.size() , reply.dep_infos.size() , jerr.files.size() ) ;
		PyObject* res = PyDict_New() ;
		for( size_t i=0 ; i<reply.dep_infos.size() ; i++ ) {
			PyObject* v = PyTuple_New(2) ;
			switch (reply.dep_infos[i].first) {
				case Yes   : Py_INCREF(Py_True ) ; PyTuple_SET_ITEM(v,0,Py_True ) ; break ;
				case Maybe : Py_INCREF(Py_None ) ; PyTuple_SET_ITEM(v,0,Py_None ) ; break ;
				case No    : Py_INCREF(Py_False) ; PyTuple_SET_ITEM(v,0,Py_False) ; break ;
				default : FAIL(reply.dep_infos[i].first) ;
			}
			// answer returned value, even if dep is out-of-date, as if crc turns out to be correct, the job will not be rerun
			PyTuple_SET_ITEM( v , 1 , PyUnicode_FromString(::string(reply.dep_infos[i].second).c_str()) ) ;
			PyDict_SetItemString( res , jerr.files[i].first.c_str() , v ) ;
			Py_DECREF(v) ;
		}
		return res ;
	} else {
		_g_autodep_support.req( JobExecRpcReq( Proc::Access , ::move(files) , {.accesses=accesses,.dflags=dflags} , no_follow , "depend" ) ) ;
		Py_RETURN_NONE ;
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
	SWEAR( views.size()==1 , views.size() ) ;
	::string const& view = views[0] ;
	if (!view) return PyUnicode_FromString("") ;
	//
	RealPath::SolveReport solve_report = RealPath(_g_autodep_support).solve(view,no_follow) ;
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

static PyObject* target( PyObject* /*null*/ , PyObject* args , PyObject* kw ) {
	ssize_t n_kw_args  = kw ? PyDict_Size(kw) : 0 ;
	bool    unlink     = false                    ;
	bool    no_follow  = false                    ;
	Tflags  neg_tflags ;
	Tflags  pos_tflags ;
	if (n_kw_args) {
		if ( PyObject* py_v = PyDict_GetItemString(kw,"unlink"         ) ) { n_kw_args-- ; unlink    =  PyObject_IsTrue(py_v) ; }
		if ( PyObject* py_v = PyDict_GetItemString(kw,"follow_symlinks") ) { n_kw_args-- ; no_follow = !PyObject_IsTrue(py_v) ; }
	}
	if (!unlink) pos_tflags = {Tflag::Crc,Tflag::Write} ;                      // we declare that we write, allow it and compute crc by default
	if (n_kw_args) {
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
	JobExecRpcReq   jerr  = JobExecRpcReq( Proc::Access , ::move(files) , {.neg_tflags=neg_tflags,.pos_tflags=pos_tflags,.write=!unlink,.unlink=unlink} , no_follow , "target" ) ;
	JobExecRpcReply reply = _g_autodep_support.req(jerr)                                                                                                                         ;
	//
	Py_RETURN_NONE ;
}

static PyMethodDef funcs[] = {
	{	"check_deps"
	,	reinterpret_cast<PyCFunction>(chk_deps)
	,	METH_VARARGS|METH_KEYWORDS
	,	"check_deps(verbose=False). Ensure that all previously seen deps are up-to-date."
	}
,	{	"decode"
	,	reinterpret_cast<PyCFunction>(decode)
	,	METH_VARARGS|METH_KEYWORDS
	,	"decode(code,file,ctx). Return the associated value passed by encode(value,file,ctx)."
	}
,	{	"depend"
	,	reinterpret_cast<PyCFunction>(depend)
	,	METH_VARARGS|METH_KEYWORDS
	,	"depend(dep1,dep2,...,verbose=False,follow_symlinks=True,critical=False,ignore_error=False,essential=False). Mark all arguments as parallel dependencies."
	}
,	{	"encode"
	,	reinterpret_cast<PyCFunction>(encode)
	,	METH_VARARGS|METH_KEYWORDS
	,	"encode(value,file,ctx,min_length=1). Return a code associated with value. If necessary create such a code of length at least min_length after a checksum computed after value."
	}
,	{	"has_backend"
	,	reinterpret_cast<PyCFunction>(has_backend)
	,	METH_VARARGS|METH_KEYWORDS
	,	"has_backend(backend). Return true if the corresponding backend is implememented."
	}
,	{	"search_sub_root_dir"
	,	reinterpret_cast<PyCFunction>(search_sub_root_dir)
	,	METH_VARARGS|METH_KEYWORDS
	,	"search_sub_root_dir(cwd=os.getcwd()). Return the nearest hierarchical root dir relative to the actual root dir."
	}
,	{	"target"
	,	reinterpret_cast<PyCFunction>(target)
	,	METH_VARARGS|METH_KEYWORDS
	,	"target(target1,target2,...,unlink=False,follow_symlinks=True,<flags>=<leave as is>). Mark all arguments as targets."
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
	if (!has_env("LMAKE_AUTODEP_ENV")) {
		try                     { _g_autodep_support.root_dir = search_root_dir().first ; }
		catch (::string const&) { _g_autodep_support.root_dir = cwd()                   ; }
	}
	PyObject* backends_py = PyTuple_New(1+HAS_SLURM) ;                             // PER_BACKEND : add an entry here
	/**/           PyTuple_SET_ITEM(backends_py,0,PyUnicode_FromString("local")) ;
	if (HAS_SLURM) PyTuple_SET_ITEM(backends_py,1,PyUnicode_FromString("slurm")) ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	PyModule_AddStringConstant( mod , "root_dir"       , _g_autodep_support.root_dir.c_str() ) ;
	PyModule_AddObject        ( mod , "backends"       , backends_py                         ) ;
	PyObject_SetAttrString    ( mod , "has_ld_audit"   , HAS_LD_AUDIT ? Py_True : Py_False   ) ;
	PyObject_SetAttrString    ( mod , "has_ld_preload" ,                Py_True              ) ;
	PyObject_SetAttrString    ( mod , "has_ptrace"     ,                Py_True              ) ;
	PyObject_SetAttrString    ( mod , "no_crc"         , PyLong_FromLong(+Crc::Unknown)      ) ;
	PyObject_SetAttrString    ( mod , "crc_no_file"    , PyLong_FromLong(+Crc::None   )      ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mod ;
}
#pragma GCC visibility pop
