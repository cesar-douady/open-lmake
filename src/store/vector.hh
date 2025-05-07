// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "alloc.hh"

namespace Store {

	namespace Vector {

		// MinSz is indicative : allocation granularity is based on this size and no hole smaller than this will be generated
		template<class Idx,class Item,class Sz=Idx,size_t MinSz=1> struct ChunkBase {
			using ItemMem = char[sizeof(Item)] ;                                      // define memory for item so that cxtor & dxtor is managed by hand
			// cxtors & casts
			ChunkBase(Sz sz_) : sz{sz_} {}
			// accesses
			Item const* items() const { return ::launder(reinterpret_cast<Item const*>(_items)) ; }
			Item      * items()       { return ::launder(reinterpret_cast<Item      *>(_items)) ; }
			// data
			Sz                    sz               ;
			alignas(Item) ItemMem _items[1][MinSz] ; // [1] is just there to suppress gcc warning about size : gcc handles specially arrays[1] as arrays of indeterminate size
		} ;
		template<class Idx_,class Item_,class Sz_=Idx_,size_t MinSz_=1> struct Chunk
		:	              ChunkBase<Idx_,Item_,Sz_,MinSz_>
		{	using Base  = ChunkBase<Idx_,Item_,Sz_,MinSz_> ;
			using Idx   = Idx_        ;
			using Item  = Item_       ;
			using Sz    = Sz_         ;
			using IdxSz = IntIdx<Idx> ;
			static constexpr size_t MinSz = MinSz_ ;
			//
			static constexpr bool IsTrivial = ::Store::IsTrivial<Item> ;
			using VecView = ::span<Item const> ;
			using Base::sz    ;
			using Base::items ;
			// statics
			static constexpr IdxSz s_n_items(Sz sz_) {
				return div_up<sizeof(Base)>( sizeof(Base) - MinSz*sizeof(Item) + sz_*sizeof(Item) ) ; // /!\ unsigned computation : take care of any subtraction
			}                                                                                         // compute size before we have an object
			// cxtors & casts
			using Base::Base ;
			template<::convertible_to<Item> I> Chunk(::span<I> const& v) : Base{Sz(v.size())} {
				for( Sz i : iota(sz) ) new(items()+i) Item{v[i]} ;
			}
			template<::convertible_to<Item> I0,::convertible_to<Item> I> Chunk( I0 const& x0 , ::span<I> const& v ) : Base{Sz(1+v.size())} {
				new(items()) Item{x0} ;
				for( Sz i : iota(1,sz) ) new(items()+i) Item{v[i-1]} ;
			}
			//
			Chunk(                  VecView const& v ) requires(IsTrivial) : Base{Sz(  v.size())} {                   ::memcpy( items()   , v.data() , v.size()*sizeof(Item) ) ; }
			Chunk( Item const& x0 , VecView const& v ) requires(IsTrivial) : Base{Sz(1+v.size())} { items()[0] = x0 ; ::memcpy( items()+1 , v.data() , v.size()*sizeof(Item) ) ; }
			//
			~Chunk() requires(!IsTrivial) { for( Sz i : iota(sz) ) items()[i].~Item() ; }
			~Chunk() requires( IsTrivial) {                                             }
			//
			operator VecView() const { return VecView(items(),sz) ; }
			// accesses
			IdxSz n_items() const { return s_n_items(sz) ; }                                          // to inform AllocFile of the size of the item
			// services
			void shorten_by(Sz by) {
				SWEAR( by<sz , by , sz ) ;
				for( Sz i : iota(sz-by,sz) ) items()[i].~Item() ;
				sz = sz-by ;
			}
		} ;

	}

