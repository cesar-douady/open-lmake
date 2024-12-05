// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#if HAS_MEMFD
	#include <sys/mman.h>
#endif

#include "process.hh"
#include "thread.hh"

namespace Py {

	Mutex<MutexLvl::Gil> Gil::_s_mutex ;

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

	void init(::string const& lmake_root_s) {
		static bool once=false ; if (once) return ; else once = true ;
		#if PY_VERSION_HEX >= 0x03080000
			PyPreConfig pre_config ; PyPreConfig_InitIsolatedConfig(&pre_config) ;
			Py_PreInitialize(&pre_config) ;
			//
			PyConfig config ;                                   PyConfig_InitIsolatedConfig(&config) ; // ignore env variables and no user site dir
			wchar_t* python = Py_DecodeLocale(PYTHON,nullptr) ; SWEAR(python) ;
			config.write_bytecode = false  ;                                                           // be as non-intrusive as possible
			config.program_name   = python ;
			Py_InitializeFromConfig(&config) ;
		#else
			Py_IgnoreEnvironmentFlag = true ;                                                          // favor repeatability
			Py_NoUserSiteDirectory   = true ;                                                          // .
			Py_DontWriteBytecodeFlag = true ;                                                          // be as non-intrusive as possible
			Py_InitializeEx(0) ;                                                                       // skip initialization of signal handlers
		#endif
		//
		py_get_sys("implementation").set_attr("cache_tag",None) ;                                      // avoid pyc management
		//
		List& py_path = py_get_sys<List>("path") ;
		py_path.prepend( *Ptr<Str>(lmake_root_s+"lib") ) ;
		py_path.append ( *Ptr<Str>("."               ) ) ;
		#if PY_VERSION_HEX >= 0x03080000
			PyEval_SaveThread() ;
		#endif
	}

	// divert stderr to a memfd (if available, else to an internal pipe), call PyErr_Print and restore stderr
	::string py_err_str_clear() {
		static bool s_busy = false ;                                            // avoid recursion loop : fall back to printing error string if we cannot gather it
		if (s_busy) {
			PyErr_Print() ;
			return {} ;
		}
		Save          sav_busy     { s_busy , true }             ;
		::string      res          ;
		Object&       py_stderr    = py_get_sys("stderr")        ;
		Ptr<Callable> py_flush     ;
		AcFd          stderr_save  = Fd::Stderr.dup()            ;              // save stderr
		int           stderr_flags = ::fcntl(Fd::Stderr,F_GETFD) ;              // .
		{	SaveExc sav_exc { New } ;                                           // flush cannot be called if exception is set
			py_flush = py_stderr.get_attr<Callable>("flush") ;
			try { py_flush->call() ; } catch (::string const&) {}               // flush stderr buffer before manipulation (does not justify the burden of error in error if we cant)
		}
		auto read_all = [&](Fd fd)->void {
			char    buf[256] ;
			ssize_t c        ;
			while ( (c=read(fd,buf,sizeof(buf)))>0 ) res += ::string_view(buf,c) ;
		} ;
		#if HAS_MEMFD
			::dup2(AcFd(::memfd_create("back_trace",MFD_CLOEXEC)),Fd::Stderr) ; // name is for debug purpose only
			PyErr_Print() ;                                                     // clears exception
			try { py_flush->call() ; } catch (::string const&) {}               // flush stderr buffer after manipulation (does not justify the burden of error in error if we cant)
			::lseek( Fd::Stderr , 0 , SEEK_SET ) ;                              // rewind to read error message
			read_all(Fd::Stderr) ;
		#else
			Pipe fds { New } ;
			::dup2(fds.write,Fd::Stderr) ;
			{	::jthread gather { read_all , fds.read } ;
				PyErr_Print() ;                                                 // clears exception
				try { py_flush->call() ; } catch (::string const&) {}           // flush stderr buffer after manipulation (does not justify the burden of error in error if we cant)
				fds.write.close() ;                                             // all file descriptors to write side must be closed for the read side to see eof condition
				::close(Fd::Stderr) ;                                           // .
			}
			fds.read.close() ;
		#endif
		//
		::dup2 (stderr_save,Fd::Stderr                     ) ;                  // restore stderr
		::fcntl(            Fd::Stderr,F_SETFD,stderr_flags) ;                  // .
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

	void py_run( ::string const& text , Dict& env ) {
		Ptr<Object> rc { PyRun_String( text.c_str() , Py_file_input , env.to_py() , env.to_py() ) } ;
		if (!rc) throw py_err_str_clear() ;
	}

	Ptr<Dict> py_run (::string const& text) {
		Ptr<Dict> env = _mk_env() ;
		py_run(text,*env) ;
		return env ;
	}

	//
	// val methods (mostly for debug)
	//

	bool     Bool ::val () const { return bool    (self) ; }
	long     Int  ::val () const { return long    (self) ; }
	ulong    Int  ::uval() const { return ulong   (self) ; }
	double   Float::val () const { return double  (self) ; }
	::string Str  ::val () const { return ::string(self) ; }

	//
	// Object
	//

	Ptr<Str> Object::str() const {
		PyObject* s = PyObject_Str(to_py()) ;
		if (s) try { return s ; } catch (...) {}
		else   py_err_clear() ;
		return cat('<',type_name()," object at 0x",static_cast<void const*>(this),'>') ; // catch any error so calling str is reliable
	}

	//
	// Module
	//

	PyObject* Ptr<Module>::_s_mk_mod( ::string const& name , PyMethodDef* funcs ) {
		::string*    nm  = new ::string(name)   ;                                                                    // keep name alive
		size_t       nf1 = 1                    ; for( PyMethodDef* f=funcs ; f->ml_name ; f++ ) nf1++             ; // start at 1 to account for terminating sentinel
		PyMethodDef* fns = new PyMethodDef[nf1] ; for( size_t i : iota(nf1)                    ) fns[i] = funcs[i] ; // keep funcs alive
		#if PY_MAJOR_VERSION<3
			return Py_InitModule( nm->c_str() , fns ) ;
		#else
			PyModuleDef* def = new PyModuleDef {                                                                     // must have the lifetime of the module
				PyModuleDef_HEAD_INIT
			,	nm->c_str()                                                                                          // m_name
			,	nullptr                                                                                              // m_doc
			,	-1                                                                                                   // m_size
			,	fns                                                                                                  // m_methods
			,	nullptr                                                                                              // m_slots
			,	nullptr                                                                                              // m_traverse
			,	nullptr                                                                                              // m_clear
			,	nullptr                                                                                              // m_free
			} ;
			return PyModule_Create(def) ;
		#endif
	}

}
