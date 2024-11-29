// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

#include "store/store_utils.hh"
#include "store/struct.hh"
#include "store/alloc.hh"
#include "store/vector.hh"
#include "store/prefix.hh"
#include "store/side_car.hh"

//
// Idxed
//

template<class I,uint8_t NGuardBits_=0> struct Idxed {
	static constexpr bool IsIdxed = true ;
	//
	using Idx = I ;
	static constexpr uint8_t NGuardBits = NGuardBits_             ;
	static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
	// statics
private :
	static constexpr void _s_chk(Idx idx) { swear_prod( !(idx&~lsb_msk(NValBits)) , "index overflow" ) ; }
	// cxtors & casts
public :
	constexpr Idxed(     ) = default ;
	constexpr Idxed(Idx i) : _idx{i} { _s_chk(i) ; } // ensure no index overflow
	//
	constexpr Idx operator+() const { return _idx&lsb_msk(NValBits) ; }
	//
	void clear() { self = Idxed{} ; }
	// accesses
	constexpr bool              operator== (Idxed other) const { return +self== +other ; }
	constexpr ::strong_ordering operator<=>(Idxed other) const { return +self<=>+other ; }
	//
	template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) Idx  side(       ) const { return Idx(_idx>>(LSB+NValBits))&lsb_msk<Idx>(W) ; }
	template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) void side(Idx val)       {
		_idx =
			Idx( _idx & ~(     lsb_msk<Idx>(W) <<(LSB+NValBits)) )
		|	Idx(          (val&lsb_msk<Idx>(W))<<(LSB+NValBits)  )
		;
	}
	// data
private :
	Idx _idx = 0 ;
} ;
template<class T> concept IsIdxed = T::IsIdxed && sizeof(T)==sizeof(typename T::Idx) ;
template<IsIdxed I> ::string& operator+=( ::string& os , I const i ) { return os<<+i ; }

namespace std {
	template<IsIdxed I> struct hash<I> { size_t operator()(I i) const { return +i ; } } ;
}

//
// Idxed2
//

template<IsIdxed A_,IsIdxed B_> requires(!::is_same_v<A_,B_>) struct Idxed2 {
	static constexpr bool IsIdxed2 = true ;
	//
	using A    = A_                                           ;
	using B    = B_                                           ;
	using Idx  = Largest< typename A::Idx , typename B::Idx > ;
	using SIdx = ::make_signed_t<Idx>                         ;
	static constexpr uint8_t NValBits   = ::max(A::NValBits,B::NValBits)+1 ; static_assert(NValBits<=NBits<Idx>) ;
	static constexpr uint8_t NGuardBits = NBits<Idx>-NValBits              ;
	//
	template<class T> static constexpr bool IsA    = ::is_base_of_v<A,T> && ( ::is_base_of_v<B,A> || !::is_base_of_v<B,T> ) ; // ensure T does not derive independently from both A & B
	template<class T> static constexpr bool IsB    = ::is_base_of_v<B,T> && ( ::is_base_of_v<A,B> || !::is_base_of_v<A,T> ) ; // .
	template<class T> static constexpr bool IsAOrB = IsA<T> || IsB<T> ;
	//
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
	template<class T> requires( IsA<T> && sizeof(T)==sizeof(Idx) ) explicit operator T const&() const { SWEAR(is_a<T>()) ; return reinterpret_cast<T const&>(self) ; }
	template<class T> requires( IsA<T> && sizeof(T)==sizeof(Idx) ) explicit operator T      &()       { SWEAR(is_a<T>()) ; return reinterpret_cast<T      &>(self) ; }
	//
	void clear() { self = Idxed2() ; }
	// accesses
	template<class T> requires(IsAOrB<T>) bool is_a() const {
		if (IsA<T>) return !(  _val & (SIdx(1)<<SIdx(NValBits-1)) ) ;
		else        return !( -_val & (SIdx(1)<<SIdx(NValBits-1)) ) ;
	}
	//
	SIdx operator+() const { return _val<<NGuardBits>>NGuardBits ; }
	//
	bool              operator== (Idxed2 other) const { return +self== +other ; }
	::strong_ordering operator<=>(Idxed2 other) const { return +self<=>+other ; }
	//
	template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) Idx  side(       ) const { return (_val>>(LSB+NValBits))&lsb_msk<SIdx>(W) ; }
	template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB+NValBits<=NBits<Idx> ) void side(Idx val)       {
		_val =
			( _val & ~SIdx(     lsb_msk<Idx>(W) <<(LSB+NValBits)) )
		|	          SIdx((val&lsb_msk<Idx>(W))<<(LSB+NValBits))
		;
	}
