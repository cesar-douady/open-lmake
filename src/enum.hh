// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "basic_utils.hh"

//
// StdEnum
//

// our enum's are scoped enum (is_scoped_enum is only available in C++23) and made from uint8_t
template<class E> concept StdEnum = ::is_enum_v<E> && !::convertible_to<E,::underlying_type_t<E>> && ::is_same_v<::underlying_type_t<E>,uint8_t> ;

namespace EnumHelper {

	template<StdEnum E> using EnumUint = ::underlying_type_t<E>       ;
	template<StdEnum E> using EnumInt  = ::make_signed_t<EnumUint<E>> ;

	// return X's name from pretty func name
	// func_name looks like 'constexpr auto func() [X = <X's name>]'      with clang
	// and             like 'constexpr auto func() [with X = <X's name>]' with gcc
	inline constexpr ::string_view extract_enum_name(::string_view func_name) {
		size_t                        start  = func_name.find ('['      )+1 ;
		/**/                          start  = func_name.find ('=',start)+1 ;
		while (func_name[start]==' ') start += 1                            ;
		size_t                        end    = func_name.rfind(']'      )   ;
		while (func_name[end-1]==' ') end   -= 1                            ;
		return ::string_view( &func_name[start] , end-start ) ;
	}

	inline constexpr ::string_view extract_enum_val_name(::string_view func_name) {
		::string_view name = extract_enum_name(func_name) ;
		size_t        pos  = name.find(':') ;
		if (pos==Npos) return ::string_view() ;
		while (name[pos]==':') pos++ ;
		return name.substr(pos) ;
	}

	template<StdEnum E> constexpr auto enum_type_name_view() { return extract_enum_name    (__PRETTY_FUNCTION__) ; } // must return auto to simplify __PRETTY_FUNCTION__ analysis with gcc
	template<auto    V> constexpr auto enum_val_name_view () { return extract_enum_val_name(__PRETTY_FUNCTION__) ; } // .

	template<auto V> constexpr ::string_view EnumValNameView = enum_val_name_view<V>() ;

	template<auto V,bool Snake> constexpr size_t enum_val_name_sz() {
		size_t res = EnumValNameView<V>.size() ;
		if (Snake) {
			bool first = true ;
			for( char c : EnumValNameView<V> )
				if      ( first            ) first = false ;
				else if ( 'A'<=c && c<='Z' ) res++ ;         // snake case prepends a _ before each upper case, except first
		}
		return res ;
	}

	template<auto V,bool Snake> constexpr size_t EnumValNameSz = enum_val_name_sz<V,Snake>() ;

	template<auto V,bool Snake> constexpr ::array<char,EnumValNameSz<V,Snake>> enum_val_name() {
		::array<char,EnumValNameSz<V,Snake>> res  ;
		size_t                               j    = 0 ;
		for( char c : EnumValNameView<V> )
			if ( Snake && 'A'<=c && c<='Z' ) { { if (j!=0) res[j++] = '_' ; } res[j++] = c+'a'-'A' ; }
			else                                                              res[j++] = c         ;
		return res ;
	}

	template<auto V,bool Snake> constexpr ::array<char,EnumValNameSz<V,Snake>> EnumValName = enum_val_name<V,Snake>() ;

	// search by dichotomy, assuming E(Start) has a value and E(Start+Cnt) does not
	template<StdEnum E,size_t Start=0,size_t Cnt=size_t(1)<<NBits<::underlying_type_t<E>>> constexpr size_t search_enum_sz() {
		if constexpr (Cnt==1) return Start+1 ;
		//
		constexpr size_t CntLeft  = Cnt/2           ;
		constexpr size_t CntRight = Cnt   - CntLeft ;
		constexpr size_t Mid      = Start + CntLeft ;
		if constexpr (+EnumValNameView<E(Mid)>) return search_enum_sz<E,Mid  ,CntRight>() ;
		else                                    return search_enum_sz<E,Start,CntLeft >() ;
	}

	template<StdEnum E> constexpr size_t N   = search_enum_sz<E>() ;
	template<StdEnum E> constexpr E      All = E(N<E>)             ;

