// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

namespace Re {

	//
	// RegExpr
	//

	RegExpr::Cache RegExpr::s_cache ;

	// for details, refer to https://www.pcre.org/current/doc/html/pcre2pattern.html, under chapter CHARACTERS AND METACHARACTERS

	static constexpr ::array<bool,256> _EscapeIsSpecial = []() {
		::array<bool,256> res = {} ;
		for( char const* p = "()[].*+?|\\{}^$" ; *p ; p++ ) res[*p] = true ; // ] and } is necessary to analyze suffix in split_pattern
		return res ;
	}() ;

	::string escape(::string const& s) {
		::string res ; res.reserve(s.size()+(s.size()>>4)) ; // take a little margin for escapes
		for( char c : s ) {
			if (_EscapeIsSpecial[uint8_t(c)]) res += '\\' ;  // escape specials
			/**/                              res += c    ;
		}
		return res ;
	}

	#if HAS_PCRE

		static constexpr ::array<bool,256> _EscapeIsQuantifier = []() {
			::array<bool,256> res = {} ;
			for( char const* p = "*+?{" ; *p ; p++ ) res[*p] = true ; // { is the start of a quantifier
			return res ;
		}() ;

		static bool _is_alphanum(char c) {
			if ( '0'<=c && c<='9' ) return true  ;
			if ( 'a'<=c && c<='z' ) return true  ;
			if ( 'A'<=c && c<='Z' ) return true  ;
			/**/                    return false ;
		}
		static Pattern _mk_pattern(::string const& s) {
			Pattern res   { {{},Maybe} , {{},No} , {{},Maybe} } ;
			size_t  start = 0                                   ;
			size_t  end   = s.size()                            ;
			// analyze prefix
			for(; start<end ; start++ ) {
				char c     = s[start  ] ;
				char c1    = s[start+1] ;
				bool is_bs = c=='\\'    ;
				if (is_bs) {
					c  = c1         ;
					c1 = s[start+2] ;
					switch (c) {
						case 0   : if (start==end-1) goto Done ; else break ;         // embedded 0 is not forbidden by pcre2, but ending backslash is left to pcre to analyze
						case 'a' : c = '\a' ;                         break ;
						case 'e' : c = 0x1b ;                         break ;         // escape
						case 'f' : c = '\f' ;                         break ;
						case 'n' : c = '\n' ;                         break ;
						case 'r' : c = '\r' ;                         break ;
						case 't' : c = '\t' ;                         break ;
						default  : if (_is_alphanum(c)) goto Done ;                   // too many pitfalls to analyze suffix
					}
				} else if (_EscapeIsSpecial[uint8_t(c)])
					goto DoneStart ;
				if (_EscapeIsQuantifier[uint8_t(c1)])                                 // beware of (possibly escaped) plain char appearing as fixed, but followed by a quantifier
					goto DoneStart ;
				res[0].first += c     ;
				start        += is_bs ;                                               // clobber backslash
			}
			goto Done ;                                                               // fixed pattern, no suffix
		DoneStart :
			// avoid fancy escapes
			for( size_t backslash=start ; backslash<end ; backslash++ ) {
				if ( s[backslash]=='\\' && _is_alphanum(s[backslash+1]) ) goto Done ; // too many pitfalls to analyze suffix
			}
			// analyze suffix
			for(; start+1<end ; end-- ) {                                             // s[start] cannot be plain, so we can stop suffix analysis one char further
				char c  = s[end-1] ;
				char c1 = s[end-2] ;
				if (c1=='\\') {
					switch (c) {
						case 'a' : c = '\a' ; break ;
						case 'e' : c = 0x1b ; break ;                                 // escape
						case 'f' : c = '\f' ; break ;
						case 'n' : c = '\n' ; break ;
						case 'r' : c = '\r' ; break ;
						case 't' : c = '\t' ; break ;
						default  : SWEAR( !_is_alphanum(c) , c ) ;                    // fancy escapes have been avoided
					}
					end-- ;                                                           // clobber backslash
				} else {
					if (_EscapeIsSpecial[uint8_t(c)]) break ;
				}
				res[2].first += c ;                                                   // accumulate in reverse order
			}
			::reverse( res[2].first.begin() , res[2].first.end() ) ;                  // straighten out
		Done :
			res[1].first = s.substr(start,end-start) ;
			return res ;
		}

