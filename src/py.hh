// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <Python.h> // /!\ must be included first because doc says so (else it does not work)

#include "serialize.hh"
#include "trace.hh"

#if PY_MAJOR_VERSION>=3
	enum class PyException : uint8_t { OsErr , RuntimeErr , TypeErr , ValueErr , FileNotFoundErr } ;
#else
	enum class PyException : uint8_t { OsErr , RuntimeErr , TypeErr , ValueErr                   } ;
#endif

namespace Py {

	struct Object       ;
	struct NoneType     ;
	struct EllipsisType ;
	struct Bool         ;
	struct Int          ;
	struct Float        ;
	struct Str          ;
	struct Sequence     ;
	struct Tuple        ;
	struct List         ;
	struct Dict         ;
	struct Code         ;
	struct Module       ;
	struct Callable     ;
	#if PY_MAJOR_VERSION<3
		using Bytes = Str ;
	#else
		struct Bytes ;
	#endif
	//
	template<class T=Object> struct Ptr ;

	struct Gil {
		friend struct NoGil ;
		// statics
		static void s_swear_locked() { _s_mutex.swear_locked() ; }
		// static data
	private :
		static Mutex<MutexLvl::Gil> _s_mutex ;
		// cxtors & casts
	public :
		Gil () : _lock{_s_mutex} { Trace trace("Gil::acquire") ; _state = PyGILState_Ensure (      ) ; }
		~Gil()                   { Trace trace("Gil::release") ;          PyGILState_Release(_state) ; }
		// data
	private :
		Lock<Mutex<MutexLvl::Gil>> _lock  ;
		PyGILState_STATE           _state ;
	} ;

	struct NoGil {
		// cxtors & casts
		NoGil () : _lock{Gil::_s_mutex} { Trace trace("NoGil::acquire") ; }
		~NoGil()                        { Trace trace("NoGil::release") ; }
		// data
	private :
		Lock<Mutex<MutexLvl::Gil>> _lock  ;
	} ;

	struct SavPyLdLibraryPath {
		static constexpr bool HasPyLdLibraryPath = PY_LD_LIBRARY_PATH[0]!=0 ;
		SavPyLdLibraryPath() {
			if (HasPyLdLibraryPath) {
				sav = get_env("LD_LIBRARY_PATH") ;
				if (+sav) set_env( "LD_LIBRARY_PATH" , sav+':'+PY_LD_LIBRARY_PATH ) ;
				else      set_env( "LD_LIBRARY_PATH" ,         PY_LD_LIBRARY_PATH ) ;
			}
		}
		~SavPyLdLibraryPath() {
			if (HasPyLdLibraryPath) set_env( "LD_LIBRARY_PATH" , sav ) ;
		}
		//
		::string sav ;
	} ;

	//
	// helpers
	//

	inline void py_err_clear   () {        PyErr_Clear   () ; }
	inline bool py_err_occurred() { return PyErr_Occurred() ; }
	//
	::string py_err_str_clear() ; // like PyErr_Print, but return text instead of printing it (python API provides no means to do this !)
	//
	nullptr_t py_err_set( PyException e , ::string const& txt ) ;

	template<class T> T const* _chk(Object const* o) ;
	template<class T> T      * _chk(Object      * o) ;
	//
	template<class T=Object> T         * from_py(PyObject  * o) { T* r = static_cast<T*>(o) ; { if (!r               ) throw py_err_str_clear() ; } _chk<T>(r) ; return r ; }
	inline                   char const* from_py(char const* s) {                             { if (!s               ) throw py_err_str_clear() ; }              return s ; }
	inline                   int         from_py(int         i) {                             { if (i<0              ) throw py_err_str_clear() ; }              return i ; }
	inline                   void        from_py(             ) {                               if (py_err_occurred()) throw py_err_str_clear() ;                           }

	//
	// Object
	//

