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

	// static Python object are collected at program exit and create bunches of problems.
	// protect them from such collection by incrementing ref count.
	static inline Object boost(Object obj) {
		obj.increment_reference_count() ;
		return obj ;
	}
	static inline PyObject* boost(PyObject* obj) {
		Py_XINCREF(obj) ;
		return obj ;
	}

	extern PyObject* g_ellipsis ;

	static inline Module import_module(::string const& name) {
		return Module(PyImport_ImportModule(name.c_str()),true/*clobber*/) ;
	}

	// like PyErr_Print, but return text instead of printing it (Python API does not provide any means to do this !)
	::string err_str() ;

	struct Match ;

	struct Pattern : Object {
		friend ::ostream& operator<<( ::ostream& , Pattern const& ) ;
		// cxtors & casts
		Pattern(                       ) : Object{static_cast<PyObject*>(nullptr)} {}
		Pattern(Object   const& from   ) : Object{from                           } {}
		Pattern(::string const& pattern) : Pattern{} {
			static Callable py_compile = boost(import_module("re").getAttr("compile")) ;
			static_cast<Object&>(*this) = py_compile.apply(TupleN(String(pattern))) ;
		}
		// accesses
		::string pattern() const { return String(getAttr("pattern")) ; }
		// services
		Match match(::string const&) const ;
	} ;

	struct Match : Object {
		friend ::ostream& operator<<( ::ostream& , Pattern const& ) ;
		// cxtors & casts
		using Object::Object ;
		Match(const Object& from) : Object(from) {}
		bool operator +() const { return **this && *this!=None() ; }
		bool operator !() const { return !+*this                 ; }
		// accesses
		::string operator[](::string const& key) const {
			// find py_group by executing "re.compile('').fullmatch('')._class__.group"
			static Callable py_group = boost(
					Callable(
							Callable(import_module("re").getAttr("compile"))
						.	apply(TupleN(String("")))
						.	getAttr("fullmatch")
					)
				.	apply(TupleN(String("")))
				.	type()
				.	getAttr("group")
			) ;
			return String(py_group.apply(TupleN( *this , String(key) ))) ;
		}
	} ;

	struct Gil {
		Gil() : state{PyGILState_Ensure()} {}
		~Gil() { PyGILState_Release(state) ; }
		// data
		PyGILState_STATE state ;
	} ;

	//
	// implementation
	//

	inline Match Pattern::match(::string const& target) const {
		// find py_mach by executing "re.compile('').__class__.fullmatch"
		static Callable py_match = boost(
				Callable(import_module("re").getAttr("compile"))
			.	apply(TupleN(String("")))
			.	type()
			.	getAttr("fullmatch")
		) ;
		return Match(py_match.apply(TupleN( *this , String(target) ))) ;
	}

}
