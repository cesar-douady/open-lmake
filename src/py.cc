// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "process.hh"
#include "thread.hh"

namespace Py {

	//
	// functions
	//

	struct SaveExc {
		// cxtors & casts
		SaveExc (       ) = default ;
		SaveExc (NewType) { PyErr_Fetch  ( &exc , &val , &tb ) ; }
		~SaveExc(       ) { PyErr_Restore(  exc ,  val ,  tb ) ; }
		// data
		PyObject* exc = nullptr ;
		PyObject* val = nullptr ;
		PyObject* tb  = nullptr ;
	} ;

	void init( ::string const& lmake_dir , bool multi_thread ) {
		static bool once=false ; if (once) return ; else once = true ;
		Py_IgnoreEnvironmentFlag = true ;                              // favor repeatability
		Py_NoUserSiteDirectory   = true ;                              // .
		Py_DontWriteBytecodeFlag = true ;                              // be as non-intrusive as possible
		Py_InitializeEx(0) ;                                           // skip initialization of signal handlers
		List& py_path = py_get_sys<List>("path") ;
		py_path.prepend( *Ptr<Str>(lmake_dir+"/lib") ) ;
		py_path.append ( *Ptr<Str>("."             ) ) ;
		if (multi_thread) /*PyThreadState**/ PyEval_SaveThread() ;
	}

	// divert stderr to a pipe, call PyErr_Print and restore stderr
	// this would be simpler by using memfd_create if it was available on CentOS7
	::string py_err_str_clear() {
		static bool s_busy = false ; // avoid recursion loop : fall back to printing error string if we cannot gather it
		if (s_busy) {
			PyErr_Print() ;
			return {} ;
		}
		Save sav_busy { s_busy , true } ;
		//
		::string      res          ;
		Object&       py_stderr    = py_get_sys("stderr")        ;
		Ptr<Callable> py_flush     ;
		Pipe          fds          { New }                       ;
		Fd            stderr_save  = Fd::Stderr.dup()            ;
		int           stderr_flags = ::fcntl(Fd::Stderr,F_GETFD) ;
		//
		auto gather_thread_func = [&]()->void {
			char    buf[256] ;
			ssize_t c        ;
			while ( (c=read(fds.read,buf,sizeof(buf)))>0 ) res += ::string_view(buf,c) ;
		} ;
		// flush stderr buffer as we will directly manipulate the file descriptor
		{	SaveExc sav_exc { New } ;                                           // flush cannot be called if exception is set
			py_flush = py_stderr.get_attr<Callable>("flush") ;
			try { py_flush->call() ; } catch (::string const&) { py_err_clear() ; }
		}
		::close(Fd::Stderr) ;
		::dup2(fds.write,Fd::Stderr) ;
		fds.write.close() ;
		{	::jthread gather { gather_thread_func } ;
			PyErr_Print() ;                                                         // clears exception
			// flush again stderr buffer as we will again directly manipulate the file descriptor
			try { py_flush->call() ; } catch (::string const&) { py_err_clear() ; } // exception has been cleared, no need to save and restore
			::close(Fd::Stderr) ;
		}
		::dup2 (stderr_save,Fd::Stderr                     ) ;                      // restore stderr
		::fcntl(            Fd::Stderr,F_SETFD,stderr_flags) ;                      // restore flags
		stderr_save.close() ;
		fds.read   .close() ;
		return res ;
	}

	static Ptr<Dict> _mk_env() {
		Ptr<Dict> res{New} ;
		res->set_item( "__builtins__" , *Dict::s_builtins()   ) ; // Python3.6 and Python3.8 do not provide it for us and is harmless for Python3.10
		res->set_item( "inf"          , *Ptr<Float>(Infinity) ) ; // this is how non-finite floats are printed with print
		res->set_item( "nan"          , *Ptr<Float>(Nan     ) ) ; // .
		return res ;
	}

	Ptr<Object> py_eval(::string const& expr) {
		Ptr<Dict> env = _mk_env() ;
		Ptr<Object> res { PyRun_String( expr.c_str() , Py_eval_input , env->to_py() , env->to_py() ) } ;
		if (!res) throw py_err_str_clear() ;
		return res ;
	}

	Ptr<Dict> py_run(::string const& text) {
		Ptr<Dict  > env = _mk_env() ;
		Ptr<Object> rc { PyRun_String( text.c_str() , Py_file_input , env->to_py() , env->to_py() ) } ;
		if (!rc) throw py_err_str_clear() ;
		return env ;
	}

	//
	// Object
	//

	Ptr<Str> Object::str() const {
		PyObject* s = PyObject_Str(to_py()) ;
		if (s) try { return s ; } catch (...) {}
		else   py_err_clear() ;
		return to_string('<',type_name()," object at 0x",static_cast<void const*>(this),'>') ; // catch any error so calling str is reliable
	}

	//
	// Module
	//

	PyObject* Ptr<Module>::_s_mk_mod( ::string const& name , PyMethodDef* funcs ) {
		#if PY_MAJOR_VERSION<3
			return Py_InitModule( name.c_str() , funcs ) ;
		#else
			PyModuleDef* def = new PyModuleDef { // must have the lifetime of the module
				PyModuleDef_HEAD_INIT
			,	name.c_str()                     // m_name
			,	nullptr                          // m_doc
			,	-1                               // m_size
			,	funcs                            // m_methods
			,	nullptr                          // m_slots
			,	nullptr                          // m_traverse
			,	nullptr                          // m_clear
			,	nullptr                          // m_free
			} ;
			return PyModule_Create(def) ;
		#endif
	}

}