	struct Object : PyObject {
		static constexpr const char* Name = "object" ;
		// cxtors & casts
		Object(Object&&) = delete ;                                          // manipulating objects is python's responsibility
		Object& operator=(Object&&) = delete ;                               // .
		// accesses
		bool operator==(Object const& other) const { return this==&other ; }
		bool qualify() const { return true ; }                               // everything qualifies as object
		//
		template<class T> T      & as_a()       { return *_chk<T>(this)                         ; }
		template<class T> T const& as_a() const { return *_chk<T>(this)                         ; }
		template<class T> bool     is_a() const { return static_cast<T const*>(this)->qualify() ; }
		//
		bool operator+() const {
			Gil::s_swear_locked() ;
			return bool(from_py(PyObject_IsTrue(to_py()))) ;
		}
		constexpr PyObject    * to_py      ()       {           return                     this  ;                                     }
		constexpr PyObject    * to_py      () const {           return const_cast<Object*>(this) ;                                     }
		/**/      PyObject    * to_py_boost()       { boost() ; return                     this  ;                                     }
		/**/      PyObject    * to_py_boost() const { boost() ; return const_cast<Object*>(this) ;                                     }
		/**/      Object      & boost      ()       {                                               Py_INCREF(to_py()) ; return self ; }
		/**/      Object const& boost      () const {                                               Py_INCREF(to_py()) ; return self ; }
		/**/      Object      & unboost    ()       { { if (ref_cnt()==1) Gil::s_swear_locked() ; } Py_DECREF(to_py()) ; return self ; }
		/**/      Object const& unboost    () const { { if (ref_cnt()==1) Gil::s_swear_locked() ; } Py_DECREF(to_py()) ; return self ; }
		// services
		Ptr<Str> str      () const ;
		::string type_name() const { return ob_type->tp_name ; }
		ssize_t  ref_cnt  () const { return ob_refcnt        ; }
		//
		template<class T=Object> Ptr<T> get_attr( ::string const& attr                     ) const ;
		/**/                     void   set_attr( ::string const& attr , Object const& val ) ;
		/**/                     void   del_attr( ::string const& attr                     ) ;
	} ;

	template<> struct Ptr<Object> {
		// statics
		static void s_init() ;
		// static datas
		static Callable* s_dumps ;
		static Callable* s_loads ;
		// cxtors & casts
		Ptr(               ) = default ;
		Ptr(PyObject*     p) : ptr{from_py(p)            } {              } // steal ownership from argument
		Ptr(Object const* o) : ptr{const_cast<Object*>(o)} { boost()    ; }
		Ptr(Ptr    const& p) : ptr{p.ptr                 } { boost()    ; }
		Ptr(Ptr        && p) : ptr{p.ptr                 } { p.detach() ; }
		//
		~Ptr() { unboost() ; }
		//
		Ptr& operator=(Ptr const& p) { unboost() ; ptr = p.ptr ; boost()    ; return self ; }
		Ptr& operator=(Ptr     && p) { unboost() ; ptr = p.ptr ; p.detach() ; return self ; }
		//
		template<IsStream T> void serdes(T& s) ;
		// accesses
		bool operator+() const { return bool(ptr) ; }
		//
		Object      & operator* ()       { return *ptr   ; }
		Object const& operator* () const { return *ptr   ; }
		Object      * operator->()       { return &*self ; }
		Object const* operator->() const { return &*self ; }
		// services
		void       detach ()       { ptr = nullptr ;                         }
		Ptr      & boost  ()       { if (ptr) ptr->boost  () ; return self ; }
		Ptr const& boost  () const { if (ptr) ptr->boost  () ; return self ; }
		void       unboost() const { if (ptr) ptr->unboost() ;               }
		// data
	protected :
		Object* ptr = nullptr ;
	} ;

