// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <Python.h> // /!\ must be included first because doc says so (else it does not work)

#include "utils.hh"
#include "trace.hh"

ENUM( Exception
,	RuntimeErr
,	TypeErr
,	ValueErr
)

namespace Py {
	struct Gil :     Lock<Mutex<MutexLvl::Gil>> {
		using Base = Lock<Mutex<MutexLvl::Gil>> ;
		// statics
		static void s_swear_locked() { _s_mutex.swear_locked() ; }
		// static data
	private :
		static Mutex<MutexLvl::Gil> _s_mutex ;
		// cxtors & casts
	public :
		Gil () : Base{_s_mutex} { trace("acquired") ; }
		~Gil()                  { trace("released") ; }
		// data
		Trace trace { "Gil" } ;
	} ;

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
	//
	template<class T=Object> struct Ptr ;

	//
	// functions
	//

	template<class T       > T const* _chk   (T const * o) { if ( o && !o->qualify() ) throw to_string("not a ",o->type_name()) ; return o   ; }
	template<class T       > T      * _chk   (T       * o) { if ( o && !o->qualify() ) throw to_string("not a ",o->type_name()) ; return o   ; }
	template<class T=Object> T      * from_py(PyObject* o) { T* res = static_cast<T*>(o) ; _chk(res) ;                            return res ; }

	inline void py_err_clear   () {        PyErr_Clear   () ; }
	inline bool py_err_occurred() { return PyErr_Occurred() ; }
	//
	template<class T=Object> T& py_get_sys(::string const& name) {
		PyObject* v = PySys_GetObject(const_cast<char*>(name.c_str())) ;
		if (!v) throw to_string("cannot find sys.",name) ;
		return *from_py<T>(v) ;
	}

	void init( ::string const& lmake_dir_s , bool multi_thread=false ) ; // if multi_thread, GIL must be acquired before each any call to Python API

	::string py_err_str_clear() ;        // like PyErr_Print, but return text instead of printing it (Python API provides no means to do this !)
	//
	inline nullptr_t py_err_set( Exception e , ::string const& txt ) {
		static PyObject* s_exc_tab[] = {
			PyExc_RuntimeError           // RuntimeErr
		,	PyExc_TypeError              // TypeErr
		,	PyExc_ValueError             // ValueErr
		} ;
		static_assert(sizeof(s_exc_tab)/sizeof(PyObject*)==N<Exception>) ;
		PyErr_SetString(s_exc_tab[+e],txt.c_str()) ;
		return nullptr ;
	}

	Ptr<Object> py_eval(::string const&) ;
	Ptr<Dict  > py_run (::string const&) ;

	//
	// Object
	//

	struct Object : PyObject {
		template<class T> friend struct Ptr ;
		friend Code ;
		friend Ptr<Object> py_eval(::string const&) ;
		friend Ptr<Dict  > py_run (::string const&) ;
		// cxtors & casts
		template<class... As> Object(As&&...) = delete ;
		// accesses
		bool operator==(Object const& other) const { return this==&other ; }
		bool qualify() const { return true ; }                               // everything qualifies as object
		//
		template<class T> T      & as_a()       { return *_chk(static_cast<T      *>(this))           ; }
		template<class T> T const& as_a() const { return *_chk(static_cast<T const*>(this))           ; }
		template<class T> bool     is_a() const { return       static_cast<T const*>(this)->qualify() ; }
		//
		bool operator+() const {
			int rc = PyObject_IsTrue(to_py()) ;
			if (rc<0) throw py_err_str_clear() ;
			return bool(rc) ;
		}
		bool operator!() const { return !+*this ; }
		constexpr PyObject* to_py      ()       {           return                     this  ; }
		constexpr PyObject* to_py      () const {           return const_cast<Object*>(this) ; }
		PyObject*           to_py_boost()       { boost() ; return                     this  ; }
		PyObject*           to_py_boost() const { boost() ; return const_cast<Object*>(this) ; }
		void                boost      () const { Py_INCREF(this) ;                            }
		void                unboost    () const { Py_DECREF(this) ;                            }
		// services
		Ptr<Str> str() const ;
		::string type_name() const { return ob_type->tp_name ; }
		//
		template<class T=Object> Ptr<T> get_attr( ::string const& attr                     ) const ;
		/**/                     void   set_attr( ::string const& attr , Object const& val ) ;
		/**/                     void   del_attr( ::string const& attr                     ) ;
	} ;