private :
	// data
	SIdx _val = 0 ;
} ;
template<class T> concept IsIdxed2 = T::IsIdxed2 && sizeof(T)==sizeof(typename T::Idx) ;
template<IsIdxed2 I2> ::string& operator+=( ::string& os , I2 const i2 ) {
	using A = typename I2::A ;
	using B = typename I2::B ;
	if      (!i2                  ) return os << '0'   ;
	else if (i2.template is_a<A>()) return os << A(i2) ;
	else                            return os << B(i2) ;
}

// must be outside Engine namespace as it specializes std::hash
namespace std {
	template<IsIdxed2 TU> struct hash<TU> { size_t operator()(TU tu) const { return +tu ; } } ;
}

//
// vectors
//

namespace Vector {

	template<class T> struct File ;

	template<class Idx_,class Item_,class Mrkr_=void,uint8_t NGuardBits=0> struct SimpleBase ;
	template<class Idx_,class Item_,class Mrkr_=void,uint8_t NGuardBits=1> struct CrunchBase ;

	template<class V> struct Generic ;

	template<class Idx_,class Item_,class Mrkr_=void> using Simple = Generic<SimpleBase<Idx_,Item_,Mrkr_>> ;
	template<class Idx_,class Item_,class Mrkr_=void> using Crunch = Generic<CrunchBase<Idx_,Item_,Mrkr_>> ;

	//
	// SimpleBase
	//

	template<class Idx_,class Item_,class Mrkr_,uint8_t NGuardBits> struct SimpleBase
	:	             Idxed<Idx_,NGuardBits>
	{	using Base = Idxed<Idx_,NGuardBits> ;
		using Idx  = Idx_                        ;
		using Item = Item_                       ;
		using Mrkr = Mrkr_                       ;
		using Sz   = Idx                         ;
		using F    = File<Simple<Idx,Item,Mrkr>> ;
		static const Idx EmptyIdx ;
		// cxtors & casts
		using Base::Base ;
		//
		template<::convertible_to<Item> I> SimpleBase ( NewType , I        const& x ) : SimpleBase{::span<I const>(&x,1)} {} // New to disambiguate with cxtor from index defined in Base
		template<::convertible_to<Item> I> SimpleBase (          ::span<I> const& v ) : Base{F::file.emplace(v)}          {}
		template<::convertible_to<Item> I> void assign(          ::span<I> const& v ) { self = F::file.assign(+self,v) ; }
		//
		void pop   () { F::file.pop(+self) ; forget() ; }
		void clear () { pop() ;                         }
		void forget() { Base::clear() ;                 }
		// accesses
		Sz          size () const { return ::constify(F::file).size (+self) ; }
		Item const* items() const { return ::constify(F::file).items(+self) ; }
		Item      * items()       { return            F::file .items(+self) ; }
		// services
		void shorten_by(Sz by) { self = F::file.shorten_by(+self,by) ; }
		//
		template<::convertible_to<Item> I> void append(::span<I> const& v) { self = F::file.append(+self,v ) ; }
	} ;
	template<class Idx,class Item,class Mrkr,uint8_t NGuardBits> constexpr Idx SimpleBase<Idx,Item,Mrkr,NGuardBits>::EmptyIdx = ::constify(F::file).EmptyIdx ;

	//
	// CrunchBase
	//