	template<class T> requires(!::is_same_v<Object,T>) struct PtrBase : Ptr<typename T::Base> {
		using TBase = typename T::Base ;
		using Base  = Ptr<TBase>       ;
		using Base::ptr ;
		// cxtors & casts
		PtrBase() = default ;
		//
		PtrBase(PyObject    * p) : Base{       p } { _chk<T>(ptr) ; }
		PtrBase(Object const* o) : Base{       o } { _chk<T>(ptr) ; }
		PtrBase(Base   const& o) : Base{       o } { _chk<T>(ptr) ; }
		PtrBase(Base       && o) : Base{::move(o)} { _chk<T>(ptr) ; }
		PtrBase(T           * o) : Base{       o } {                }
		//
		template<IsStream S> void serdes(S& s) {
			Base::serdes(s) ;
			if (IsIStream<S>) _chk<T>(ptr) ;
		}
		// accesses
		T      & operator* ()       { SWEAR(ptr) ; return *static_cast<T      *>(ptr) ; }
		T const& operator* () const { SWEAR(ptr) ; return *static_cast<T const*>(ptr) ; }
		T      * operator->()       {              return &*self                      ; }
		T const* operator->() const {              return &*self                      ; }
		//
		operator Object      *()                                            { return                           ptr  ; }
		operator Object const*() const                                      { return                           ptr  ; }
		operator TBase       *()       requires(!::is_same_v<Object,TBase>) { return static_cast<TBase      *>(ptr) ; }
		operator TBase  const*() const requires(!::is_same_v<Object,TBase>) { return static_cast<TBase const*>(ptr) ; }
		operator T           *()                                            { return static_cast<T          *>(ptr) ; }
		operator T      const*() const                                      { return static_cast<T     const*>(ptr) ; }
		// services
		Ptr<T>      & boost()       { Ptr<Object>::boost() ; return static_cast<Ptr<T>&>(self) ; }
		Ptr<T> const& boost() const { Ptr<Object>::boost() ; return static_cast<Ptr<T>&>(self) ; }
	} ;

	//
	// None
	//

	struct NoneType : Object {
		static constexpr const char* Name = "NoneType" ;
		using Base = Object ;
		bool qualify() const { return to_py()==Py_None ; }
	} ;
	static NoneType& None = *from_py<NoneType>(Py_None) ;
	template<> struct Ptr<NoneType> : PtrBase<NoneType> {
		using Base = PtrBase<NoneType> ;
		using Base::Base ;
	} ;

	//
	// Ellipsis
	//

	struct EllipsisType : Object {
		static constexpr const char* Name = "ellipsis" ;
		using Base = Object ;
		bool qualify() const { return to_py()==Py_Ellipsis ; }
	} ;
	static EllipsisType& Ellipsis = *from_py<EllipsisType>(Py_Ellipsis) ;
	template<> struct Ptr<EllipsisType> : PtrBase<EllipsisType> {
		using Base = PtrBase<EllipsisType> ;
		using Base::Base ;
	} ;

	//
	// Bool
	//

	struct Bool : Object {
		static constexpr const char* Name = "bool" ;
		using Base = Object ;
		bool qualify () const { return PyBool_Check(to_py()) ; }
		operator bool() const { return +self                 ; }
		bool     val () const ;                                  // for debug
	} ;
	template<> struct Ptr<Bool> : PtrBase<Bool> {
		using Base = PtrBase<Bool> ;
		using Base::Base ;
		Ptr(bool v) : Base{ ( Gil::s_swear_locked() , PyBool_FromLong(v) ) } {}
	} ;
	static Bool& True  = *from_py<Bool>(Py_True ) ;
	static Bool& False = *from_py<Bool>(Py_False) ;

	//
	// Int
	//

	struct Int : Object {
		static constexpr const char* Name = "int" ;
		using Base = Object ;
		bool qualify() const { return PyLong_Check(to_py()) ; }
		template<::integral T> operator T() const {
			if (::is_signed_v<T>) {
				long v = PyLong_AsLong( to_py() ) ;
				from_py() ;
				throw_unless( v>=int64_t(Min<T>) , "underflow" ) ;
				throw_unless( v<=int64_t(Max<T>) , "overflow"  ) ;
				return T(v) ;
			} else {
				ulong v = PyLong_AsUnsignedLong( to_py() ) ;
				from_py() ;
				throw_unless( v<=Max<T> , "overflow" ) ;
				return T(v) ;
			}
		}
		long  val () const ; // for debug
		ulong uval() const ; // for debug
	} ;
	template<> struct Ptr<Int> : PtrBase<Int> {
		using Base = PtrBase<Int> ;
		using Base::Base ;
		template<::integral I> Ptr(I v) : Base{ ( Gil::s_swear_locked() , ::is_signed_v<I> ? PyLong_FromLong(long(v)) : PyLong_FromUnsignedLong(ulong(v)) ) } {}
	} ;

