// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

#include <regex>

namespace Re {

	struct Match : ::smatch {
		// cxtors & casts
		using smatch::smatch ;
		// accesses
		::string_view operator[](size_t i) const {
			::sub_match sm = smatch::operator[](i) ;
			return {sm.first,sm.second} ;
		}
		//
		bool operator+() const { return !empty() ; }
		bool operator!() const { return !+*this  ; }
	} ;

	struct RegExpr : ::regex {
		static constexpr ::regex_constants::syntax_option_type None { 0 } ;
		// cxtors & casts
		RegExpr() = default ;
		RegExpr( ::string const& pattern , bool fast=false , bool no_groups=false ) :
			::regex{ pattern ,
				/**/         ::regex::ECMAScript
			|	(fast      ? ::regex::optimize   : None )
			|	(no_groups ? ::regex::nosubs     : None )
			}
		{}
		// services
		Match match(::string const& txt) const {
			Match res ;
			::regex_match(txt,res,*this) ;
			return res ;
		}
		bool can_match(::string const& txt) const {
			Match m ;
			return ::regex_match(txt,m,*this) ;
		}
	} ;

}
