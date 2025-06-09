// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"

#if HAS_PCRE
	#define PCRE2_CODE_UNIT_WIDTH 8
	#include <pcre2.h>
#else
	#include <regex>
	using std::regex       ;
	using std::regex_match ;
	using std::smatch      ;
	using std::sub_match   ;
#endif

// /!\ this interface assumes that all variable parts are enclosed within () : this simpliies a lot prefix and suffix identification

#if HAS_PCRE
	enum class RegExprUse : uint8_t {
		Unused
	,	Old
	,	New
	} ;
#endif

namespace Re {

	struct Match   ;
	struct RegExpr ;
	using Pattern = ::vmap_s<Bool3/*capturing*/> ; // maybe means fixed parts

	::string escape(::string const& s) ;

	#if HAS_PCRE

		inline void swap( Match& a , Match& b ) ;
		struct Match {
			friend RegExpr ;
			friend void swap( Match& a , Match& b ) ;
		private :
			// cxtors & casts
			Match() = default ;
			//
			Match( RegExpr const& re , ::string const& s , Bool3 chk_psfx ) ; // chk_psfx=Maybe means check size only
		public :
			~Match() {
				if (_data) pcre2_match_data_free(_data) ;
			}
			//
			Match           (Match&& m) { swap(self,m) ;               }
			Match& operator=(Match&& m) { swap(self,m) ; return self ; }
			// accesses
			bool operator+() const { return _data && pcre2_get_ovector_pointer(_data)[0]!=PCRE2_UNSET ; }
			//
			::string_view group( ::string const& subject , size_t i ) const {
				PCRE2_SIZE const* v = pcre2_get_ovector_pointer(_data) ;
				SWEAR( v[2*i+1]<=subject.size() , v[2*i],v[2*i+1],subject ) ;
				return { subject.data()+v[2*i] , v[2*i+1]-v[2*i] } ;
			}
			// data
		private :
			pcre2_match_data* _data = nullptr ;
		} ;
		inline void swap( Match& a , Match& b ) {
			::swap( a._data , b._data ) ;
		}

		inline void swap( RegExpr& a , RegExpr& b ) ;
		struct RegExpr {
			friend Match ;
			friend void swap( RegExpr& a , RegExpr& b ) ;
			using Use = RegExprUse ;
			static constexpr size_t ErrMsgSz = 120 ;                                               // per PCRE doc
			struct Cache {
				// cxtors & casts
				template<IsOStream S> void serdes(S& os) const {
					// START_OF_VERSIONING
					::vector_s        keys  ;
					::vector<uint8_t> bytes ;
					// END_OF_VERSIONING
					uint8_t*                    buf     = nullptr ;
					PCRE2_SIZE                  cnt     = 0       ;
					::vector<pcre2_code      *> created ;
					::vector<pcre2_code const*> codes   ;           codes.reserve(_cache.size()) ; // include useless unused entries, not a big deal
					/**/                                            keys .reserve(_cache.size()) ; // .
					for( auto const& [k,v] : _cache ) {
						switch (v.second) {
							case Use::Unused :                            continue ; // dont save unused regexprs
							case Use::New    : codes.push_back(v.first) ; break    ; // new regexprs are usable as is
							case Use::Old    :
								if (!_has_new()) {
									codes.push_back(v.first) ;
								} else {
									created.push_back(_s_compile(k)) ;               // unless all are old, old regexprs must be recompiled because they use their own character tables ...
									codes.push_back(created.back()) ;                // ... and we cannot serialize with different character tables
								}
							break ;
						}
						keys.push_back(k) ;
					}
					//
					int32_t n_codes = ::pcre2_serialize_encode( codes.data() , codes.size() , &buf , &cnt , nullptr/*general_context*/ ) ; if (n_codes<0) throw _s_err_msg(n_codes) ;
					SWEAR( size_t(n_codes)==keys.size() , n_codes , keys.size() ) ;
					for( ::pcre2_code* c : created ) ::pcre2_code_free(c) ;          // free what has been created
					//
					bytes.resize(size_t(cnt)) ; ::memcpy( bytes.data() , buf , cnt ) ;
					// START_OF_VERSIONING
					::serdes(os,keys ) ;
					::serdes(os,bytes) ;
					// END_OF_VERSIONING
					//
					::pcre2_serialize_free(buf) ;
				}
				template<IsIStream S> void serdes(S& is) {
					// START_OF_VERSIONING
					::vector_s        keys  ;
					::vector<uint8_t> bytes ;
					::serdes(is,keys ) ;
					::serdes(is,bytes) ;
					// END_OF_VERSIONING
					::vector<pcre2_code*> codes ;
					//
					PCRE2_SIZE n_codes = ::pcre2_serialize_get_number_of_codes(bytes.data()) ; SWEAR( n_codes==keys.size() , n_codes , keys.size() ) ; if (n_codes<0) throw _s_err_msg(n_codes) ;
					codes.resize(n_codes) ;
					::pcre2_serialize_decode( codes.data() , n_codes , bytes.data() , nullptr/*general_context*/ ) ;
					//
					SWEAR(!_cache) ;
					for( size_t i : iota(keys.size()) ) {
						bool inserted = _cache.try_emplace(keys[i],codes[i],Use::Unused).second ; SWEAR(inserted,keys[i]) ;
					}
					_n_unused = keys.size() ;
				}
				// accesses
				bool _has_new() const { return _n_unused<0 ; }
				// services
				bool steady() const {
					return !_n_unused ;
				}
				pcre2_code const* insert(::string const& infix) ;
				// data
			private :
				::umap_s<::pair<pcre2_code const*,Use/*use*/>> _cache    ;
				ssize_t                                        _n_unused = 0 ;       // <0 if new codes
			} ;
			// statics
		private :
			static ::pcre2_code* _s_compile(::string const& infix) ;
			static ::string _s_err_msg(int err_code) {
				char err_buf[RegExpr::ErrMsgSz] ;
				::pcre2_get_error_message(err_code,_s_cast_in(err_buf),sizeof(err_buf)) ;
				return ::string(err_buf) ;
			}
			static uint8_t const* _s_cast_in(char const* p) { return ::launder(reinterpret_cast<uint8_t const*>(p)) ; }
			static uint8_t      * _s_cast_in(char      * p) { return ::launder(reinterpret_cast<uint8_t      *>(p)) ; }