	//
	// Float
	//

	struct Float : Object {
		static constexpr const char* Name = "float" ;
		using Base = Object ;
		bool qualify() const { return PyFloat_Check(to_py()) || PyLong_Check(to_py()) ; }
		operator double() const {
			return PyFloat_AsDouble(to_py()) ;
		}
		double val() const ; // for debug
	} ;
	template<> struct Ptr<Float> : PtrBase<Float> {
		using Base = PtrBase<Float> ;
		using Base::Base ;
		Ptr(double          v) : Base{ ( Gil::s_swear_locked() , PyFloat_FromDouble(v) ) } {}
		Ptr(Str      const& v) ;
		Ptr(::string const& v) ;
	} ;

	//
	// Str
	//

	struct Str : Object {
		static constexpr const char* Name = "str" ;
		using Base = Object ;
		bool qualify() const {
			#if PY_MAJOR_VERSION<3
				return PyString_Check (to_py()) ;
			#else
				return PyUnicode_Check(to_py()) ;
			#endif
		}
		operator ::string() const {
			Py_ssize_t sz = 0 ;
			#if PY_MAJOR_VERSION<3
				char* data = nullptr ;
				PyString_AsStringAndSize(to_py(),&data,&sz) ;
			#else
				const char* data = PyUnicode_AsUTF8AndSize(to_py(),&sz) ;
			#endif
			return { from_py(data) , size_t(sz) } ;
		}
		::string val() const ; // for debug
	} ;
	template<> struct Ptr<Str> : PtrBase<Str> {
		using Base = PtrBase<Str> ;
		using Base::Base ;
		Ptr(::string const& v) :
			#if PY_MAJOR_VERSION<3
				Base{ ( Gil::s_swear_locked() , PyString_FromStringAndSize (v.c_str(),v.size()) ) }
			#else
				Base{ ( Gil::s_swear_locked() , PyUnicode_FromStringAndSize(v.c_str(),v.size()) ) }
			#endif
		{}
	} ;

	//
	// Bytes
	//

	#if PY_MAJOR_VERSION>=3        // else Bytes is Str
		struct Bytes : Object {
			static constexpr const char* Name = "bytes" ;
			using Base = Object ;
			bool qualify() const {
				return PyBytes_Check(to_py()) ;
			}
			operator ::string() const {
				Py_ssize_t sz = 0 ;
				char* data = nullptr ;
				PyBytes_AsStringAndSize(to_py(),&data,&sz) ;
				return { from_py(data) , size_t(sz) } ;
			}
			::string val() const ; // for debug
		} ;
		template<> struct Ptr<Bytes> : PtrBase<Bytes> {
			using Base = PtrBase<Bytes> ;
			using Base::Base ;
			Ptr(::string const& v) : Base{ ( Gil::s_swear_locked() , PyBytes_FromStringAndSize(v.c_str(),v.size()) ) } {}
		} ;
	#endif

	//
	// Sequence
	//

	struct Sequence ;

	template<bool C> struct SequenceIter {
		using Iterable = ::conditional_t< C , Sequence const , Sequence > ;
		using PtrItem  = ::conditional_t< C , Ptr<>    const , Ptr<>    > ;
		using Item     = ::conditional_t< C , Object   const , Object   > ;
		// cxtors & casts
		SequenceIter(                       ) = default ;
		SequenceIter(Iterable& i,bool at_end) : _item {::launder(reinterpret_cast<PtrItem*>(PySequence_Fast_ITEMS(i.to_py())))} { if (at_end) _item += i.size() ; }
		// accesses
		bool operator==(SequenceIter const&) const = default ;
		// services
		Item        & operator* (   ) const {                                 return **_item ; }
		SequenceIter& operator++(   )       { _item++ ;                       return self    ; }
		SequenceIter  operator++(int)       { SequenceIter it = self ; ++it ; return it      ; }
		// data
	private :
		PtrItem* _item = nullptr ;
	} ;