	template<> struct Ptr<Object> {
		// cxtors & casts
		Ptr(            ) = default ;
		Ptr(PyObject*  p) : ptr{from_py(p)} {              } // steal ownership from argument
		Ptr(Object*    o) : ptr{o         } { boost()    ; }
		Ptr(Ptr const& p) : ptr{p.ptr     } { boost()    ; }
		Ptr(Ptr     && p) : ptr{p.ptr     } { p.detach() ; }
		//
		~Ptr() { unboost() ; }
		//
		Ptr& operator=(Ptr const& p) { unboost() ; ptr = p.ptr ; boost()    ; return *this ; }
		Ptr& operator=(Ptr     && p) { unboost() ; ptr = p.ptr ; p.detach() ; return *this ; }
		// accesses
		bool operator+() const { return bool(ptr) ; }
		bool operator!() const { return !+*this ;   }
		template<class T> operator T      *()       { return _chk(static_cast<T      *>(ptr)) ; }
		template<class T> operator T const*() const { return _chk(static_cast<T const*>(ptr)) ; }
		//
		Object      & operator* ()       { return *ptr    ; }
		Object const& operator* () const { return *ptr    ; }
		Object      * operator->()       { return &**this ; }
		Object const* operator->() const { return &**this ; }
		// services
		void detach ()       { ptr = nullptr ; }
		void boost  () const { if (ptr) ptr->boost  () ; }
		void unboost() const { if (ptr) ptr->unboost() ; }
		// data
	protected :
		Object* ptr = nullptr ;
	} ;

	template<class T> requires(!::is_same_v<Object,T>) struct PtrBase : Ptr<typename T::Base> {
		using TBase = typename T::Base ;
		using Base = Ptr<TBase> ;
		using Base::ptr ;
		// cxtors & casts
		PtrBase() = default ;
		//
		PtrBase(PyObject*   p) : Base{       p } { _chk(ptr) ; }
		PtrBase(Object*     o) : Base{       o } { _chk(ptr) ; }
		PtrBase(Base const& o) : Base{       o } { _chk(ptr) ; }
		PtrBase(Base     && o) : Base{::move(o)} { _chk(ptr) ; }
		PtrBase(T*          o) : Base{       o } {             }
		//
		// accesses
		T      & operator* ()       { SWEAR(ptr) ; return *static_cast<T      *>(ptr) ; }
		T const& operator* () const { SWEAR(ptr) ; return *static_cast<T const*>(ptr) ; }
		T      * operator->()       {              return &**this                     ; }
		T const* operator->() const {              return &**this                     ; }
		//
		operator Object      *()                                            { return &**this ; }
		operator Object const*() const                                      { return &**this ; }
		operator TBase       *()       requires(!::is_same_v<Object,TBase>) { return &**this ; }
		operator TBase  const*() const requires(!::is_same_v<Object,TBase>) { return &**this ; }
		operator T           *()                                            { return &**this ; }
		operator T      const*() const                                      { return &**this ; }
	} ;

	//
	// None
	//

	struct NoneType : Object {
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
		using Base = Object ;
		bool qualify () const { return PyBool_Check(to_py()) ; }
		operator bool() const { return +*this                ; }
		bool     val () const ;                                  // for debug
	} ;
	template<> struct Ptr<Bool> : PtrBase<Bool> {
		using Base = PtrBase<Bool> ;
		using Base::Base ;
		Ptr(bool v) : Base{PyBool_FromLong(v)} {}
	} ;
	static Bool& True  = *from_py<Bool>(Py_True ) ;
	static Bool& False = *from_py<Bool>(Py_False) ;

	//
	// Int
	//

