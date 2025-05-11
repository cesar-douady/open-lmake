// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

namespace Re {

	//
	// RegExpr
	//

	RegExpr::Cache RegExpr::s_cache ;

	#if HAS_PCRE

		//
		// Match
		//

		Match::Match( RegExpr const& re , ::string const& s , Bool3 chk_psfx ) {
			switch (chk_psfx) {
				case Yes :
					if   ( s.size() <  re.pfx.size()+re.sfx.size() ) return ;
					if   ( !s.starts_with(re.pfx)                  ) return ;
					if   ( !s.ends_with  (re.sfx)                  ) return ;
				break ;
				case Maybe :
					if   ( s.size() <  re.pfx.size()+re.sfx.size() ) return ;
					SWEAR( s.starts_with(re.pfx)                   , s,re.pfx,re.sfx ) ;
					SWEAR( s.ends_with  (re.sfx)                   , s,re.pfx,re.sfx ) ;
				break ;
				case No :
					SWEAR( s.size() >= re.pfx.size()+re.sfx.size() , s,re.pfx,re.sfx ) ;
					SWEAR( s.starts_with(re.pfx)                   , s,re.pfx,re.sfx ) ;
					SWEAR( s.ends_with  (re.sfx)                   , s,re.pfx,re.sfx ) ;
				break ;
			}
			_data = pcre2_match_data_create_from_pattern(re._code,nullptr) ;
			SWEAR(pcre2_get_ovector_count(_data)>0) ;
			pcre2_get_ovector_pointer(_data)[0] = PCRE2_UNSET ;
			pcre2_match(
				re._code
			,	RegExpr::_s_cast_in(s.c_str()) , s.size()-re.sfx.size() , re.pfx.size()
			,	0/*options*/
			,	_data
			,	nullptr/*context*/
			) ;
		}

		//
		// RegExpr
		//

		::pcre2_code* RegExpr::_s_compile(::string const& infix) {
			int         err_code = 0 ;
			PCRE2_SIZE  err_pos  = 0 ;
			pcre2_code* code     = ::pcre2_compile(
				_s_cast_in(infix.data()) , infix.size()
			,	PCRE2_ANCHORED | PCRE2_DOLLAR_ENDONLY | PCRE2_DOTALL | PCRE2_ENDANCHORED
			,	&err_code , &err_pos
			,	nullptr/*context*/
			) ;
			if (!code) throw cat(_s_err_msg(err_code)," at position ",err_pos) ;
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

		RegExpr::RegExpr( ::string const& pattern , bool cache , bool with_paren ) : _own{!cache} {
			size_t      sz    = pattern.size()  ;
			const char* start = pattern.c_str() ;
			const char* end   = start+sz        ;
			if (with_paren) {                         // find fixed prefix and suffix, variable parts are assumed to be enclosed within ()
				for(; start<end ; start++ ) {
					if      (*start=='\\') { SWEAR(start+1<end) ; start++ ; }
					else if (*start=='(' )   break ;
					pfx += *start ;
				}
				sz = 0 ;
				for( const char* p=start ; p<end ; p++ ) {
					if      (*p=='\\') { SWEAR(p+1<end) ; p++ ;                    }
					else if (*p==')' ) { sfx.clear() ; sz = p+1-start ; continue ; }
					sfx += *p ;
				}
			}
			if (cache) _code = s_cache.insert({start,sz}) ;
			else       _code = _s_compile    ({start,sz}) ;
		}

	#endif
}