	struct Sequence : Object {
		static constexpr const char* Name = "list/tuple" ;
		using Base = Object ;
		template<bool C> friend struct SequenceIter ;
		// accesses
		bool   qualify() const { return PyTuple_Check(to_py()) || PyList_Check(to_py()) ; }
		size_t size   () const { return PySequence_Fast_GET_SIZE(to_py())               ; }
		//
		Object& operator[](size_t idx) const {
			return *from_py(PySequence_Fast_GET_ITEM(to_py(),ssize_t(idx))) ;
		}
		// services
	public :
		SequenceIter<false> begin ()       { return {self,false} ; }
		SequenceIter<false> end   ()       { return {self,true } ; }
		SequenceIter<true > begin () const { return {self,false} ; }
		SequenceIter<true > end   () const { return {self,true } ; }
		SequenceIter<true > cbegin() const { return {self,false} ; }
		SequenceIter<true > cend  () const { return {self,true } ; }
	} ;
	template<> struct Ptr<Sequence> : PtrBase<Sequence> {
		using Base = PtrBase<Sequence> ;
		using Base::Base ;
	} ;

	//
	// Tuple
	//

	struct Tuple : Sequence {
		static constexpr const char* Name = "tuple" ;
		using Base = Sequence ;
		bool   qualify() const { return PyTuple_Check   (to_py()) ; }
		size_t size   () const { return PyTuple_GET_SIZE(to_py()) ; }
		void set_item( ssize_t idx , Object const& val ) {
			PyTuple_SET_ITEM( to_py() , idx , val.to_py_boost() ) ;
		}
		Object& operator[](size_t idx) const {
			return *from_py(PyTuple_GET_ITEM(to_py(),ssize_t(idx))) ;
		}
	} ;
	template<> struct Ptr<Tuple> : PtrBase<Tuple> {
		using Base = PtrBase<Tuple> ;
		using Base::Base ;
		Ptr( NewType                             ) : Base{ ( Gil::s_swear_locked() , PyTuple_New(0 ) ) } { SWEAR(ptr) ;                                               }
		Ptr( size_t sz                           ) : Base{ ( Gil::s_swear_locked() , PyTuple_New(sz) ) } { SWEAR(ptr) ;                                               }
		Ptr( Object const& i0                    ) : Base{ ( Gil::s_swear_locked() , PyTuple_New(1 ) ) } { SWEAR(ptr) ; self->set_item(0,i0) ;                        }
		Ptr( Object const& i0 , Object const& i1 ) : Base{ ( Gil::s_swear_locked() , PyTuple_New(2 ) ) } { SWEAR(ptr) ; self->set_item(0,i0) ; self->set_item(1,i1) ; }
	} ;

	//
	// List
	//

	struct List : Sequence {
		static constexpr const char* Name = "list" ;
		// accesses
		bool   qualify() const { return PyList_Check   (to_py()) ; }
		size_t size   () const { return PyList_GET_SIZE(to_py()) ; }
		//
		void set_item( ssize_t idx , Object const& val ) {
			int rc = PyList_SetItem( to_py() , idx , val.to_py_boost() ) ; from_py(rc) ;
		}
		Object& operator[](size_t idx) const {
			return *from_py(PyList_GET_ITEM(to_py(),ssize_t(idx))) ;
		}
		// services
		void append(Object& v) { Gil::s_swear_locked() ; int rc = PyList_Append  ( to_py() , v.to_py()            ) ; from_py(rc) ; }
		void clear (         ) { Gil::s_swear_locked() ; int rc = PyList_SetSlice( to_py() , 0 , size() , nullptr ) ; from_py(rc) ; }
	} ;
	template<> struct Ptr<List> : PtrBase<List> {
		using Base = PtrBase<List> ;
		using Base::Base ;
	} ;

