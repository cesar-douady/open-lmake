// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "CXX/Objects.hxx"
#include "CXX/Extensions.hxx"

#include "utils.hh"

namespace Py {

	void init() ;

	// static Python object are collected at program exit and create bunches of problems.
	// protect them from such collection by incrementing ref count.
	// must be called on all statically allocated objects such as :
	// static Dict my_static_dict ; mk_static(my_static_dict) ;
	static inline Object mk_static(Object obj) {
		obj.increment_reference_count() ;
		return obj ;
	}

	static inline Module import_module(::string const& name) {
		return Module(PyImport_ImportModule(name.c_str()),true/*clobber*/) ;
	}

	struct Match ;

	struct Pattern : Object {
		friend ::ostream& operator<<( ::ostream& , Pattern const& ) ;
		// cxtors & casts
		Pattern(                       ) : Object{static_cast<PyObject*>(nullptr)} {}
		Pattern(Object   const& from   ) : Object{from                           } {}
		Pattern(::string const& pattern) : Pattern{} {
			static Callable py_compile = mk_static(import_module("re").getAttr("compile")) ;
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
			static Callable py_group = mk_static(
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

	// implementation
	inline Match Pattern::match(::string const& target) const {
		// find py_mach by executing "re.compile('').__class__.fullmatch"
		static Callable py_match = mk_static(
				Callable(import_module("re").getAttr("compile"))
			.	apply(TupleN(String("")))
			.	type()
			.	getAttr("fullmatch")
		) ;
		return Match(py_match.apply(TupleN( *this , String(target) ))) ;
	}

}