		::pcre2_code* RegExpr::_s_compile(::string const& infix) {
			#if HAS_PCRE_ENDANCHORED
				::string const& infix_for_compile = infix ;
			#else
				#define PCRE2_ENDANCHORED 0
				::string infix_for_compile = infix+'$' ;                              // work around missing flag
			#endif
			int         err_code = 0 ;
			PCRE2_SIZE  err_pos  = 0 ;
			::pcre2_code* code     = ::pcre2_compile(
				_s_cast_in(infix_for_compile.data()) , infix_for_compile.size()
			,	PCRE2_ANCHORED | PCRE2_DOLLAR_ENDONLY | PCRE2_DOTALL | PCRE2_ENDANCHORED
			,	&err_code , &err_pos
			,	nullptr/*context*/
			) ;
			#ifdef PCRE2_CONFIG_JIT
				::pcre2_jit_compile( code , PCRE2_JIT_COMPLETE ) ;                    // best effort, if there is an error, the code will work anyway
			#endif
			if (!code) throw cat(_s_err_msg(err_code)," at position ",err_pos) ;
			return code ;
		}
		::pcre2_code const* RegExpr::Cache::insert(::string const& infix) {
			::pair                           it_inserted = _cache.try_emplace(infix) ;
			::pair<::pcre2_code const*,Use>& entry       = it_inserted.first->second ;
			if (it_inserted.second) {
				try                     { entry = {_s_compile(infix),Use::New} ;    }
				catch (::string const&) { _cache.erase(it_inserted.first) ; throw ; } // do not insert errors in cache
				_n_new++ ;
			} else if (entry.second==Use::Unused) {
				entry.second = Use::Old ;
				_n_unused-- ;
			}
			return entry.first ;
		}

		RegExpr::RegExpr( Pattern const& pattern , bool cache ) {
			size_t start = 0              ; while ( start<pattern.size() && pattern[start].second==Maybe ) start++ ;
			size_t end   = pattern.size() ; while ( end>start            && pattern[end-1].second==Maybe ) end  -- ;
			//
			for( size_t i : iota(0  ,start         ) ) _pfx << pattern[i].first ;
			for( size_t i : iota(end,pattern.size()) ) _sfx << pattern[i].first ;
			//
			if (start==end) {
				_special = Special::Single ;
				return ;
			}
			if (end==start+1) {
				if ( pattern[start].first==".*" || pattern[start].first==".*?" ) {
					switch (pattern[start].second) {
						case Yes : _special = Special::AnyCapture   ; break ;
						case No  : _special = Special::AnyNoCapture ; break ;
					DF}                                                            // NO_COV
					return ;
				}
				if ( pattern[start].first==".+" || pattern[start].first==".+?" ) {
					switch (pattern[start].second) {
						case Yes : _special = Special::NonEmptyCapture   ; break ;
						case No  : _special = Special::NonEmptyNoCapture ; break ;
					DF}                                                            // NO_COV
					return ;
				}
			}
			::string pat          ;
			Bool3    prev_capture = No ;
			for( size_t i : iota(start,end) ) {
				switch (pattern[i].second) {
					case Yes   : pat <<'('  <<pattern[i].first<<')' ; break ;
					case No    : pat <<"(?:"<<pattern[i].first<<')' ; break ;
					case Maybe :
						if (prev_capture==Maybe) _infxs.back() << pattern[i].first  ;
						else                     _infxs.push_back(pattern[i].first) ;
						pat << escape(pattern[i].first) ;
					break ;
				DF}                                                                // NO_COV
				prev_capture = pattern[i].second ;
			}
			if (cache)                 _code = s_cache.insert(pat) ;
			else       { _own = true ; _code = _s_compile    (pat) ; }
			/**/                       _data = data()              ;
			#ifndef NDEBUG
				SWEAR(::pcre2_get_ovector_count(_data.p)>0) ;
			#endif
		}

		RegExpr::RegExpr( ::string const& pattern , bool cache ) : RegExpr{_mk_pattern(pattern),cache} {}

		size_t RegExpr::n_marks() const {
			switch (_special) {
				case Special::Single            : return 0 ;
				case Special::AnyNoCapture      : return 0 ;
				case Special::AnyCapture        : return 1 ;
				case Special::NonEmptyNoCapture : return 0 ;
				case Special::NonEmptyCapture   : return 1 ;
			DN}
			uint32_t cnt ;
			int      rc  = ::pcre2_pattern_info( _code , PCRE2_INFO_CAPTURECOUNT , &cnt ) ; SWEAR( rc==0 , rc ) ; // return value is not documented but seems to follow general convention
			return cnt ;
		}

