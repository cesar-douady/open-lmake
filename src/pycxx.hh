// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "CXX/Objects.hxx"
#include "CXX/Extensions.hxx"

#include "utils.hh"

namespace Py {

	void init(bool multi_thread=false) ;                                       // if multi_thread, GIL must be acquired before each any call to Python API

	extern PyObject* g_ellipsis ;

	static inline PyObject* eval_dict(bool printed_expr=false) {
		PyObject* res = PyDict_New() ;
		PyDict_SetItemString( res , "__builtins__" , PyEval_GetBuiltins() ) ; // Python3.6 and Python3.8 do not provide it for us and is harmless for Python3.10
		if (printed_expr) {
			PyDict_SetItemString( res , "inf" , *Py::Float(Infinity) ) ;      // this is how non-finite floats are printed with print
			PyDict_SetItemString( res , "nan" , *Py::Float(nan("") ) ) ;      // .
		}
		return res ;
	}

	// like PyErr_Print, but return text instead of printing it (Python API does not provide any means to do this !)
	::string err_str() ;

	struct Gil {
		Gil() : state{PyGILState_Ensure()} {}
		~Gil() { PyGILState_Release(state) ; }
		// data
		PyGILState_STATE state ;
	} ;

}