	//
	// Dict
	//

	struct Dict ;

	template<bool C> struct DictIter {
		using Iterable = ::conditional_t< C , Dict   const , Dict   > ;
		using Item     = ::conditional_t< C , Object const , Object > ;
		// cxtors & casts
		DictIter(           ) = default ;
		DictIter(Iterable& i) : _iterable{&i},_pos{0} { _legalize() ; }
		bool operator==(DictIter const&) const = default ;
		// services
		::pair<Object const&,Item&> operator*() const {
			Py_ssize_t p   = _pos    ;
			PyObject*  key = nullptr ;
			PyObject*  val = nullptr ;
			SWEAR(_iterable) ;
			PyDict_Next( _iterable->to_py() , &p , &key , &val ) ;
			return { *from_py(key) , *from_py(val) } ;
		}
		DictIter& operator++() {
			PyDict_Next( _iterable->to_py() , &_pos , nullptr , nullptr ) ;
			_legalize() ;
			return self ;
		}
		DictIter operator++(int) { DictIter it = self ; ++it ; return it ; }
	private :
		void _legalize() {
			Py_ssize_t p = _pos ;
			if (!PyDict_Next( _iterable->to_py() , &p , nullptr , nullptr )) self = {} ;
		}
		// data
		Iterable*  _iterable = nullptr ; // default value represents ended iterators
		Py_ssize_t _pos      = 0       ; // .
	} ;

	struct Dict : Object {
		static constexpr const char* Name = "dict" ;
		using Base = Object ;
		template<bool C> friend struct DictIter ;
		using value_type = ::pair<Object const&,Object&> ;
		// statics
		static Dict* s_builtins ;
		// services
		bool   qualify() const { return PyDict_Check(to_py()) ; }
		size_t size   () const { return PyDict_Size (to_py()) ; }
		//
		bool          contains(::string const& k) const { Gil::s_swear_locked() ; return bool    (PyDict_GetItemString(to_py(),k.c_str())) ; }
		Object      & get_item(::string const& k)       { Gil::s_swear_locked() ; return *from_py(PyDict_GetItemString(to_py(),k.c_str())) ; }
		Object const& get_item(::string const& k) const { Gil::s_swear_locked() ; return *from_py(PyDict_GetItemString(to_py(),k.c_str())) ; }
		//
		void set_item  ( ::string const& k , Object& v ) { Gil::s_swear_locked() ; int rc = PyDict_SetItemString( to_py() , k.c_str() , v.to_py() ) ; throw_if( rc , "cannot set ",k       ) ; }
		void del_item  ( ::string const& k             ) { Gil::s_swear_locked() ; int rc = PyDict_DelItemString( to_py() , k.c_str()             ) ; throw_if( rc , "key ",k," not found" ) ; }
		void erase_item( ::string const& k             ) { Gil::s_swear_locked() ;          PyDict_DelItemString( to_py() , k.c_str()             ) ;                                          }
		//
		Object      & operator[](::string const& key)       { return get_item(key) ; }
		Object const& operator[](::string const& key) const { return get_item(key) ; }
		//
	public :
		DictIter<false> begin ()       { return self ; }
		DictIter<false> end   ()       { return {}   ; }
		DictIter<true > begin () const { return self ; }
		DictIter<true > end   () const { return {}   ; }
		DictIter<true > cbegin() const { return self ; }
		DictIter<true > cend  () const { return {}   ; }
	} ;
	template<> struct Ptr<Dict> : PtrBase<Dict> {
		using Base = PtrBase<Dict> ;
		using Base::Base ;
		Ptr(NewType) : Base{ ( Gil::s_swear_locked() , PyDict_New() ) } {}
		//
		template<IsStream T> void serdes(T& s) {
			// marshal seems very slow to fail, so avoid calling it on most common cases known to fail
			if (!IsIStream<T>)
				for( auto const& [py_k,py_v] : *self ) throw_if( py_v.template is_a<Callable>() , "cannot serialize callables" ) ; // XXX? : why is template necessary with gcc11 ?
			Base::serdes(s) ;
		}
	} ;