	// Crunch's are like Simple's except that a vector of 0 element is simply 0 and a vector of 1 element is stored in place
	// This is particular efficient for situations where the vector size is 1 most of the time
	template<class Idx_,class Item_,class Mrkr_,uint8_t NGuardBits> struct CrunchBase
	:	               Idxed2< Item_ , Idxed<Idx_,NGuardBits> >
	{	using Base   = Idxed2< Item_ , Idxed<Idx_,NGuardBits> > ;
		using Item   =         Item_                            ;
		using Vector =                 Idxed<Idx_,NGuardBits>   ;
		using Idx    = Idx_                        ;
		using Mrkr   = Mrkr_                       ;
		using Sz     = Idx                         ;
		using F      = File<Crunch<Idx,Item,Mrkr>> ;
		//
		// cxtors & casts
		using Base::Base ;
		//
		template<IsA<Item>              I> CrunchBase(           I         const& x ) = delete ;
		template<::convertible_to<Item> I> CrunchBase( NewType , I         const& x ) : Base{Item(x)} {}
		template<::convertible_to<Item> I> CrunchBase(           ::span<I> const& v ) {
			if (v.size()!=1) static_cast<Base&>(self) = F::file.emplace(v) ;
			else             static_cast<Base&>(self) = v[0]               ;
		}
		template<::convertible_to<Item> I> void assign(::span<I> const& v) {
			if      (!_multi()  )                       self = CrunchBase(v)          ;
			else if (v.size()!=1)                       self = F::file.assign(self,v) ;
			else                  { F::file.pop(self) ; self = CrunchBase(New,v[0])   ; }
		}
		//
		void pop   () { if (_multi()) F::file.pop(self) ; forget     () ; }
		void clear () {                                   pop        () ; }
		void forget() {                                   Base::clear() ; }
		// accesses
		auto        size () const -> Sz { if (_single()) return 1                               ; else return            F::file .size (self) ; }
		Item const* items() const       { if (_single()) return &static_cast<Item const&>(self) ; else return ::constify(F::file).items(self) ; }
		Item      * items()             { if (_single()) return &static_cast<Item      &>(self) ; else return            F::file .items(self) ; }
	private :
		bool _multi () const { return !self.template is_a<Item  >() ; } // 0 is both a Vector and an Item, so this way 0 is !_multi ()
		bool _single() const { return !self.template is_a<Vector>() ; } // 0 is both a Vector and an Item, so this way 0 is !_single()
		// services
	public :
		void shorten_by(Sz by) {
			Sz sz = size() ;
			SWEAR( by<=sz , by , sz ) ;
			if      (!_multi()) { if (by==sz) forget() ;                                          }
			else if (by!=sz-1 )   self = F::file.shorten_by( self , by ) ;
			else                { Item save = self[0] ; F::file.pop(Vector(self)) ; self = save ; }
		}
		//
		template<::convertible_to<Item> I> void append(::span<I> const& v) {
			if      (!self  ) assign(v) ;
			else if (_multi()) self = F::file.append (     self ,v) ;
			else if (+v      ) self = F::file.emplace(Item(self),v) ;
		}
	} ;

	//
	// Generic
	//

