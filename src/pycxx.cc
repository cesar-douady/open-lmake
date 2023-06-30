// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "pycxx.hh"

namespace Py {

	PyObject* Ellipsis = nullptr ;

	void init() {
		static bool once=false ; if (once) return ; else once = true ;
		Py_Initialize() ;
		PyObject* eval_env = PyDict_New() ;
		Ellipsis = PyRun_String("...",Py_eval_input,eval_env,eval_env) ;
		Py_DECREF(eval_env) ;
	}
	static bool _inited = at_init(1,init) ;

	::ostream& operator<<( ::ostream& os , Pattern const& pat ) {
		return os << "Pattern(" << pat.pattern() << ")" ;
	}

	// Divert stderr to a pipe, call PyErr_Print and restore stderr
	// This could be simpler by using memfd_create, but this is not available for CentOS7.
	::string err_str() {
		::string res ;
		Pipe fds{New} ;
		Fd stderr_save = Fd::Stderr.dup() ;
		::dup2(fds.write,Fd::Stderr) ;
		auto gather_thread_func = [&]()->void {
			char buf[256] ;
			ssize_t c ;
			while ( (c=read(fds.read,buf,sizeof(buf)))>0 ) res += ::string_view(buf,c) ;
		} ;
		::jthread gather{gather_thread_func} ;
		PyErr_Print() ;
		gather.join() ;
		::dup2(stderr_save,Fd::Stderr) ;                                       // restore
		fds.close() ;
		return res ;
	}

}
