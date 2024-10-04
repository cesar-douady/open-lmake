// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

#if HAS_PCRE
	#define PCRE2_CODE_UNIT_WIDTH 8
	#include <pcre2.h>
#else
	#include <regex>
#endif

// /!\ this interface assumes that all variable parts are enclosed within () : this simpliies a lot prefix and suffix identification

namespace Re {

	struct Match   ;
	struct RegExpr ;

	static const ::string SpecialChars = "()[.*+?|\\{^$" ;   // in decreasing frequency of occurrence, ...
	inline ::string escape(::string const& s) {              // ... list from https://www.pcre.org/current/doc/html/pcre2pattern.html, under chapter CHARACTERS AND METACHARACTERS
		::string res ; res.reserve(s.size()+(s.size()>>4)) ; // take a little margin for escapes
		for( char c : s ) {
			if (SpecialChars.find(c)!=Npos) res += '\\' ;    // escape specials
			/**/                            res += c    ;
		}
		return res ;
	}

	#if HAS_PCRE

		inline void swap( Match& a , Match& b ) ;
		struct Match {
			friend RegExpr ;
			friend void swap( Match& a , Match& b ) ;
			// statics
		private :
			// cxtors & casts
			Match() = default ;
			//
			Match( RegExpr const& re , ::string const& s , bool chk_psfx ) ;
		public :
			~Match() {
				if (_data) pcre2_match_data_free(_data) ;
			}
			//
			Match           (Match&& m) { swap(*this,m) ;                }
			Match& operator=(Match&& m) { swap(*this,m) ; return *this ; }
			// accesses
			bool operator+() const { return _data && pcre2_get_ovector_pointer(_data)[0]!=PCRE2_UNSET ; }
			bool operator!() const { return !+*this                                                   ; }
			//
			::string_view operator[](size_t i) const {
				PCRE2_SIZE const* v = pcre2_get_ovector_pointer(_data) ;
				return { _subject.data()+v[2*i] , v[2*i+1]-v[2*i] } ;
			}
			// data
		private :
			pcre2_match_data* _data    = nullptr ;
			::string          _subject ;
		} ;
		inline void swap( Match& a , Match& b ) {
			::swap(a._data   ,b._data   ) ;
			::swap(a._subject,b._subject) ;
		}

		inline void swap( RegExpr& a , RegExpr& b ) ;
		struct RegExpr {
			friend Match ;
			friend void swap( RegExpr& a , RegExpr& b ) ;
			static constexpr size_t ErrMsgSz = 120 ;      // per PCRE doc
			// static data
			static ::umap_s<pcre2_code*> s_code_store ;
			// cxtors & casts
			RegExpr() = default ;
			RegExpr( ::string const& pattern , bool fast=false , bool no_groups=false ) ;
			//
			RegExpr           (RegExpr&& re) { swap(*this,re) ;                }
			RegExpr& operator=(RegExpr&& re) { swap(*this,re) ; return *this ; }
			// services
			Match match( ::string const& subject , bool chk_psfx=true ) const {
				return { *this , subject , chk_psfx } ;
			}
			size_t mark_count() const {
				uint32_t cnt ;
				pcre2_pattern_info( _code , PCRE2_INFO_CAPTURECOUNT , &cnt ) ;
				return cnt ;
			}
			// data
			::string pfx ;                                // fixed prefix
			::string sfx ;                                // fixed suffix
		private :
			pcre2_code* _code = nullptr ;                 // only contains code for infix part, shared and stored in s_store
		} ;
		inline void swap( RegExpr& a , RegExpr& b ) {
			::swap(a.pfx  ,b.pfx  ) ;
			::swap(a.sfx  ,b.sfx  ) ;
			::swap(a._code,b._code) ;
		}

	#else

		struct Match : private ::smatch {
			friend RegExpr ;
			// cxtors & casts
		private :
			using ::smatch::smatch ;
			// accesses
		public :
			bool operator+() const { return !empty() ; }
			bool operator!() const { return !+*this  ; }
			//
			::string_view operator[](size_t i) const {
				::sub_match sm = ::smatch::operator[](i) ;
				return {sm.first,sm.second} ;
			}
		} ;

		struct RegExpr : private ::regex {
			friend Match ;
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
			Match match( ::string const& subject , bool /*chk_psfx*/=true ) const {
				Match res ;
				::regex_match(subject,res,*this) ;
				return res ;
			}
			size_t mark_count() const {
				return ::regex::mark_count() ;
			}
		} ;

	#endif

}
