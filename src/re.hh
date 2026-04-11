// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"

#if __SIZEOF_POINTER__!=8
	#undef HAS_PCRE
	#define HAS_PCRE 0 // PCRE is only supported in 64 bits
#endif

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

#if HAS_PCRE

	enum class RegExprSpecial : uint8_t {
		Plain                             // call regexpr package
	,	Single                            // no stem
	,	AnyNoCapture                      // a single stem as non-capturing .*
	,	AnyCapture                        // a single stem as     capturing .*
	,	NonEmptyNoCapture                 // a single stem as non-capturing .+
	,	NonEmptyCapture                   // a single stem as     capturing .+
	} ;

	enum class RegExprUse : uint8_t {
		Unused
	,	Old
	,	New
	} ;

#endif

namespace Re {

	using Pattern = ::vmap_s<Bool3/*capturing*/>        ; // maybe means fixed parts
	using Match   = ::optional<::vector<::string_view>> ;

	::string escape(::string const& s) ;

	#if HAS_PCRE

		struct RegExpr ;
		struct _RegExprBits {
			using Special = RegExprSpecial ;
			using Use     = RegExprUse     ;
			struct Data {
				Data (RegExpr const& re) ;
				~Data(                 ) { close() ; }
				//
				Data           (        ) = default ;
				Data           (Data&& d) : p{d.p} {                                d.p = nullptr ;                                  }
				Data& operator=(Data&& d)          { ::pcre2_match_data* dp = d.p ; d.p = nullptr ; close() ; p = dp ; return self ; } // /!\ beware of auto-assignment
				//
				void close() { if (p) ::pcre2_match_data_free(p) ; }
				// accesses
				bool operator+() const { return p ; }
				// data
				::pcre2_match_data* p = nullptr ;
			} ;
			// data
			::string            _pfx     ;                                                         // fixed prefix
			::string            _sfx     ;                                                         // fixed suffix
			::vector_s          _infxs   ;                                                         // internal fixed parts
			Data                _data    ;
			::pcre2_code const* _code    = nullptr                 ;                               // only contains code for infix part, shared and stored in s_store
			bool                _own     = false                   ;                               // if true <=> _code is private and must be freed in dxtor
			Special             _special = {}                      ;
			thread::id          _tid     = ::this_thread::get_id() ;
		} ;
		struct RegExpr : private _RegExprBits {
			friend _RegExprBits::Data ;
			static constexpr size_t ErrMsgSz = 120 ;                                               // per PCRE doc
			using _RegExprBits::Data ;
			struct Cache {
				// cxtors & casts
				~Cache() {
					for( auto const& [k,c_u] : _cache ) {
						SWEAR(c_u.first) ;
						::pcre2_code_free(const_cast<pcre2_code*>(c_u.first)) ;
					}
				}
				// services
				template<IsOStream S> void serdes(S& os) const {
					// START_OF_VERSIONING REPO
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
								if (!_n_new) {
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
					// START_OF_VERSIONING REPO
					::serdes(os,keys ) ;
					::serdes(os,bytes) ;
					// END_OF_VERSIONING
					//
					::pcre2_serialize_free(buf) ;
				}
				template<IsIStream S> void serdes(S& is) {
					// START_OF_VERSIONING REPO
					::vector_s        keys  ;
					::vector<uint8_t> bytes ;
					::serdes(is,keys ) ;
					::serdes(is,bytes) ;
					// END_OF_VERSIONING
					::vector<pcre2_code*> codes ;
					//
					PCRE2_SIZE n_codes = ::pcre2_serialize_get_number_of_codes(bytes.data()) ; if (n_codes!=keys.size()) throw n_codes<0?_s_err_msg(n_codes):"corrupted regexpr cache"s ;
					codes.resize(n_codes) ;
					::pcre2_serialize_decode( codes.data() , n_codes , bytes.data() , nullptr/*general_context*/ ) ;
					//
					SWEAR_PROD(!_cache) ;
					for( size_t i : iota(keys.size()) ) {
						#if 0 && defined(PCRE2_CONFIG_JIT)
							::pcre2_jit_compile( codes[i] , PCRE2_JIT_COMPLETE ) ;   // best effort, if there is an error, the code will work anyway
						#endif
						bool inserted = _cache.try_emplace(keys[i],codes[i],Use::Unused).second ; SWEAR(inserted,keys[i]) ;
					}
					_n_unused = keys.size() ;
				}
				// services
				bool steady() const {
					return !_n_unused && !_n_new ;
				}
				pcre2_code const* insert(::string const& infix) ;
				// data
			private :
				::umap_s<::pair<pcre2_code const*,Use/*use*/>> _cache    ;
				ssize_t                                        _n_unused = 0 ;
				ssize_t                                        _n_new    = 0 ;
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
			RegExpr( Pattern  const& pattern , bool cache ) ;
			RegExpr( ::string const& pattern , bool cache ) : RegExpr{{{pattern,No}},cache} {}
			//
			RegExpr           (RegExpr&& re) : _RegExprBits{::move(re)} {                                                 re._code = nullptr ;               }
			RegExpr& operator=(RegExpr&& re)                            { close() ; _RegExprBits::operator=(::move(re)) ; re._code = nullptr ; return self ; }
			//
			~RegExpr() { close() ; }
			//
			void close() {
				if ( _own && _code ) { ::pcre2_code_free(const_cast<pcre2_code*>(_code)) ; _code = nullptr ; }
			}
			// accesses
			bool has_stems() const { return _special!=Special::Single ; }
			Data data     () const { return { self }                  ; }
			// services
			// chk_psfx=Maybe means check size only
			// without Data, matching is not reentrant
			size_t n_marks  (                                                              ) const ;
			Match  match    ( ::string const& subject , Data const&   , Bool3 chk_psfx=Yes ) const ;
			bool   can_match( ::string const& s       , Data const& d , Bool3 cp      =Yes ) const {                                          return _n_match1( s , d     , cp )>=0 ; }
			Match  match    ( ::string const& s       ,                 Bool3 cp      =Yes ) const { SWEAR( ::this_thread::get_id()==_tid ) ; return match    ( s , _data , cp )    ; }
			bool   can_match( ::string const& s       ,                 Bool3 cp      =Yes ) const { SWEAR( ::this_thread::get_id()==_tid ) ; return can_match( s , _data , cp )    ; }
		private :
			int _n_match1( ::string const& subject , Data const& , Bool3 chk_psfx ) const ;
		} ;

