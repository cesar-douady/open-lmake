// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

namespace Re {

	//
	// RegExpr
	//

	RegExpr::Cache RegExpr::s_cache ;

	#if HAS_PCRE

		static uint8_t const* _cast_in(char const* p) { return reinterpret_cast<uint8_t const*>(p) ; }
		static uint8_t      * _cast_in(char      * p) { return reinterpret_cast<uint8_t      *>(p) ; }

		//
		// Match
		//

		static ::string _mk_subject( ::string const& s , RegExpr const& re ) {
			if ( s.size() >= re.pfx.size()+re.sfx.size() ) return s.substr( re.pfx.size() , s.size()-(re.pfx.size()+re.sfx.size()) ) ;
			else                                           return {}                                                                 ;
		}
		Match::Match( RegExpr const& re , ::string const& s , bool chk_psfx ) : _subject{ _mk_subject(s,re) } {
			if (chk_psfx) {
				if (!s.starts_with(re.pfx)) return ;
				if (!s.ends_with  (re.sfx)) return ;
			} else {
				SWEAR(s.starts_with(re.pfx),s,re.pfx,re.sfx) ;
				SWEAR(s.ends_with  (re.sfx),s,re.pfx,re.sfx) ;
			}
			_data = pcre2_match_data_create_from_pattern(re._code,nullptr) ;
			SWEAR(pcre2_get_ovector_count(_data)>0) ;
			pcre2_get_ovector_pointer(_data)[0] = PCRE2_UNSET ;
			pcre2_match(
				re._code
			,	_cast_in(_subject.c_str()) , _subject.size() , 0/*start_offset*/
			,	0/*options*/
			,	_data
			,	nullptr/*context*/
			) ;
		}

		//
		// RegExpr
		//

		static ::string _err_msg(int err_code) {
			char err_buf[RegExpr::ErrMsgSz] ;
			::pcre2_get_error_message(err_code,_cast_in(err_buf),sizeof(err_buf)) ;
			return ::string(err_buf) ;
		}

		void RegExpr::Cache::serdes(::ostream& s) const {
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
					case Use::Unused :                            continue ;               // dont save unused regexprs
					case Use::New    : codes.push_back(v.first) ; break    ;               // new regexprs are usable as is
					case Use::Old    :
						if (!_has_new()) {
							codes.push_back(v.first) ;
						} else {
							created.push_back(_s_compile(k)) ;                             // unless all are old, old regexprs must be recompiled because they use their own character tables ...
							codes.push_back(created.back()) ;                              // ... and we cannot serialize with different character tables
						}
					break ;
				}
				keys.push_back(k) ;
			}
			//
			int32_t n_codes = ::pcre2_serialize_encode( codes.data() , codes.size() , &buf , &cnt , nullptr/*general_context*/ ) ; if (n_codes<0) throw _err_msg(n_codes) ;
			SWEAR( size_t(n_codes)==keys.size() , n_codes , keys.size() ) ;
			for( ::pcre2_code* c : created ) ::pcre2_code_free(c) ;                        // free what has been created
			//
			bytes.resize(size_t(cnt)) ; ::memcpy(bytes.data(),buf,cnt) ;
			// START_OF_VERSIONING
			::serdes(s,keys ) ;
			::serdes(s,bytes) ;
			// END_OF_VERSIONING
			//
			::pcre2_serialize_free(buf) ;
		}

		void RegExpr::Cache::serdes(::istream& s) {
			// START_OF_VERSIONING
			::vector_s        keys  ;
			::vector<uint8_t> bytes ;
			::serdes(s,keys ) ;
			::serdes(s,bytes) ;
			// END_OF_VERSIONING
			::vector<pcre2_code*> codes ;
			//
			PCRE2_SIZE n_codes = ::pcre2_serialize_get_number_of_codes(bytes.data()) ; SWEAR( n_codes==keys.size() , n_codes , keys.size() ) ; if (n_codes<0) throw _err_msg(n_codes) ;
			codes.resize(n_codes) ;
			::pcre2_serialize_decode( codes.data() , n_codes , bytes.data() , nullptr/*general_context*/ ) ;
			//
			for( auto const& [_,v] : _cache )
				if (v.second==Use::Unused) ::pcre2_code_free(const_cast<::pcre2_code*>(v.first)) ;
			_cache.clear() ;
			for( size_t i=0 ; i<keys.size() ; i++ ) {
				bool inserted = _cache.try_emplace(keys[i],codes[i],Use::Unused).second ; SWEAR(inserted,keys[i]) ;
			}
			_n_unused = keys.size() ;
		}

		::pcre2_code* RegExpr::_s_compile(::string const& infix) {
			int         err_code = 0 ;
			PCRE2_SIZE  err_pos  = 0 ;
			pcre2_code* code     = ::pcre2_compile(
				_cast_in(infix.data()) , infix.size()
			,	PCRE2_ANCHORED | PCRE2_DOLLAR_ENDONLY | PCRE2_DOTALL | PCRE2_ENDANCHORED
			,	&err_code , &err_pos
			,	nullptr/*context*/
			) ;
			if (!code) throw _err_msg(err_code)+" at position "+err_pos ;
			return code ;
		}
		::pcre2_code const* RegExpr::Cache::insert(::string const& infix) {
			::pair                         it_inserted = _cache.try_emplace(infix) ;
			::pair<pcre2_code const*,Use>& entry       = it_inserted.first->second ;
			if (it_inserted.second) {
				entry     = {_s_compile(infix),Use::New} ;
				_n_unused = -1                           ;
			} else if (entry.second==Use::Unused) {
				entry.second = Use::Old ;
				_n_unused-- ;
			}
			return entry.first ;
		}

		RegExpr::RegExpr(::string const& pattern) {
			const char* start_pat = pattern.c_str()          ;
			const char* end_pat   = start_pat+pattern.size() ;
			const char* start     = nullptr/*garbage*/       ;
			size_t      sz        = 0      /*garbage*/       ;
			// find prefix and suffix
			const char* p = start_pat ;
			for(; p<end_pat ; p++ ) {
				if      (*p=='\\') { SWEAR(p+1<end_pat) ; p++ ; }
				else if (*p=='(' )   break ;                                     // /!\ variable parts are assumed to be enclosed within ()
				pfx += *p ;
			}
			start = p ;
			for(; p<end_pat ; p++ ) {
				if      (*p=='\\') { SWEAR(p+1<end_pat) ; p++ ;                }
				else if (*p==')' ) { sfx.clear() ; sz = p+1-start ; continue ; } // /!\ variable parts are assumed to be enclosed within ()
				sfx += *p ;
			}
			_code = s_cache.insert({start,sz}) ;
		}

	#endif
}
