// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "process.hh"

static const ::amap<PyException,PyObject*,N<PyException>> PyExceptionTab {{
	{ PyException::OsErr      , PyExc_OSError      }
,	{ PyException::RuntimeErr , PyExc_RuntimeError }
,	{ PyException::TypeErr    , PyExc_TypeError    }
,	{ PyException::ValueErr   , PyExc_ValueError   }
	#if PY_MAJOR_VERSION>=3
	,	{ PyException::FileNotFoundErr , PyExc_FileNotFoundError }
	#endif
}} ;

namespace Py {

	Mutex<MutexLvl::Gil> Gil::_s_mutex ;

	//
	// functions
	//

	struct SaveExc {
		// cxtors & casts
		SaveExc () = default ;
		SaveExc (NewType) { Gil::s_swear_locked() ; PyErr_Fetch  ( &exc , &val , &tb ) ; }
		~SaveExc(       ) { Gil::s_swear_locked() ; PyErr_Restore(  exc ,  val ,  tb ) ; }
		// data
		PyObject* exc = nullptr ;
		PyObject* val = nullptr ;
		PyObject* tb  = nullptr ;
	} ;

	static StaticUniqPtr<::vector_s> _g_std_sys_path ;

	void init(::string const& lmake_root_s) {
		static bool once=false ; if (once) return ; else once = true ;
		//
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
		NoGil no_gil ;                                                                                 // tell our mutex we already have the GIL
		//
		py_get_sys("implementation").set_attr("cache_tag",None) ;                                      // avoid pyc management
		//
		List& py_path = py_get_sys<List>("path") ;
		if (+lmake_root_s) py_path.append( *Ptr<Str>(lmake_root_s+"lib") ) ;
		//
		SWEAR( !_g_std_sys_path , _g_std_sys_path ) ;
		/**/                       _g_std_sys_path = New ;
		for( Object& p : py_path ) _g_std_sys_path->emplace_back(p.as_a<Str>()) ;
		//
		Dict::s_builtins = from_py<Dict>(PyEval_GetBuiltins()) ;
		//
		#if PY_VERSION_HEX >= 0x03080000
			PyEval_SaveThread() ;
		#endif
	}

	void py_reset_sys_path() {
		SWEAR(+_g_std_sys_path) ;
		Ptr<Tuple> py_sys_path { _g_std_sys_path->size() } ; for( size_t i : iota(_g_std_sys_path->size()) ) py_sys_path->set_item( i , *Ptr<Str>((*_g_std_sys_path)[i]) ) ;
		py_set_sys("path",*py_sys_path) ;
	}

