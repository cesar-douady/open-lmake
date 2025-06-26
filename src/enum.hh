// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "basic_utils.hh"

template<class E> concept Enum  = ::is_enum_v<E>                                     ;
template<class E> concept UEnum = Enum<E> && ::is_unsigned_v<::underlying_type_t<E>> ;

namespace EnumHelper {

	template<Enum E> using EnumInt  = ::underlying_type_t<E>        ;
	template<Enum E> using EnumUInt = ::make_unsigned_t<EnumInt<E>> ;
	template<Enum E> using EnumSInt = ::make_signed_t  <EnumInt<E>> ;

	// use __PRETTY_FUNCTION__ as a way to reach reflexion about enum names

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
		size_t        pos  = name.find(':')               ; // value is either EnumType::EnumVal if define, or (EnumType)123 if it's not
		if (pos==Npos) return ::string_view() ;             // value is not defined
		pos++ ;
		if (name[pos]!=':') throw 0 ;                       // value is defined, if we see a :, there must be a second one following it
		pos++ ;
		return name.substr(pos) ;
	}

	template<Enum E>                             constexpr auto enum_type_name_view() { return extract_enum_name    (__PRETTY_FUNCTION__) ; } // return auto to simplify __PRETTY_FUNCTION__ ...
	template<auto V> requires(Enum<decltype(V)>) constexpr auto enum_val_name_view () { return extract_enum_val_name(__PRETTY_FUNCTION__) ; } // ... analysis with gcc

	template<auto V> constexpr ::string_view EnumValNameView = enum_val_name_view<V>() ;

	// computes the length of the name
	template<auto V,bool Snake> constexpr size_t EnumValNameSz = []() {
		size_t res = EnumValNameView<V>.size() ;
		if (Snake) {
			bool first = true ;
			for( char c : EnumValNameView<V> )
				if      ( first            ) first = false ;
				else if ( 'A'<=c && c<='Z' ) res++ ;         // snake case prepends a _ before each upper case, except first
		}
		return res ;
	}() ;

	template<auto V,bool Snake> constexpr ::array<char,EnumValNameSz<V,Snake>> EnumValName = []() {
		::array<char,EnumValNameSz<V,Snake>> res ;
		size_t                               j   = 0 ;
		for( char c : EnumValNameView<V> )
			if ( Snake && 'A'<=c && c<='Z' ) { { if (j!=0) res[j++] = '_' ; } res[j++] = c+'a'-'A' ; }
			else                                                              res[j++] = c         ;
		return res ;
	}() ;

	// XXX? : support signed enum's if necessary

	// search by dichotomy, assuming E(Start) has a value and E(Start+Cnt) does not
	template<UEnum E,size_t Start=0,size_t Cnt=::min(size_t(Max<EnumInt<E>>)+1,Max<size_t>)> constexpr size_t search_enum_sz() { // ensure we resist to size_t based enums
		if constexpr (Cnt==1) return Start+1 ;
		//
		constexpr size_t CntLeft  = Cnt/2         ;
		constexpr size_t CntRight = Cnt - CntLeft ;
		if constexpr (+EnumValNameView<E(Start+CntLeft)>) return search_enum_sz<E,Start+CntLeft,CntRight>() ;
		else                                              return search_enum_sz<E,Start        ,CntLeft >() ;
	}

	template<UEnum E> constexpr size_t N   = search_enum_sz<E>() ;
	template<UEnum E> constexpr E      All = E(N<E>)             ;

	// recursive implementation is much simpler than loop based code
	template<UEnum E,bool Snake,size_t Start=0,size_t Cnt=N<E>> constexpr ::array<::string_view,Cnt> enum_names() {
		::array<::string_view,Cnt> res ;
		if constexpr (Cnt==1) {
			res[0] = { &EnumValName<E(Start),Snake>[0] , EnumValNameSz<E(Start),Snake> } ;
		} else {
			constexpr size_t CntLeft  = Cnt/2         ;
			constexpr size_t CntRight = Cnt - CntLeft ;
			::array<::string_view,CntLeft > left  = enum_names<E,Snake,Start        ,CntLeft >() ;
			::array<::string_view,CntRight> right = enum_names<E,Snake,Start+CntLeft,CntRight>() ;
			for( size_t i : iota(CntLeft ) ) res[        i] = left [i] ;
			for( size_t i : iota(CntRight) ) res[CntLeft+i] = right[i] ;
		}
		return res ;
	}

	template<Enum E           > constexpr ::string_view               EnumName  = enum_type_name_view<E>() ;
	template<Enum E,bool Snake> constexpr ::array<::string_view,N<E>> EnumNames = enum_names<E,Snake>()    ;

	template<UEnum E> ::pair<E,bool/*ok*/> mk_enum(::string const& x) {
		static ::umap_s<E> const s_tab = []() {
			::umap_s<E> res ;
			for( E e : iota(All<E>) ) {
				res[camel_str(e)] = e ;
				res[snake_str(e)] = e ;
			}
			return res ;
		}() ;
		//
		auto it = s_tab.find(x) ; //!           ok
		if (it==s_tab.end()) return {{}        ,false} ;
		else                 return {it->second,true } ;
	}

}