	template<StdEnum E,bool Snake,size_t Start=0,size_t Cnt=N<E>> constexpr ::array<::string_view,Cnt> enum_names() {
		::array<::string_view,Cnt> res ;
		if constexpr (Cnt==1) {
			res[0] = { &EnumValName<E(Start),Snake>[0] , EnumValNameSz<E(Start),Snake> } ;
		} else {
			constexpr size_t CntLeft  = Cnt/2           ;
			constexpr size_t CntRight = Cnt   - CntLeft ;
			constexpr size_t Mid      = Start + CntLeft ;
			::array<::string_view,CntLeft > left  = enum_names<E,Snake,Start,CntLeft >() ;
			::array<::string_view,CntRight> right = enum_names<E,Snake,Mid  ,CntRight>() ;
			for( size_t i : iota(CntLeft ) ) res[        i] = left [i] ;
			for( size_t i : iota(CntRight) ) res[CntLeft+i] = right[i] ;
		}
		return res ;
	}

	template<StdEnum E           > constexpr ::string_view               EnumName  = enum_type_name_view<E>() ;
	template<StdEnum E,bool Snake> constexpr ::array<::string_view,N<E>> EnumNames = enum_names<E,Snake>()    ;

	template<StdEnum E> ::umap_s<E> mk_enum_tab() {
		::umap_s<E> res ;
		for( E e : iota(All<E>) ) {
			res[camel_str(e)] = e ;
			res[snake_str(e)] = e ;
		}
		return res ;
	}
	template<StdEnum E> ::pair<E,bool/*ok*/> mk_enum(::string const& x) {
		static ::umap_s<E> const s_tab = mk_enum_tab<E>() ;
		auto it = s_tab.find(x) ; //!           ok
		if (it==s_tab.end()) return {{}        ,false} ;
		else                 return {it->second,true } ;
	}

}

//
// user interface
//

template<StdEnum E> constexpr size_t N   = EnumHelper::N  <E> ;
template<StdEnum E> constexpr E      All = EnumHelper::All<E> ;

//                                                                                                Snake
template<StdEnum E> inline ::string_view camel    (E e) { return          EnumHelper::EnumNames<E,false>[+e]  ; }
template<StdEnum E> inline ::string_view snake    (E e) { return          EnumHelper::EnumNames<E,true >[+e]  ; }
template<StdEnum E> inline ::string      camel_str(E e) { return ::string(EnumHelper::EnumNames<E,false>[+e]) ; }
template<StdEnum E> inline ::string      snake_str(E e) { return ::string(EnumHelper::EnumNames<E,true >[+e]) ; }

template<StdEnum E> static constexpr size_t NBits<E> = n_bits(N<E>) ;

template<StdEnum E> inline ::string  operator+ ( ::string     && s , E               e ) { return ::move(s)+snake(e)                          ; }
template<StdEnum E> inline ::string  operator+ ( ::string const& s , E               e ) { return        s +snake(e)                          ; }
template<StdEnum E> inline ::string  operator+ ( E               e , ::string const& s ) { return snake (e)+      s                           ; }
template<StdEnum E> inline ::string& operator+=( ::string      & s , E               e ) { return e<All<E> ? s<<snake(e) : s<<"N+"<<(+e-N<E>) ; }

template<StdEnum E> inline bool can_mk_enum(::string const& x) {
	return EnumHelper::mk_enum<E>(x).second ;
}

template<StdEnum E> inline E mk_enum(::string const& x) {
	::pair<E,bool/*ok*/> res = EnumHelper::mk_enum<E>(x) ;
	throw_unless( res.second , "cannot make enum ",EnumHelper::EnumName<E>," from ",x ) ;
	return res.first ;
}