	//
	// Code
	//

	struct Code : Object {
		static constexpr const char* Name = "code" ;
		using Base = Object ;
		// services
		bool      qualify(                                                       ) const { return PyCode_Check(to_py()) ; }
		Ptr<    > eval   ( Dict* glbs=nullptr , Sequence const* sys_path=nullptr ) const ;
		Ptr<Dict> run    ( Dict* glbs=nullptr , Sequence const* sys_path=nullptr ) const ; // update glbs if provided (else use clean dict), return globals after execution
	} ;
	template<> struct Ptr<Code> : PtrBase<Code> {
		using Base = PtrBase<Code> ;
		using Base::Base ;
		Ptr( ::string const& v , bool for_eval ) :
			Base{(
				Gil::s_swear_locked()
			,	Py_CompileString( v.c_str() , "<code>" , for_eval?Py_eval_input:Py_file_input )
			)}
		{}
	} ;

	//
	// Module
	//

	struct Module : Object {
		static constexpr const char* Name = "module" ;
		using Base = Object ;
		// services
		bool qualify() const { return PyModule_Check(to_py()) ; }
	} ;
	template<> struct Ptr<Module> : PtrBase<Module> {
		using Base = PtrBase<Module> ;
		using Base::Base ;
		Ptr( NewType , ::string const& name , PyMethodDef* funcs=nullptr ) ; // new module
		Ptr(           ::string const& name                              ) ; // import
	} ;

	//
	// Callable
	//

	struct Callable : Object {
		static constexpr const char* Name = "callable" ;
		using Base = Object ;
		// services
		bool qualify() const { return PyCallable_Check(to_py()) ; }
		//
		Ptr<> operator()() const {  // fast path : no empty tuple
			Gil::s_swear_locked() ;
			return PyObject_CallObject( to_py() , nullptr ) ;
		}
		template<class... A> Ptr<> operator()(A&&... args) const {
			Gil::s_swear_locked() ;
			Ptr<Tuple> t { sizeof...(A) } ;
			size_t     i = 0              ;
			((t->set_item(i++,args)),...) ;
			return PyObject_CallObject( to_py() , t->to_py() ) ;
		}
		template<class R=Object,class... A> Ptr<R> call(A&&... args) const { return self(args...) ; }
	} ;
	template<> struct Ptr<Callable> : PtrBase<Callable> {
		using Base = PtrBase<Callable> ;
		using Base::Base ;
	} ;

	//
	// functions
	//

	void init(::string const& lmake_root_s) ;

	template<class T=Object> T& py_get_sys(::string const& name) {
		Gil::s_swear_locked() ;
		PyObject* v = PySys_GetObject(const_cast<char*>(name.c_str())) ;
		throw_unless( v , "cannot find sys.",name ) ;
		return *from_py<T>(v) ;
	}

	inline void py_set_sys( ::string const& name , Object const& val ) {
		Gil::s_swear_locked() ;
		int rc = PySys_SetObject( const_cast<char*>(name.c_str()) , val.to_py() ) ;
		throw_unless( rc==0 , "cannot set sys.",name ) ;
	}

	void py_reset_sys_path() ;

	Ptr<    > py_eval( ::string const& expr , Dict* glbs=nullptr , Sequence const* sys_path=nullptr ) ;
	Ptr<Dict> py_run ( ::string const& text , Dict* glbs=nullptr , Sequence const* sys_path=nullptr ) ; // update glbs if provided (else use clean dict), return globals after execution

	::string py_fstr_escape(::string const& s) ;

	//
	// RAII
	//