	template<class V> ::string& operator+=( ::string& , Generic<V> const& ) ;
	template<class V> struct Generic : V {
		friend ::string& operator+= <>( ::string& , Generic const& ) ;
		using Base           = V                   ;
		using Idx            = typename Base::Idx  ;
		using Item           = typename Base::Item ;
		using value_type     = Item                ;                          // mimic vector
		using iterator       = Item      *         ;                          // .
		using const_iterator = Item const*         ;                          // .
		static constexpr bool IsStr = IsChar<Item> ;
		//
		using Base::items ;
		using Base::size  ;
		// cxtors & casts
		using Base::Base ;
		//
		template<::convertible_to<Item> I> requires( ::is_const_v<I>) Generic(::vector           <::remove_const_t<I>> const& v) : Base{::span<I const>(v)} {}
		template<::convertible_to<Item> I> requires(!::is_const_v<I>) Generic(::vector           <                 I > const& v) : Base{::span<I const>(v)} {}
		template<::convertible_to<Item> I> requires(IsStr           ) Generic(::basic_string_view<                 I > const& s) : Base{::span<I const>(s)} {}
		template<::convertible_to<Item> I> requires(IsStr           ) Generic(::basic_string     <                 I > const& s) : Base{::span<I const>(s)} {}
		//
		template<::convertible_to<Item> I>                            void assign(::span             <                 I > const& v) { Base::assign(                v ) ; }
		template<::convertible_to<Item> I> requires( ::is_const_v<I>) void assign(::vector           <::remove_const_t<I>> const& v) { Base::assign(::span<I const>(v)) ; }
		template<::convertible_to<Item> I> requires(!::is_const_v<I>) void assign(::vector           <                 I > const& v) { Base::assign(::span<I const>(v)) ; }
		template<::convertible_to<Item> I> requires(IsStr           ) void assign(::basic_string_view<                 I > const& s) { Base::assign(::span<I const>(s)) ; }
		template<::convertible_to<Item> I> requires(IsStr           ) void assign(::basic_string     <                 I > const& s) { Base::assign(::span<I const>(s)) ; }
		//
		operator ::span             <Item const>() const                 { return view    () ; }
		operator ::span             <Item      >()                       { return view    () ; }
		operator ::basic_string_view<Item      >() const requires(IsStr) { return str_view() ; }
		// accesses
		::span      <Item const> view    () const                 { return { items() , size() } ; }
		::span      <Item      > view    ()                       { return { items() , size() } ; }
		::basic_string_view<Item      > str_view() const requires(IsStr) { return { items() , size() } ; }
		//
		Item const* begin     (        ) const { return items()           ; } // mimic vector
		Item      * begin     (        )       { return items()           ; } // .
		Item const* cbegin    (        ) const { return items()           ; } // .
		Item const* end       (        ) const { return items()+size()    ; } // .
		Item      * end       (        )       { return items()+size()    ; } // .
		Item const* cend      (        ) const { return items()+size()    ; } // .
		Item const& front     (        ) const { return items()[0       ] ; } // .
		Item      & front     (        )       { return items()[0       ] ; } // .
		Item const& back      (        ) const { return items()[size()-1] ; } // .
		Item      & back      (        )       { return items()[size()-1] ; } // .
		Item const& operator[](size_t i) const { return items()[i       ] ; } // .
		Item      & operator[](size_t i)       { return items()[i       ] ; } // .
		//
		::span             <Item const> const subvec( size_t start , size_t sz=Npos ) const { return ::span<Item const> ( begin()+start , ::min(sz,size()-start) ) ; }
		::span             <Item      >       subvec( size_t start , size_t sz=Npos )       { return ::span<Item      > ( begin()+start , ::min(sz,size()-start) ) ; }
		::basic_string_view<Item      > const substr( size_t start , size_t sz=Npos ) const { return ::basic_string_view( begin()+start , ::min(sz,size()-start) ) ; }
		::basic_string_view<Item      >       substr( size_t start , size_t sz=Npos )       { return ::basic_string_view( begin()+start , ::min(sz,size()-start) ) ; }
		// services
		template<::convertible_to<Item> I> void append(::span             <I> const& v) { Base::append(                       v ) ; }
		template<::convertible_to<Item> I> void append(::vector           <I> const& v) {       append(::span<I const>(v)) ; }
		template<::convertible_to<Item> I> void append(::basic_string_view<I> const& s) {       append(::span<I const>(s)) ; }
		template<::convertible_to<Item> I> void append(::basic_string     <I> const& s) {       append(::span<I const>(s)) ; }
	} ;
	template<class V> ::string& operator+=( ::string& os , Generic<V> const& gv ) {
		bool first = true ;
		/**/                                                                  os <<'[' ;
		for( typename V::Item const& x : gv ) { if (first) first=false ; else os <<',' ; os << x ; }
		return                                                                os <<']' ;
	}

}
