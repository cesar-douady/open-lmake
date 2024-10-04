// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

namespace Re {

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

		::umap_s<pcre2_code*> RegExpr::s_code_store ;

		RegExpr::RegExpr( ::string const& pattern , bool /*fast*/ , bool /*no_groups*/ ) {
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
			::pair it_inserted = s_code_store.try_emplace( {start,sz} ) ;
			if (!it_inserted.second) {
				_code = it_inserted.first->second ;
				return ;
			}
			//
			int        err_code = 0 ;
			PCRE2_SIZE err_pos  = 0 ;
			_code = pcre2_compile(
				_cast_in(start) , sz
			,	PCRE2_ANCHORED | PCRE2_DOLLAR_ENDONLY | PCRE2_DOTALL | PCRE2_ENDANCHORED
			,	&err_code , &err_pos
			,	nullptr/*context*/
			) ;
			if (!_code) {
				char err_buf[ErrMsgSz] ;
				pcre2_get_error_message(err_code,_cast_in(err_buf),sizeof(err_buf)) ;
				throw ::string(err_buf)+" at position "+err_pos ;
			}
			it_inserted.first->second = _code ;
		}

	#endif
}