	template<class T> struct WithGil : T {
		using T::operator+ ;
		// cxtors & casts
		using T::T ;
		WithGil(T const& t) : T{       t } {}
		WithGil(T     && t) : T{::move(t)} {}
		~WithGil() {
			if (!self) return ; // fast path : dont pay init of all fields if not necessary
			self = T() ;
		}
		WithGil& operator=(WithGil const& p) { if (+self) { Gil gil ; T::operator=(       p ) ; } else { T::operator=(       p ) ; } return self ; }
		WithGil& operator=(WithGil     && p) { if (+self) { Gil gil ; T::operator=(::move(p)) ; } else { T::operator=(::move(p)) ; } return self ; }
		WithGil& operator=(T       const& p) { if (+self) { Gil gil ; T::operator=(       p ) ; } else { T::operator=(       p ) ; } return self ; }
		WithGil& operator=(T           && p) { if (+self) { Gil gil ; T::operator=(::move(p)) ; } else { T::operator=(::move(p)) ; } return self ; }
	} ;

	struct WithBuiltins {                              // set __builtins__ in dict and remove it at the end
		// cxtors & casts
		WithBuiltins (Dict& dct_) : dct{dct_} {
			#ifndef NDEBUG                             // avoid executing glb.contains if not debugging
				SWEAR(!dct.contains("__builtins__")) ;
			#endif
			dct.set_item("__builtins__",*Dict::s_builtins) ;
		}
		~WithBuiltins() {
			dct.del_item("__builtins__") ;
		}
		// data
		Dict& dct ;
	} ;

	struct WithSysPath { // set __builtins__ in dict and remove it at the end
		// cxtors & casts
		WithSysPath (Sequence const* sys_path) {
			if (!sys_path) return ;
			sav_sys_path = &py_get_sys<Sequence>("path") ;
			py_set_sys( "path" , *sys_path ) ;
		}
		~WithSysPath() {
			if (!sav_sys_path) return ;
			py_set_sys( "path" , *sav_sys_path ) ;
		}
		// data
		Ptr<Sequence> sav_sys_path ;
	} ;

	//
	// implementation
	//

	template<class T> T const* _chk(Object const* o) { SWEAR(o) ; throw_unless(o->is_a<T>(),"type error : ",o->type_name()," is not a ",T::Name) ; return static_cast<T const*>(o) ; }
	template<class T> T      * _chk(Object      * o) { SWEAR(o) ; throw_unless(o->is_a<T>(),"type error : ",o->type_name()," is not a ",T::Name) ; return static_cast<T      *>(o) ; }

	//
	// Object
	//

	template<class T> Ptr<T> Object::get_attr( ::string const& attr                     ) const { return                          PyObject_GetAttrString( to_py() , attr.c_str()               )  ; }
	inline void              Object::set_attr( ::string const& attr , Object const& val )       { Gil::s_swear_locked() ; from_py(PyObject_SetAttrString( to_py() , attr.c_str() , val.to_py() )) ; }
	inline void              Object::del_attr( ::string const& attr                     )       { Gil::s_swear_locked() ; from_py(PyObject_DelAttrString( to_py() , attr.c_str()               )) ; }

	inline void Ptr<Object>::s_init() {
		SWEAR(bool(s_loads)==bool(s_dumps)) ;
		if (s_loads) return ;
		Ptr<Module> marshal { "marshal" } ;
		s_dumps = marshal->get_attr<Callable>("dumps").boost() ; // ensure no destruction at finalization
		s_loads = marshal->get_attr<Callable>("loads").boost() ; // .
	}

	template<IsStream T> void Ptr<Object>::serdes(T& s) {
		s_init() ;
		::string buf ;
		if (!IsIStream<T>) buf  = *s_dumps->call<Bytes>(*self)    ;
		::serdes(s,buf) ;
		if ( IsIStream<T>) self = s_loads->call(*Ptr<Bytes>(buf)) ;
	}

	//
	// Float
	//

	inline Ptr<Float>::Ptr(Str const& v) :
		#if PY_MAJOR_VERSION<3
			Base{ ( Gil::s_swear_locked() , PyFloat_FromString(v.to_py(),nullptr/*unused*/) ) }
		#else
			Base{ ( Gil::s_swear_locked() , PyFloat_FromString(v.to_py()                  ) ) }
		#endif
	{ throw_unless( +self , "cannot convert to float" ) ; }
	inline Ptr<Float>::Ptr(::string const& v) : Ptr{*Ptr<Str>(v)} {}

}
