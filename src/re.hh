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

namespace Re {

	struct Match   ;
	struct RegExpr ;

	#if HAS_PCRE

		inline uint8_t const* _cast_in (char const* p) { return reinterpret_cast<uint8_t const*>(p) ; }
		inline uint8_t      * _cast_in (char      * p) { return reinterpret_cast<uint8_t      *>(p) ; }

		inline void swap( Match& a , Match& b ) ;
		struct Match {
			friend RegExpr ;
			friend void swap( Match& a , Match& b ) ;
			// statics
		private :
			pcre2_match_data* _s_mk_data(RegExpr const& re) ;
			// cxtors & casts
			Match() = default ;
			//
			Match( RegExpr const& re                     ) ;
			Match( RegExpr const& re , ::string const& s ) ;
		public :
			~Match() {
				pcre2_match_data_free(_data) ;
			}
			//
			Match           (Match&& m) { swap(*this,m) ;                }
			Match& operator=(Match&& m) { swap(*this,m) ; return *this ; }
			// accesses
			bool operator+() const { return pcre2_get_ovector_pointer(_data)[0]!=PCRE2_UNSET ; }
			bool operator!() const { return !+*this                                          ; }
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
			static constexpr size_t ErrMsgSz = 120 ; // per PCRE doc
			// cxtors & casts
			RegExpr() = default ;
			RegExpr( ::string const& pattern , bool /*fast*/=false , bool /*no_groups*/=false ) {
				int        err     = 0 ;
				PCRE2_SIZE err_pos = 0 ;
				_code = pcre2_compile(
					_cast_in(pattern.c_str()) , pattern.size()
				,	PCRE2_ANCHORED | PCRE2_DOTALL | PCRE2_ENDANCHORED
				,	&err , &err_pos
				,	nullptr/*context*/
				) ;
				if (!_code) {
					char err_buf[ErrMsgSz] ;
					pcre2_get_error_message(err,_cast_in(err_buf),sizeof(err_buf)) ;
					throw ::string(err_buf) ;
				}
			}
			~RegExpr() {
				pcre2_code_free(_code) ;
			}
			//
			RegExpr           (RegExpr&& re) { swap(*this,re) ;                }
			RegExpr& operator=(RegExpr&& re) { swap(*this,re) ; return *this ; }
			// services
			Match match    (::string const& subject) const { return { *this ,subject } ; }
			bool  can_match(::string const& subject) const { return +match(subject)    ; }
			size_t mark_count() const {
				uint32_t cnt ;
				pcre2_pattern_info( _code , PCRE2_INFO_CAPTURECOUNT , &cnt ) ;
				return cnt ;
			}
			// data
		private :
			pcre2_code* _code = nullptr ;
		} ;
		inline void swap( RegExpr& a , RegExpr& b ) {
			::swap(a._code,b._code) ;
		}

		inline pcre2_match_data* Match::_s_mk_data(RegExpr const& re) {
			pcre2_match_data* res = pcre2_match_data_create_from_pattern(re._code,nullptr) ;
			SWEAR(pcre2_get_ovector_count(res)>0) ;
			pcre2_get_ovector_pointer(res)[0] = PCRE2_UNSET ;
			return res ;
		}

		inline Match::Match( RegExpr const& re                     ) : _data{_s_mk_data(re)}               {}
		inline Match::Match( RegExpr const& re , ::string const& s ) : _data{_s_mk_data(re)} , _subject{s} {
			pcre2_match(
				re._code
			,	_cast_in(_subject.c_str()) , _subject.size() , 0/*start_offset*/
			,	0/*options*/
			,	_data
			,	nullptr/*context*/
			) ;
		}

	#else

		struct Match : private ::smatch {
			friend RegExpr ;
			// cxtors & casts
		private :
			using smatch::smatch ;
			// accesses
		public :
			bool operator+() const { return !empty() ; }
			bool operator!() const { return !+*this  ; }
			//
			::string_view operator[](size_t i) const {
				::sub_match sm = smatch::operator[](i) ;
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
			Match match(::string const& txt) const {
				Match res ;
				::regex_match(txt,res,*this) ;
				return res ;
			}
			bool can_match(::string const& txt) const {
				return +match(txt) ;
			}
			size_t mark_count() const {
				return ::regex::mark_count() ;
			}
		} ;

	#endif

}