template<StdEnum E> inline constexpr EnumHelper::EnumUint<E> operator+(E e) { return EnumHelper::EnumUint<E>(e) ; }
//
template<StdEnum E> inline constexpr E                      operator+ (E  e,EnumHelper::EnumInt<E> i) {                            e = E(+e+ i) ; return e  ; }
template<StdEnum E> inline constexpr E&                     operator+=(E& e,EnumHelper::EnumInt<E> i) {                            e = E(+e+ i) ; return e  ; }
template<StdEnum E> inline constexpr E                      operator- (E  e,EnumHelper::EnumInt<E> i) {                            e = E(+e- i) ; return e  ; }
template<StdEnum E> inline constexpr EnumHelper::EnumInt<E> operator- (E  e,E                      o) { EnumHelper::EnumInt<E> d ; d =   +e-+o  ; return d  ; }
template<StdEnum E> inline constexpr E&                     operator-=(E& e,EnumHelper::EnumInt<E> i) {                            e = E(+e- i) ; return e  ; }
template<StdEnum E> inline constexpr E                      operator++(E& e                         ) {                            e = E(+e+ 1) ; return e  ; }
template<StdEnum E> inline constexpr E                      operator++(E& e,int                     ) { E e_ = e ;                 e = E(+e+ 1) ; return e_ ; }
template<StdEnum E> inline constexpr E                      operator--(E& e                         ) {                            e = E(+e- 1) ; return e  ; }
template<StdEnum E> inline constexpr E                      operator--(E& e,int                     ) { E e_ = e ;                 e = E(+e- 1) ; return e_ ; }
//
template<StdEnum E> inline constexpr E  operator& (E  e,E o) {           return ::min(e,o) ; }
template<StdEnum E> inline constexpr E  operator| (E  e,E o) {           return ::max(e,o) ; }
template<StdEnum E> inline constexpr E& operator&=(E& e,E o) { e = e&o ; return e          ; }
template<StdEnum E> inline constexpr E& operator|=(E& e,E o) { e = e|o ; return e          ; }
//
template<StdEnum E> inline E    decode_enum( const char* p ) { return E(decode_int<EnumHelper::EnumUint<E>>(p)) ; }
template<StdEnum E> inline void encode_enum( char* p , E e ) { encode_int(p,+e) ;                                 }

//
// BitMap
//

template<StdEnum E> struct BitMap {
	template<class> friend ::string& operator+=( ::string& , BitMap const ) ;
	using Elem =       E    ;
	using Val  = Uint<N<E>> ;
	// cxtors & casts
	BitMap() = default ;
	//
	constexpr explicit BitMap(Val v) : _val{v} {}
	//
	template<Same<E>... Args> constexpr BitMap(Args... e) {
		[[maybe_unused]] bool _[] = { (_val|=(1<<+e),false)... } ;
	}
	// accesses
	constexpr Val operator+() const { return _val ; }
	// services
	constexpr bool    operator==( BitMap const&     ) const = default ;
	constexpr bool    operator<=( BitMap other      ) const { return !(  _val & ~other._val )    ;                 }
	constexpr bool    operator>=( BitMap other      ) const { return !( ~_val &  other._val )    ;                 }
	constexpr BitMap  operator~ (                   ) const { return BitMap(lsb_msk(N<E>)&~_val) ;                 }
	constexpr BitMap  operator& ( BitMap other      ) const { return BitMap(_val&other._val)     ;                 }
	constexpr BitMap  operator| ( BitMap other      ) const { return BitMap(_val|other._val)     ;                 }
	constexpr BitMap& operator&=( BitMap other      )       { self = self&other ; return self    ;                 }
	constexpr BitMap& operator|=( BitMap other      )       { self = self|other ; return self    ;                 }
	constexpr bool    operator[]( E      bit_       ) const { return (_val>>+bit_) & Val(1)      ;                 }
	constexpr uint8_t popcount  (                   ) const { return ::popcount(_val)            ;                 }
	constexpr void    set       ( E flag , bool val )       { if (val) self |= flag ; else self &= ~BitMap(flag) ; } // operator~(E) is not always recognized because of namespace's
	// data
private :
	Val _val = 0 ;
} ;
//
template<StdEnum E> inline constexpr BitMap<E> operator~(E e) { return ~BitMap<E>(e)  ; }

template<StdEnum E>inline  BitMap<E> mk_bitmap( ::string const& x , char sep=',' ) {
	BitMap<E> res ;
	for( ::string const& s : split(x,sep) ) res |= mk_enum<E>(s) ;
	return res ;
}

template<StdEnum E> inline ::string& operator+=( ::string& os , BitMap<E> const bm ) {
	os <<'(' ;
	bool first = true ;
	for( E e : iota(All<E>) ) if (bm[e]) {
		if (first) { os <<      e ; first = false ; }
		else       { os <<'|'<< e ;                 }
	}
	return os <<')' ;
}

// used in static_assert when defining a table indexed by enum to fire if enum updates are not propagated to tab def
template<StdEnum E,class T> inline constexpr bool/*ok*/ chk_enum_tab(::amap<E,T,N<E>> tab) { // START_OF_NO_COV meant for compile time
	for( E e : iota(All<E>) ) if (tab[+e].first!=e) return false/*ok*/ ;
	/**/                                            return true /*ok*/ ;
}                                                                                            // END_OF_NO_COV