	nullptr_t py_err_set( PyException e , ::string const& txt ) {
		[[maybe_unused]] static bool once = ( SWEAR(chk_enum_tab(PyExceptionTab)) , true ) ; // cannot be static_assert'ed because PyExceptionTab cannot be constexpr, although keys are all constexpr
		Gil::s_swear_locked() ;
		PyErr_SetString(PyExceptionTab[+e].second,txt.c_str()) ;
		return nullptr ;
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
		auto read_all = [&](Fd fd) {
			#if !HAS_MEMFD
				t_thread_key = 'Y' ;
			#endif
			char    buf[256] ;
			ssize_t c        ;
			while ( (c=::read(fd,buf,sizeof(buf)))>0 ) res += ::string_view(buf,c) ;
		} ;
		#if HAS_MEMFD
			::dup2(AcFd(::memfd_create("back_trace",MFD_CLOEXEC)),Fd::Stderr) ; // name is for debug purpose only
			PyErr_Print() ;                                                     // clears exception
			try { py_flush->call() ; } catch (::string const&) {}               // flush stderr buffer after manipulation (does not justify the burden of error in error if we cant)
			::lseek( Fd::Stderr , 0/*offset*/ , SEEK_SET ) ;                    // rewind to read error message
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

	static Ptr<Dict> _mk_glbs() {
		Ptr<Dict> res { New } ;
		res->set_item( "inf" , *Ptr<Float>(Inf) ) ; // this is how non-finite floats are printed with print
		res->set_item( "nan" , *Ptr<Float>(Nan) ) ; // .
		return res ;
	}

	template<bool Run> static Ptr<::conditional_t<Run,Dict,Object>> _py_eval_run( ::string const& expr , Dict* glbs , Sequence const* sys_path ) {
		Gil::s_swear_locked() ;
		Ptr<Dict> fresh_glbs ;
		if (!glbs) glbs = fresh_glbs = _mk_glbs() ;
		WithSysPath  wsp { sys_path } ;
		WithBuiltins wb  { *glbs    } ;
		Ptr<> res = PyRun_String( expr.c_str() , Run?Py_file_input:Py_eval_input , glbs->to_py() , glbs->to_py() ) ;
		if constexpr (Run) return glbs ;
		else               return res  ;
	}
	Ptr<    > py_eval( ::string const& expr , Dict* glbs , Sequence const* sys_path ) { return _py_eval_run<false/*Run*/>( expr , glbs , sys_path ) ; }
	Ptr<Dict> py_run ( ::string const& expr , Dict* glbs , Sequence const* sys_path ) { return _py_eval_run<true /*Run*/>( expr , glbs , sys_path ) ; }

	::string py_fstr_escape(::string const& s) {
		static constexpr ::array<bool,256> IsSpecial = []() {
			::array<bool,256> res = {} ;
			for( char const* p = "{}" ; *p ; p++ ) res[*p] = true ;
			return res ;
		}() ;
		::string res ; res.reserve(s.size()) ; // typically nothing to double
		for( char c : s ) {
			if (IsSpecial[c]) res += c ;       // double specials
			/**/              res += c ;
		}
		return res ;
	}

	//
	// val methods (mostly for debug)
	//

	bool     Bool ::val () const { return bool    (self) ; }
	long     Int  ::val () const { return long    (self) ; }
	ulong    Int  ::uval() const { return ulong   (self) ; }
	double   Float::val () const { return double  (self) ; }
	::string Str  ::val () const { return ::string(self) ; }
	#if PY_MAJOR_VERSION>=3
		::string Bytes::val() const { return ::string(self) ; }
	#endif

	//
	// Object
	//

	Callable* Ptr<Object>::s_dumps = nullptr ;
	Callable* Ptr<Object>::s_loads = nullptr ;

	Ptr<Str> Object::repr() const {
		try                     { return PyObject_Repr(to_py()) ;                                           }
		catch (::string const&) { py_err_clear() ; return cat("'<",type_name()," object at 0x",this,">/") ; } // catch any error so calling repr is reliable
	}

	Ptr<Str> Object::str() const {
		try                     { return PyObject_Str(to_py()) ;                                          }
		catch (::string const&) { py_err_clear() ; return cat('<',type_name()," object at 0x",this,'>') ; } // catch any error so calling str is reliable
	}

	//
	// Dict
	//

	Dict* Dict::s_builtins = nullptr ;

	//
	// Module
	//

	Ptr<Module>::Ptr( ::string const& name ) {
		Gil::s_swear_locked() ;
		// XXX> : use PyImport_ImportModuleEx with a non-empy from_list when python2 support is no longer required
		Ptr<Module> py_top = PyImport_ImportModule(name.c_str()) ;            // in case of module in a package, PyImport_ImportModule returns the top level package
		if (name.find('.')==Npos) self = py_top                             ;
		else                      self = &py_get_sys<Dict>("modules")[name] ; // it is a much more natural API to return the asked module, get it from sys.modules
	}

	//
	// Code
	//

	template<bool Run> static Ptr<::conditional_t<Run,Dict,Object>> _code_eval_run( Code const& code , Dict* glbs , Sequence const* sys_path ) {
		Gil::s_swear_locked() ;
		#if PY_MAJOR_VERSION<3
			PyCodeObject* c = ::launder(reinterpret_cast<PyCodeObject*>(code.to_py())) ;
		#else
			PyObject    * c =                                           code.to_py()   ;
		#endif
		Ptr<Dict> fresh_glbs ;
		if (!glbs) glbs = fresh_glbs = _mk_glbs() ;
		WithSysPath  wsp { sys_path } ;
		WithBuiltins wb  { *glbs    } ;
		Ptr<> res = PyEval_EvalCode( c , glbs->to_py() , nullptr ) ;
		if constexpr (Run) return glbs ;
		else               return res  ;
	} //!                                                                                       Run
	Ptr<    > Code::eval( Dict* glbs , Sequence const* sys_path ) const { return _code_eval_run<false>( self , glbs , sys_path ) ; }
	Ptr<Dict> Code::run ( Dict* glbs , Sequence const* sys_path ) const { return _code_eval_run<true >( self , glbs , sys_path ) ; }

}