	struct Int : Object {
		using Base = Object ;
		bool qualify() const { return PyLong_Check(to_py()) ; }
		template<::integral T> operator T() const {
			if (::is_signed_v<T>) {
				long v = PyLong_AsLong( to_py() ) ;
				if (py_err_occurred()           ) throw py_err_str_clear() ;
				if (v<::numeric_limits<T>::min()) throw "underflow"s       ;
				if (v>::numeric_limits<T>::max()) throw "overflow"s        ;
				return T(v) ;
			} else {
				unsigned long v = PyLong_AsUnsignedLong( to_py() ) ;
				if (py_err_occurred()           ) throw py_err_str_clear() ;
				if (v>::numeric_limits<T>::max()) throw "overflow"s        ;
				return T(v) ;
			}
		}
		long  val () const ; // for debug
		ulong uval() const ; // for debug
	} ;
	template<> struct Ptr<Int> : PtrBase<Int> {
		using Base = PtrBase<Int> ;
		using Base::Base ;
		using ulong = unsigned long ;
		template<::integral I> Ptr(I v) : Base{ ::is_signed_v<I> ? PyLong_FromLong(long(v)) : PyLong_FromUnsignedLong(ulong(v)) } {}
	} ;

	//
	// Float
	//

	struct Float : Object {
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
		Ptr(double          v) : Base{PyFloat_FromDouble(v)} {}
		Ptr(Str      const& v) ;
		Ptr(::string const& v) ;
	} ;

	//
	// Str
	//

