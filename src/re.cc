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

	::string escape(::string const& s) {
		static constexpr ::array<bool,256> IsSpecial = []() {
			::array<bool,256> res = {} ;
			for( char const* p = "()[.*+?|\\{^$" ; *p ; p++ ) res[*p] = true ; // list from https://www.pcre.org/current/doc/html/pcre2pattern.html, under chapter CHARACTERS AND METACHARACTERS
			return res ;
		}() ;
		::string res ; res.reserve(s.size()+(s.size()>>4)) ;                   // take a little margin for escapes
		for( char c : s ) {
			if (IsSpecial[c]) res += '\\' ;                                    // escape specials
			/**/              res += c    ;
		}
		return res ;
	}

	#if HAS_PCRE

		//
		// Match
		//

		Match::Match( RegExpr const& re , ::string const& subject , Bool3 chk_psfx ) {
			size_t psfx_sz = re._pfx.size()+re._sfx.size() ;
			switch (chk_psfx) {
				case Yes :
					if (subject.size()<psfx_sz       ) return ;
					if (!subject.starts_with(re._pfx)) return ;
					if (!subject.ends_with  (re._sfx)) return ;
				break ;
				case Maybe :
					if (subject.size()<psfx_sz) return ;
					SWEAR( subject.starts_with(re._pfx) , subject,re._pfx,re._sfx ) ;
					SWEAR( subject.ends_with  (re._sfx) , subject,re._pfx,re._sfx ) ;
				break ;
				case No :
					SWEAR( subject.size()>=psfx_sz      , subject,re._pfx,re._sfx ) ;
					SWEAR( subject.starts_with(re._pfx) , subject,re._pfx,re._sfx ) ;
					SWEAR( subject.ends_with  (re._sfx) , subject,re._pfx,re._sfx ) ;
				break ;
			}
			if (true) {                                                                               // fast path : check infixes first to avoid matching in case of negative result
				::string_view core { &subject[re._pfx.size()] , subject.size()-psfx_sz } ;
				size_t pos = 0 ;
				for( ::string const& infx : re._infxs ) {
					pos = core.find(infx,pos) ;
					if (pos==Npos) return ;                                                           // if an infix cannot be found, no hope to match on the regexpr
					pos += infx.size() ;
				}
			}
			// full match
			_data = pcre2_match_data_create_from_pattern(re._code,nullptr) ;
			SWEAR(pcre2_get_ovector_count(_data)>0) ;
			pcre2_get_ovector_pointer(_data)[0] = PCRE2_UNSET ;
			pcre2_match(
				re._code
			,	RegExpr::_s_cast_in(subject.c_str()) , subject.size()-re._sfx.size() , re._pfx.size() // use subject (and not core) so group positions are correct
			,	0/*options*/
			,	_data
			,	nullptr/*context*/
			) ;
		}

		//
		// RegExpr
		//

		::pcre2_code* RegExpr::_s_compile(::string const& infix) {
			#if HAS_PCRE_ENDANCHORED
				::string const& infix_for_compile = infix ;
			#else
				#define PCRE2_ENDANCHORED 0
				::string infix_for_compile = infix+'$' ; // work around missing flag
			#endif
			int         err_code = 0 ;
			PCRE2_SIZE  err_pos  = 0 ;
			pcre2_code* code     = ::pcre2_compile(
				_s_cast_in(infix_for_compile.data()) , infix_for_compile.size()
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

		RegExpr::RegExpr( Pattern const& pattern , bool cache ) : _own{!cache} {
			size_t start = 0              ; while ( start<pattern.size() && pattern[start].second==Maybe ) start++ ;
			size_t end   = pattern.size() ; while ( end>start            && pattern[end-1].second==Maybe ) end  -- ;
			//
			for( size_t i : iota(0  ,start         ) ) _pfx << pattern[i].first ;
			for( size_t i : iota(end,pattern.size()) ) _sfx << pattern[i].first ;
			//
			::string pat          ;
			Bool3    prev_capture = No ;
			for( size_t i : iota(start,end) )
				switch (pattern[i].second) {
					case Yes   : pat <<'('  <<pattern[i].first<<')' ; break ;
					case No    : pat <<"(?:"<<pattern[i].first<<')' ; break ;
					case Maybe :
						if (prev_capture==Maybe) _infxs.back() << pattern[i].first  ;
						else                     _infxs.push_back(pattern[i].first) ;
						pat << escape(pattern[i].first) ;
					break ;
				DF}
			if (cache) _code = s_cache.insert(pat) ;
			else       _code = _s_compile    (pat) ;
		}

	#endif
}