		inline _RegExprBits::Data::Data(RegExpr const& re) {
			if (!re._special) p = ::pcre2_match_data_create_from_pattern(re._code,nullptr) ;
		}

	#else

		struct RegExpr : private ::regex {
			static constexpr flag_type Flags = ECMAScript|optimize ;
			using Data = ::monostate ;                               // useless : smallest possible type (avoid bool so they can be put in a vector with no burden)
			struct Cache {                                           // there is no serialization facility and cache is not implemented, fake it
				static constexpr bool steady() { return true ; }
			} ;
			// statics
		private :
			static bool _s_mk_has_stems(Pattern const& pattern) {
				return ::any_of( pattern , [](::pair_s<Bool3/*capturing*/> const& s_c) { return s_c.second!=Maybe ; } ) ;
			}
			static ::string _s_mk_pattern(Pattern const& pattern) {
				::string res ;
				for( auto const& [s,capture] : pattern ) {
					switch (capture) {
						case Maybe : res << escape(s)     ; break ;
						case Yes   : res << '('  <<s<<')' ; break ;
						case No    : res << "(?:"<<s<<')' ; break ;
					DF}                                              // NO_COV
				}
				return res ;
			}
			// static data
		public :
			static Cache s_cache ;
			// cxtors & casts
			RegExpr( Pattern  const& pattern , bool /*cache*/ ) : ::regex{_s_mk_pattern(pattern),Flags} , _has_stems{_s_mk_has_stems(pattern)} {} // cache is ignored as no cache is implemented
			RegExpr( ::string const& pattern , bool /*cache*/ ) : ::regex{pattern               ,Flags}                                        {}
			//
			RegExpr           (         ) = default ;
			RegExpr           (RegExpr&&) = default ;
			RegExpr& operator=(RegExpr&&) = default ;
			// accesses
			bool has_stems() const { return _has_stems ; }
			Data data     () const { return {}         ; }
			// services
			size_t n_marks() const {
				return mark_count() ;
			}
			Match match( ::string const& subject , Data& , Bool3 /*chk_psfx*/=Yes ) const {                                                       // chk_psfx=Maybe means check size only
				::smatch m ; ::regex_match( subject , m , self ) ;
				if (m.empty()) return {} ;
				//
				size_t n   = mark_count()              ;
				Match  res = ::vector<::string_view>() ; res->reserve(n+1) ;
				for( size_t i : iota(n+1) ) {                                                                                                     // res[0] is the whole subject
					::sub_match sm = m[i] ;
					if (sm.matched) res->emplace_back( sm.first , sm.second ) ;
					else            res->emplace_back(                      ) ;
				}
				return res ;
			}
			bool can_match( ::string const& subject , Data& , Bool3 /*chk_psfx*/=Yes ) const {                                                    // chk_psfx=Maybe means check size only
				::smatch m ; ::regex_match( subject , m , self ) ;
				return !m.empty() ;
			}
			Match  match    ( ::string const& subject , Bool3 chk_psfx=Yes ) const { return match    ( subject , ::ref(Data()) , chk_psfx ) ; }
			bool   can_match( ::string const& subject , Bool3 chk_psfx=Yes ) const { return can_match( subject , ::ref(Data()) , chk_psfx ) ; }
			// data
			bool _has_stems = false ;
		} ;

	#endif

}