		int RegExpr::_n_match1( ::string const& subject , Data const& d , Bool3 chk_psfx ) const {
			size_t psfx_sz = _pfx.size()+_sfx.size() ;
			switch (chk_psfx) {
				case Yes :
					if (subject.size()<psfx_sz    ) return -1 ;
					if (!subject.starts_with(_pfx)) return -1 ;
					if (!subject.ends_with  (_sfx)) return -1 ;
				break ;
				case Maybe :
					if (subject.size()<psfx_sz) return -1 ;
					SWEAR( subject.starts_with(_pfx) , subject,_pfx,_sfx ) ;
					SWEAR( subject.ends_with  (_sfx) , subject,_pfx,_sfx ) ;
				break ;
				case No :
					SWEAR( subject.size()>=psfx_sz   , subject,_pfx,_sfx ) ;
					SWEAR( subject.starts_with(_pfx) , subject,_pfx,_sfx ) ;
					SWEAR( subject.ends_with  (_sfx) , subject,_pfx,_sfx ) ;
				break ;
			}
			switch (_special) {
				case Special::Single            : return subject.size()==psfx_sz ? 1/*whole*/         : -1/*mismatch*/ ;
				case Special::AnyNoCapture      : return                           1/*whole*/                          ;
				case Special::AnyCapture        : return                           2/*whole+capture*/                  ;
				case Special::NonEmptyNoCapture : return subject.size()> psfx_sz ? 1/*whole*/         : -1/*mismatch*/ ;
				case Special::NonEmptyCapture   : return subject.size()> psfx_sz ? 2/*whole+capture*/ : -1/*mismatch*/ ;
			DN}
			SWEAR( _code && +d ) ;
			if (true) {                                                                        // fast path : check infixes first to avoid matching in case of negative result
				::string_view core { subject.data()+_pfx.size() , subject.size()-psfx_sz } ;
				size_t        pos  = 0                                                     ;
				for( ::string const& infx : _infxs ) {
					pos = core.find(infx,pos) ;
					if (pos==Npos) return -1 ;                                                 // if an infix cannot be found, no hope to match on the regexpr
					pos += infx.size() ;
				}
			}
			// full match
			int n1 = ::pcre2_match(                                                            // the whole group always exists impliciely
				_code
			,	RegExpr::_s_cast_in(subject.data()) , subject.size()-_sfx.size() , _pfx.size() // use subject (and not core) so group positions are correct
			,	0/*options*/
			,	d.p
			,	nullptr/*context*/
			) ;
			SWEAR( n1 , n1,subject ) ;                                                         // there is always at least the whole match as implicit group 0
			return n1 ;
		}

		Match RegExpr::match( ::string const& subject , Data const& d , Bool3 chk_psfx ) const {
			int n1 = _n_match1( subject , d , chk_psfx ) ; if (n1<0) return {} ;
			//
			Match res = ::vector<::string_view>() ; res->reserve(n1) ; // res[0] is the whole subject
			res->emplace_back(subject) ;                               // special case as prefix and suffix are not passed to pcre
			switch (n1) {
				case 0 : FAIL() ;
				case 1 : break ;
				case 2 :
					switch (_special) {
						case Special::Plain           : break ;
						case Special::AnyCapture      :
						case Special::NonEmptyCapture : res->emplace_back( subject.data()+_pfx.size() , subject.size()-(_pfx.size()+_sfx.size()) ) ; goto Return ;
					DF}
				[[fallthrough]] ;
				default : {
					PCRE2_SIZE* v = ::pcre2_get_ovector_pointer(d.p) ;
					for( size_t i : iota(1,n1) ) {
						if (v[2*i]!=PCRE2_UNSET) { SWEAR( v[2*i+1]<=subject.size() , i,n1,v[2*i],v[2*i+1],subject ) ; res->emplace_back( subject.data()+v[2*i] , v[2*i+1]-v[2*i] ) ; }
						else                                                                                          res->emplace_back(                                         ) ;
					}
				}
			}
		Return :
			return res ;
		}

	#endif
}