	struct Str : Object {
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
			if (!data) throw py_err_str_clear() ;
			return {data,size_t(sz)} ;
		}
		::string val() const ; // for debug
	} ;
	template<> struct Ptr<Str> : PtrBase<Str> {
		using Base = PtrBase<Str> ;
		using Base::Base ;
		Ptr(::string const& v) :
			#if PY_MAJOR_VERSION<3
				Base{PyString_FromStringAndSize (v.c_str(),v.size())}
			#else
				Base{PyUnicode_FromStringAndSize(v.c_str(),v.size())}
			#endif
		{}
	} ;

	//
	// Sequence
	//

	struct Sequence ;

	template<bool C> struct SequenceIter {
		using Iterable = ::conditional_t< C , Sequence    const , Sequence    > ;
		using PtrItem  = ::conditional_t< C , Ptr<Object> const , Ptr<Object> > ;
		using Item     = ::conditional_t< C ,     Object  const ,     Object  > ;
		// cxtors & casts
		SequenceIter(                       ) = default ;
		SequenceIter(Iterable& i,bool at_end) : _item {reinterpret_cast<Ptr<Object>*>(PySequence_Fast_ITEMS(i.to_py()))} { if (at_end) _item += i.size() ; }
		// accesses
		bool operator==(SequenceIter const&) const = default ;
		// services
		Item        & operator* (   ) const {                                  return **_item ; }
		SequenceIter& operator++(   )       { _item++ ;                        return *this   ; }
		SequenceIter  operator++(int)       { SequenceIter it = *this ; ++it ; return it      ; }
		// data
	private :
		PtrItem* _item = nullptr ;
	} ;

	struct Sequence : Object {
		using Base = Object ;
		template<bool C> friend struct SequenceIter ;
		// accesses
		bool   qualify() const { return PyTuple_Check(to_py()) || PyList_Check(to_py()) ; }
		size_t size   () const { return PySequence_Fast_GET_SIZE(to_py())               ; }
		//
		Object      & operator[](size_t idx)       { return _get_item(idx) ; }
		Object const& operator[](size_t idx) const { return _get_item(idx) ; }
	private :
		Object& _get_item(size_t idx) const {
			Object* res = from_py(PySequence_Fast_GET_ITEM(to_py(),ssize_t(idx))) ;
			if (!res) throw py_err_str_clear() ;
			return *res ;
		}
		// services
	public :
		SequenceIter<false> begin ()       { return {*this,false} ; }
		SequenceIter<false> end   ()       { return {*this,true } ; }
		SequenceIter<true > begin () const { return {*this,false} ; }
		SequenceIter<true > end   () const { return {*this,true } ; }
		SequenceIter<true > cbegin() const { return {*this,false} ; }
		SequenceIter<true > cend  () const { return {*this,true } ; }
	} ;
	template<> struct Ptr<Sequence> : PtrBase<Sequence> {
		using Base = PtrBase<Sequence> ;
		using Base::Base ;
	} ;

	//
	// Tuple
	//

	struct Tuple : Sequence {
		using Base = Sequence ;
		bool   qualify() const { return PyTuple_Check   (to_py()) ; }
		size_t size   () const { return PyTuple_GET_SIZE(to_py()) ; }
		void set_item( ssize_t idx , Object& val ) {
			PyTuple_SET_ITEM( to_py() , idx , val.to_py_boost() ) ;
		}
		Object      & operator[](size_t idx)       { return *from_py(PyTuple_GET_ITEM(to_py(),ssize_t(idx))) ; }
		Object const& operator[](size_t idx) const { return *from_py(PyTuple_GET_ITEM(to_py(),ssize_t(idx))) ; }
	} ;
	template<> struct Ptr<Tuple> : PtrBase<Tuple> {
		using Base = PtrBase<Tuple> ;
		using Base::Base ;
		Ptr( NewType                 ) : Base{PyTuple_New(0 )} { SWEAR(ptr) ;                                                                                       }
		Ptr( size_t sz               ) : Base{PyTuple_New(sz)} { SWEAR(ptr) ;                                                                                       }
		Ptr( Object& i0              ) : Base{PyTuple_New(1 )} { SWEAR(ptr) ; PyTuple_SET_ITEM(ptr,0,i0.to_py_boost()) ;                                            }
		Ptr( Object& i0 , Object& i1 ) : Base{PyTuple_New(2 )} { SWEAR(ptr) ; PyTuple_SET_ITEM(ptr,0,i0.to_py_boost()) ; PyTuple_SET_ITEM(ptr,1,i1.to_py_boost()) ; }
	} ;

	//
	// List
	//

	struct List : Object {
		// cxtors & casts
		using Base = Sequence ;
		// accesses
		bool   qualify() const { return PyList_Check   (to_py()) ; }
		size_t size   () const { return PyList_GET_SIZE(to_py()) ; }
		//
		void set_item( ssize_t idx , Object& val ) {
			int rc = PyList_SetItem( to_py() , idx , val.to_py_boost() ) ;
			if (rc!=0) throw py_err_str_clear() ;
		}
		Object      & operator[](size_t idx)       { return *from_py(PyList_GET_ITEM(to_py(),ssize_t(idx))) ; }
		Object const& operator[](size_t idx) const { return *from_py(PyList_GET_ITEM(to_py(),ssize_t(idx))) ; }
		// services
		void insert( size_t i , Object& v ) {
			int rc = PyList_Insert(to_py(),ssize_t(i),v.to_py()) ;
			if (rc!=0) throw py_err_str_clear() ;
		}
		void prepend(Object& v) { insert(0,v) ; }
		void append (Object& v) {
			int rc = PyList_Append(to_py(),v.to_py()) ;
			if (rc!=0) throw py_err_str_clear() ;
		}
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
			PyDict_Next( _iterable->to_py() , &p , &key , &val ) ;
			return { *from_py(key) , *from_py(val) } ;
		}
		DictIter& operator++() {
			PyDict_Next( _iterable->to_py() , &_pos , nullptr , nullptr ) ;
			_legalize() ;
			return *this ;
		}
		DictIter operator++(int) { DictIter it = *this ; ++it ; return it ; }
	private :
		void _legalize() {
			Py_ssize_t p = _pos ;
			if (!PyDict_Next( _iterable->to_py() , &p , nullptr , nullptr )) *this = {} ;
		}
		// data
		Iterable*  _iterable = nullptr ; // default value represents ended iterators
		Py_ssize_t _pos      = 0       ; // .
	} ;

	struct Dict : Object {
		using Base = Object ;
		template<bool C> friend struct DictIter ;
		// statics
		static Dict* s_builtins() { return from_py<Dict>(PyEval_GetBuiltins()) ; }
		// services
		bool   qualify() const { return PyDict_Check(to_py()) ; }
		size_t size   () const { return PyDict_Size (to_py()) ; }
		//
		bool contains(::string const& key) const {
			PyObject* rc = PyDict_GetItemString( to_py() , key.c_str() ) ;
			return bool(rc) ;
		}
		void set_item( ::string const& key , Object& val ) {
			int rc = PyDict_SetItemString( to_py() , key.c_str() , val.to_py() ) ;
			if (rc!=0) throw to_string("cannot set ",key) ;
		}
		void del_item(::string const& key) {
			int rc = PyDict_DelItemString( to_py() , key.c_str() ) ;
			if (rc!=0) throw to_string("key ",key," not found") ;
		}
		void erase_item(::string const& key) {
			PyDict_DelItemString( to_py() , key.c_str() ) ;
		}
		Object      & operator[](::string const& key)       { return _get_item(key) ; }
		Object const& operator[](::string const& key) const { return _get_item(key) ; }
	private :
		Object& _get_item(::string const& key) const {
			Object* res = from_py(PyDict_GetItemString( to_py() , key.c_str() )) ;
			if (!res) throw to_string("key ",key," not found") ;
			return *res ;
		}
		//
	public :
		DictIter<false> begin ()       { return *this ; }
		DictIter<false> end   ()       { return {}    ; }
		DictIter<true > begin () const { return *this ; }
		DictIter<true > end   () const { return {}    ; }
		DictIter<true > cbegin() const { return *this ; }
		DictIter<true > cend  () const { return {}    ; }
	} ;
	template<> struct Ptr<Dict> : PtrBase<Dict> {
		using Base = PtrBase<Dict> ;
		using Base::Base ;
		Ptr(NewType) : Base{PyDict_New()} {}
	} ;

	//
	// Code
	//

	struct Code : Object {
		using Base = Object ;
		// services
		bool        qualify(          ) const { return PyCode_Check(to_py()) ; }
		Ptr<Object> eval   (Dict& glbs) const ;
	} ;
	template<> struct Ptr<Code> : PtrBase<Code> {
		using Base = PtrBase<Code> ;
		using Base::Base ;
		Ptr(::string const& v) : Base{Py_CompileString(v.c_str(),"<code>",Py_eval_input)} {
			if (!*this) throw py_err_str_clear() ;
		}
	} ;

	//
	// Module
	//

	struct Module : Object {
		using Base = Object ;
		// services
		bool qualify() const { return PyModule_Check(to_py()) ; }
	} ;
	template<> struct Ptr<Module> : PtrBase<Module> {
		using Base = PtrBase<Module> ;
		using Base::Base ;
	private :
		static PyObject* _s_mk_mod( ::string const& name , PyMethodDef* funcs ) ;
	public :
		Ptr( ::string const& name , PyMethodDef* funcs ) : Base{_s_mk_mod(name,funcs)} { if (!*this) throw py_err_str_clear() ; }
	} ;

	//
	// Callable
	//

	struct Callable : Object {
		using Base = Object ;
		// services
		bool        qualify() const { return PyCallable_Check(to_py()) ; }
		Ptr<Object> call   () const {
			Ptr<Object> res = {PyObject_CallFunction(to_py(),nullptr)} ;
			if (!res) throw py_err_str_clear() ;
			return res ;
		}
	} ;
	template<> struct Ptr<Callable> : PtrBase<Callable> {
		using Base = PtrBase<Callable> ;
		using Base::Base ;
	} ;

	//
	// implementation
	//

	//
	// Object
	//

	template<class T> Ptr<T> Object::get_attr(::string const& attr) const {
		PyObject* val = PyObject_GetAttrString(to_py(),attr.c_str()) ;
		if (!val) throw py_err_str_clear() ;
		return val ;
	}
	inline void Object::set_attr( ::string const& attr , Object const& val ) {
		int rc = PyObject_SetAttrString( to_py() , attr.c_str() , val.to_py() ) ;
		if (rc!=0) throw py_err_str_clear() ;
	}
	inline void Object::del_attr(::string const& attr) {
		int rc = PyObject_DelAttrString( to_py() , attr.c_str() ) ;
		if (rc!=0) throw py_err_str_clear() ;
		}

	//
	// Float
	//

	inline Ptr<Float>::Ptr(Str const& v) :
		#if PY_MAJOR_VERSION<3
			Base{PyFloat_FromString(v.to_py(),nullptr/*unused*/)}
		#else
			Base{PyFloat_FromString(v.to_py()                  )}
		#endif
	{ if (!*this) throw "cannot convert to float"s ; }
	inline Ptr<Float>::Ptr(::string const& v) : Ptr{*Ptr<Str>(v)} {}

	//
	// Code
	//

	inline Ptr<Object> Code::eval(Dict& glbs) const {
		#if PY_MAJOR_VERSION<3
			PyCodeObject* c = (PyCodeObject*)(to_py()) ;
		#else
			PyObject    * c =                 to_py()  ;
		#endif
		Ptr<Object> res { PyEval_EvalCode( c , glbs.to_py() , nullptr ) } ;
		if (!res) throw py_err_str_clear() ;
		return res ;
	}

}