//
// user interface
//

using EnumHelper::N   ;
using EnumHelper::All ;

//                                                                                             Snake
template<Enum E> inline ::string_view camel    (E e) { return          EnumHelper::EnumNames<E,false>[+e]  ; }
template<Enum E> inline ::string_view snake    (E e) { return          EnumHelper::EnumNames<E,true >[+e]  ; }
template<Enum E> inline ::string      camel_str(E e) { return ::string(EnumHelper::EnumNames<E,false>[+e]) ; }
template<Enum E> inline ::string      snake_str(E e) { return ::string(EnumHelper::EnumNames<E,true >[+e]) ; }

template<UEnum E> static constexpr size_t NBits<E> = n_bits(N<E>) ;

template<Enum E> inline ::string  operator+ ( ::string     && s , E               e ) { return ::move(s)+snake(e)                          ; }
template<Enum E> inline ::string  operator+ ( ::string const& s , E               e ) { return        s +snake(e)                          ; }
template<Enum E> inline ::string  operator+ ( E               e , ::string const& s ) { return snake (e)+      s                           ; }
template<Enum E> inline ::string& operator+=( ::string      & s , E               e ) { return e<All<E> ? s<<snake(e) : s<<"N+"<<(+e-N<E>) ; }

template<UEnum E> inline bool can_mk_enum(::string const& x) {
	return EnumHelper::mk_enum<E>(x).second ;
}

template<UEnum E> inline E mk_enum(::string const& x) {
	::pair<E,bool/*ok*/> res_ok = EnumHelper::mk_enum<E>(x) ;
	throw_unless( res_ok.second , "cannot make enum ",EnumHelper::EnumName<E>," from ",x ) ;
	return res_ok.first ;
}

template<Enum E> inline constexpr EnumHelper::EnumInt <E> operator+ (E  e                          ) {                       return EnumHelper::EnumInt<E>(e    ) ; }
template<Enum E> inline constexpr E                       operator+ (E  e,EnumHelper::EnumSInt<E> i) {                       return E(+e+i)                       ; }
template<Enum E> inline constexpr E&                      operator+=(E& e,EnumHelper::EnumSInt<E> i) {             e = e+i ; return e                             ; }
template<Enum E> inline constexpr E                       operator- (E  e,EnumHelper::EnumSInt<E> i) {                       return E(+e-i)                       ; }
template<Enum E> inline constexpr EnumHelper::EnumSInt<E> operator- (E  e,E                       o) {                       return EnumHelper::EnumInt<E>(+e-+o) ; }
template<Enum E> inline constexpr E&                      operator-=(E& e,EnumHelper::EnumSInt<E> i) {             e = e-i ; return e                             ; }
template<Enum E> inline constexpr E                       operator++(E& e                          ) {             e = e+1 ; return e                             ; }
template<Enum E> inline constexpr E                       operator++(E& e,int                      ) { E e_ = e ;  e = e+1 ; return e_                            ; }
template<Enum E> inline constexpr E                       operator--(E& e                          ) {             e = e-1 ; return e                             ; }
template<Enum E> inline constexpr E                       operator--(E& e,int                      ) { E e_ = e ;  e = e-1 ; return e_                            ; }
//
template<Enum E> inline E    decode_enum( const char* p ) { return E(decode_int<EnumHelper::EnumInt<E>>(p)) ; }
template<Enum E> inline void encode_enum( char* p , E e ) { encode_int(p,+e) ;                                }

//
// BitMap
//

template<UEnum E> struct BitMap {
	template<class> friend ::string& operator+=( ::string& , BitMap const ) ;
	using Elem =       E    ;
	using Val  = Uint<N<E>> ;
	// cxtors & casts
	BitMap() = default ;
	//
	constexpr explicit BitMap(Val v) : _val{v} {}
	//
	template<Same<E>... Args> constexpr BitMap(Args... e) {
		((_val|=(1<<+e)),...) ;
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
template<UEnum E> inline constexpr BitMap<E> operator~(E e) { return ~BitMap<E>(e)  ; }

template<UEnum E>inline  BitMap<E> mk_bitmap( ::string const& x , char sep=',' ) {
	BitMap<E> res ;
	for( ::string const& s : split(x,sep) ) res |= mk_enum<E>(s) ;
	return res ;
}

template<UEnum E> inline ::string& operator+=( ::string& os , BitMap<E> const bm ) {
	os <<'(' ;
	bool first = true ;
	for( E e : iota(All<E>) ) if (bm[e]) {
		if (first) { os <<      e ; first = false ; }
		else       { os <<'|'<< e ;                 }
	}
	return os <<')' ;
}

// used in static_assert when defining a table indexed by enum to fire if enum updates are not propagated to tab def
template<UEnum E,class T> inline constexpr bool/*ok*/ chk_enum_tab(::amap<E,T,N<E>> tab) { // START_OF_NO_COV meant for compile time
	for( E e : iota(All<E>) ) if (tab[+e].first!=e) return false/*ok*/ ;
	/**/                                            return true /*ok*/ ;
}                                                                                          // END_OF_NO_COV

