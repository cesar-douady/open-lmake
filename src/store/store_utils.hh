// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

namespace Store {

	template<size_t NB> using Uint = ::conditional_t< NB<=8 , uint8_t , ::conditional_t< NB<=16 , uint16_t , ::conditional_t< NB<=32 , uint32_t , ::conditional_t< NB<=64 , uint64_t , void > > > > ;

	template<class Item> concept IsTrivial = ::is_trivially_copyable_v<Item>                      ;
	template<class Item> concept IsChar    = ::is_trivial_v<Item> && ::is_standard_layout_v<Item> ;
	template<class Item> using AsChar = ::conditional_t<IsChar<Item>,Item,conditional_t<sizeof(Item)==1,char,Uint<sizeof(Item)*8>>> ; // for use when Item is not yet known to be a Char

	template<class T>                              struct NGuardBitsHelper    { static constexpr uint8_t NGuardBits = T::NGuardBits ; } ;
	template<class T> requires(::is_integral_v<T>) struct NGuardBitsHelper<T> { static constexpr uint8_t NGuardBits = 0             ; } ;
	template<class T> static constexpr uint8_t NGuardBits = NGuardBitsHelper<T>::NGuardBits ;
	template<class T> static constexpr uint8_t NValBits   = NBits<T>-NGuardBits<T>          ;

	template<class D> concept HasDataSz = requires(D d) { { d.n_items()   }->::convertible_to<size_t> ; } ;
	template<class I> concept IsIdx     = requires(I i) { { +I{size_t(0)} }->::convertible_to<size_t> ; } ;
	template<class I> using IntIdx = ::make_unsigned_t< ::conditional_t< ::is_integral_v<I> , I , decltype(+I{}) > > ;

	template<class M> struct NoLock {
		NoLock(M&) {}
	} ;

}
