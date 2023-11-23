// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

namespace Engine {

	//
	// Idxed
	//

	template<class I,uint8_t NGuardBits_=0> struct Idxed {
		static constexpr bool IsIdxed = true ;
		using Idx = I ;
		static constexpr uint8_t NGuardBits = NGuardBits_             ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		// statics
	private :
		static constexpr void _s_chk(Idx idx) { swear_prod( !(idx&~lsb_msk(NValBits)) , "index overflow" ) ; }
		// cxtors & casts
	public :
		constexpr Idxed(     ) = default ;
		constexpr Idxed(Idx i) : _idx{i} { _s_chk(i) ; }                       // ensure no index overflow
		//
		constexpr Idx  operator+() const { return  _idx&lsb_msk(NValBits) ; }
		constexpr bool operator!() const { return !+*this                 ; }
		//
		void clear() { *this = Idxed{} ; }
		// accesses
		constexpr bool              operator== (Idxed other) const { return +*this== +other ; }
		constexpr ::strong_ordering operator<=>(Idxed other) const { return +*this<=>+other ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) Idx  side(       ) const { return bits(_idx,W,LSB+NValBits    ) ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) void side(Idx val)       { _idx = bits(_idx,W,LSB+NValBits,val) ; }
		// data
	private :
		Idx _idx = 0 ;
	} ;
	template<class T> concept IsIdxed = T::IsIdxed && sizeof(T)==sizeof(typename T::Idx) ;
	template<Engine::IsIdxed I> ::ostream& operator<<( ::ostream& os , I const i ) { return os<<+i ; }

}

namespace std {
	template<Engine::IsIdxed I> struct hash<I> { size_t operator()(I i) const { return +i ; } } ;
}

namespace Engine {

	//
	// Idxed2
	//

	template<IsIdxed A,IsIdxed B> requires(!::is_same_v<A,B>) struct Idxed2 {
		static constexpr bool IsIdxed2 = true ;

		using Idx  = Largest< typename A::Idx , typename B::Idx > ;
		using SIdx = ::make_signed_t<Idx> ;
		static constexpr uint8_t NValBits   = ::max(A::NValBits,B::NValBits)+1 ; static_assert(NValBits<=NBits<Idx>) ;
		static constexpr uint8_t NGuardBits = NBits<Idx>-NValBits              ;

		template<class T> static constexpr bool IsA    = ::is_base_of_v<A,T> && ( ::is_base_of_v<B,A> || !::is_base_of_v<B,T> ) ; // ensure T does not derive independently from both A & B
		template<class T> static constexpr bool IsB    = ::is_base_of_v<B,T> && ( ::is_base_of_v<A,B> || !::is_base_of_v<A,T> ) ; // .
		template<class T> static constexpr bool IsAOrB = IsA<T> || IsB<T> ;

		// cxtors & casts
		constexpr Idxed2(   ) = default ;
		constexpr Idxed2(A a) : _val{SIdx( +a)} {}
		constexpr Idxed2(B b) : _val{SIdx(-+b)} {}
		//
		template<class T> requires(IsAOrB<T>) operator T() const {
			SWEAR(is_a<T>()) ;
			if (IsA<T>) return T(  _val  & lsb_msk(NValBits)) ;
			else        return T((-_val) & lsb_msk(NValBits)) ;
		}
		template<class T> requires( IsA<T> && sizeof(T)==sizeof(Idx) ) explicit operator T const&() const { SWEAR(is_a<T>()) ; return reinterpret_cast<T const&>(*this) ; }
		template<class T> requires( IsA<T> && sizeof(T)==sizeof(Idx) ) explicit operator T      &()       { SWEAR(is_a<T>()) ; return reinterpret_cast<T      &>(*this) ; }
		//
		void clear() { *this = Idxed2() ; }
		// accesses
		template<class T> requires(IsAOrB<T>) bool is_a() const {
			if (IsA<T>) return !bit( _val,NValBits-1) ;
			else        return !bit(-_val,NValBits-1) ;
		}
		//
		SIdx operator+() const { return _val<<NGuardBits>>NGuardBits ; }
		bool operator!() const { return !+*this                      ; }
		//
		bool              operator== (Idxed2 other) const { return +*this== +other ; }
		::strong_ordering operator<=>(Idxed2 other) const { return +*this<=>+other ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) Idx  side(       ) const { return bits(_val,W,NValBits+LSB)     ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) void side(Idx val)       { _val = bits(_val,W,NValBits+LSB,val) ; }
	private :
		// data
		SIdx _val = 0 ;
	} ;
	template<class T> concept IsIdxed2 = T::IsIdxed2 && sizeof(T)==sizeof(typename T::Idx) ;
	template<class A,class B> ::ostream& operator<<( ::ostream& os , Idxed2<A,B> const tu ) {
		if      (!tu                  ) os << '0'   ;
		else if (tu.template is_a<A>()) os << A(tu) ;
		else                            os << B(tu) ;
		return os ;
	}

}

// must be outside Engine namespace as it specializes std::hash
namespace std {
	template<Engine::IsIdxed2 TU> struct hash<TU> { size_t operator()(TU tu) const { return +tu ; } } ;
}