			// static data
		public :
			static Cache s_cache ;
			// cxtors & casts
			RegExpr() = default ;
			RegExpr( Pattern  const& pattern , bool cache=false ) ;
			RegExpr( ::string const& pattern , bool cache=false ) : RegExpr{{{pattern,No}},cache} {}
			//
			RegExpr           (RegExpr&& re) { swap(self,re) ;               }
			RegExpr& operator=(RegExpr&& re) { swap(self,re) ; return self ; }
			//
			~RegExpr() { if (_own) ::pcre2_code_free(const_cast<pcre2_code*>(_code)) ; }
			// services
			Match match( ::string const& subject , Bool3 chk_psfx=Yes ) const { // chk_psfx=Maybe means check size only
				return { self , subject , chk_psfx } ;
			}
			size_t mark_count() const {
				uint32_t cnt ;
				pcre2_pattern_info( _code , PCRE2_INFO_CAPTURECOUNT , &cnt ) ;
				return cnt ;
			}
			// data
		private :
			::string          _pfx   ;                                          // fixed prefix
			::string          _sfx   ;                                          // fixed suffix
			::vector_s        _infxs ;                                          // internal fixed parts
			pcre2_code const* _code  = nullptr ;                                // only contains code for infix part, shared and stored in s_store
			bool              _own   = false   ;                                // if true <=> _code is private and must be freed in dxtor
		} ;
		inline void swap( RegExpr& a , RegExpr& b) {
			::swap( a._pfx   , b._pfx   ) ;
			::swap( a._sfx   , b._sfx   ) ;
			::swap( a._infxs , b._infxs ) ;
			::swap( a._code  , b._code  ) ;
			::swap( a._own   , b._own   ) ;
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
			//
			::string_view group( ::string const& , size_t i ) const {
				::sub_match sm = ::smatch::operator[](i) ;
				return { sm.first , sm.second } ;
			}
		} ;

		struct RegExpr : private ::regex {
			friend Match ;
			static constexpr flag_type Flags = ECMAScript|optimize ;
			struct Cache {                                                                                       // there is no serialization facility and cache is not implemented, fake it
				static constexpr bool steady() { return true ; }
			} ;
			// statics
			static ::string _s_mk_pattern(Pattern const& pattern) {
				::string res ;
				for( auto const& [s,capture] : pattern ) {
					switch (capture) {
						case Maybe : res << escape(s)     ; break ;
						case Yes   : res << '('  <<s<<')' ; break ;
						case No    : res << "(?:"<<s<<')' ; break ;
					DF}
				}
				return res ;
			}
			// static data
			static Cache s_cache ;
			// cxtors & casts
			RegExpr() = default ;
			RegExpr( Pattern  const& pattern , bool /*cache*/=false ) : ::regex{_s_mk_pattern(pattern),Flags} {} // cache is ignored as no cache is implemented
			RegExpr( ::string const& pattern , bool /*cache*/=false ) : ::regex{pattern               ,Flags} {}
			// services
			Match match( ::string const& subject , Bool3 /*chk_psfx*/=Yes ) const {                              // chk_psfx=Maybe means check size only
				Match res ;
				::regex_match(subject,res,self) ;
				return res ;
			}
			size_t mark_count() const {
				return ::regex::mark_count() ;
			}
		} ;

	#endif

}