	template<bool AutoLock,class Hdr_,class Idx_,uint8_t NIdxBits,class Item_,class Sz=IntIdx<Idx_>,size_t MinSz=1,uint8_t Mantissa=8> struct VectorFile
	:	              AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , NIdxBits , Vector::Chunk<Idx_,Item_,Sz,MinSz> , Mantissa >
	{	using Base  = AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , NIdxBits , Vector::Chunk<Idx_,Item_,Sz,MinSz> , Mantissa > ;
		using Chunk =                                                         Vector::Chunk<Idx_,Item_,Sz,MinSz>              ;
		//
		using Hdr   = Hdr_  ;
		using Idx   = Idx_  ;
		using Item  = Item_ ;
		using IdxSz = typename Chunk::IdxSz ;
		//
		static_assert(MinSz) ;
		//
		static constexpr bool IsStr = IsChar<Item> ;
		//
		using Char    = AsChar<Item>              ;
		using VecView = ::span<Item const>        ;
		using StrView = ::basic_string_view<Char> ;
		using ULock   = UniqueLock<AutoLock>      ;
		using SLock   = SharedLock<AutoLock>      ;
		//
		void at     (Idx   ) = delete ;
		void shorten(Idx,Sz) = delete ;
		//
		using Base::chk_writable ;
		using Base::size         ;
		using Base::_mutex       ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		bool        empty   (Idx idx) const                 {                            return !idx                           ; }
		Sz          size    (Idx idx) const                 { if (!idx) return 0       ; return Base::at(idx).sz               ; }
		Item const* items   (Idx idx) const                 { if (!idx) return nullptr ; return Base::at(idx).items()          ; }
		Item      * items   (Idx idx)                       { if (!idx) return nullptr ; return Base::at(idx).items()          ; }
		Item const* c_items (Idx idx) const                 { if (!idx) return nullptr ; return Base::at(idx).items()          ; }
		VecView     view    (Idx idx) const                 { if (!idx) return {}      ; return Base::at(idx)                  ; }
		StrView     str_view(Idx idx) const requires(IsStr) { VecView res = view(idx) ;  return StrView(res.data(),res.size()) ; }
		// services
		template<::convertible_to<Item> I> Idx emplace(::span<I> const& v) {
			if (!v) return 0 ;
			ULock lock{_mutex} ;
			return Base::emplace( Chunk::s_n_items(v.size()) , v ) ;
		}
		template<::convertible_to<Item> I0,::convertible_to<Item> I> Idx emplace( I0 const& x0 , ::span<I> const& v) {
			ULock lock{_mutex} ;
			return Base::emplace( Chunk::s_n_items(v.size()+1) , x0 , v ) ;
		}
		//
		void pop(Idx idx) {
			if (!idx) return ;
			ULock lock{_mutex} ;
			Base::pop(idx) ;
		}
		void clear(       ) { Base::clear() ; }
		void clear(Idx idx) { pop(idx)      ; }
		Idx shorten_by( Idx idx , Sz by ) {
			Sz sz = size(idx) ;
			SWEAR( by<=sz , by , sz ) ;
			if (by==sz) { clear(idx) ; return 0 ; }
			ULock lock{_mutex} ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Base::at(idx).shorten_by(by) ;
			Base::shorten( idx , Chunk::s_n_items(sz) ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return idx ;
		}
		template<::convertible_to<Item> I> Idx assign( Idx idx , ::span<I> const& v ) {
			//                            vvvvvvvvvv
			if (!idx) {            return emplace(v) ; }
			if (!v  ) { pop(idx) ; return 0          ; }
			//                            ^^^^^^^^^^
			ULock lock{_mutex} ;
			Chunk& chunk = Base::at(idx)              ;
			IdxSz  old_n = chunk.n_items()            ;
			IdxSz  new_n = Chunk::s_n_items(v.size()) ;
			// reallocate
			if (new_n!=old_n) {
				//           vvvvvvvvvvvvvvvv
				/**/   Base::pop    (idx    ) ;
				return Base::emplace(new_n,v) ;
				//           ^^^^^^^^^^^^^^^^
			}
			// in place
			chk_writable() ;
			Item* items = chunk.items() ;
			for( size_t i : iota(::min(v.size(),size_t(chunk.sz))) ) items[i] = v[i] ;
			if (v.size()<chunk.sz) for( size_t i : iota( v.size() , chunk.sz ) ) items[i].~Item()        ;
			else                   for( size_t i : iota( chunk.sz , v.size() ) ) new(items+i) Item{v[i]} ;
			chunk.sz = v.size() ;
			return idx ;
		}
		template<::convertible_to<Item> I> Idx append( Idx idx , ::span<I> const& v ) {
			//               vvvvvvvvvv
			if (!idx) return emplace(v) ;
			if (!v  ) return idx        ;
			//               ^^^^^^^^^^
			ULock lock{_mutex} ;
			Chunk& chunk = Base::at(idx)                       ;
			IdxSz  old_n = chunk.n_items()                     ;
			IdxSz  new_n = Chunk::s_n_items(chunk.sz+v.size()) ;
			// reallocate
			if (new_n>old_n) {
				::vector<Item> both = mk_vector<Item>(view(idx)) ;
				for( Item const& x : v ) both.emplace_back(x) ;
				//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				/**/   Base::pop    (idx                     ) ;
				return Base::emplace(new_n,::span<Item>(both)) ;
				//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			// in place
			Item* items = chunk.items()+chunk.sz ;
			chunk.sz += v.size() ;
			//                               vvvvvvvvvvvvvvvvvvvvvvv
			for( size_t i : iota(v.size()) ) new(items+i) Item{v[i]} ;
			//                               ^^^^^^^^^^^^^^^^^^^^^^^
			return idx ;
		}
		//
		Idx emplace(                     ::basic_string_view<Item> const& s) requires( !::is_const_v<Item> && IsStr ) { return emplace(     ::span<Item const>(s)) ; }
		Idx emplace(      Item const& c0,::basic_string_view<Item> const& s) requires( !::is_const_v<Item> && IsStr ) { return emplace(  c0,::span<Item const>(s)) ; }
		Idx assign (Idx i,               ::basic_string_view<Item> const& s) requires( !::is_const_v<Item> && IsStr ) { return assign (i,   ::span<Item const>(s)) ; }
		Idx append (Idx i,               ::basic_string_view<Item> const& s) requires( !::is_const_v<Item> && IsStr ) { return append (i,   ::span<Item const>(s)) ; }
	} ;

}
