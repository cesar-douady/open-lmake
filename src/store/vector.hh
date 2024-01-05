// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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
			Item const* items() const { return reinterpret_cast<Item const*>(_items) ; }
			Item      * items()       { return reinterpret_cast<Item      *>(_items) ; }
			// data
			Sz                    sz            ;
			alignas(Item) ItemMem _items[MinSz] ;
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
			using VecView = ::c_vector_view<Item> ;
			using Base::sz    ;
			using Base::items ;
			// statics
			static constexpr IdxSz s_n_items(Sz sz_) {
				return div_up(
					sizeof(Base) - MinSz*sizeof(Item) + sz_*sizeof(Item) // /!\ unsigned computation : take care of any subtraction
				,	sizeof(Base)
				) ;
			} // compute size before we have an object
			// cxtors & casts
			using Base::Base ;
			template<::convertible_to<Item> I> Chunk(::vector_view<I> const& v) : Base{Sz(v.size())} {
				for( Sz i=0 ; i<sz ; i++ ) new(items()+i) Item{v[i]} ;
			}
			template<::convertible_to<Item> I0,::convertible_to<Item> I> Chunk( I0 const& x0 , ::vector_view<I> const& v ) : Base{Sz(1+v.size())} {
				new(items()) Item{x0} ;
				for( Sz i=1 ; i<sz ; i++ ) new(items()+i) Item{v[i-1]} ;
			}
			//
			Chunk (                  VecView const& v ) requires(IsTrivial) : Base{Sz(  v.size())} {                   memcpy( items()   , v.cbegin() , v.size()*sizeof(Item) ) ; }
			Chunk ( Item const& x0 , VecView const& v ) requires(IsTrivial) : Base{Sz(1+v.size())} { items()[0] = x0 ; memcpy( items()+1 , v.cbegin() , v.size()*sizeof(Item) ) ; }
			//
			~Chunk() requires(!IsTrivial) { for( Sz i=0 ; i<sz ; i++ ) items()[i].~Item() ; }
			~Chunk() requires( IsTrivial) {                                                 }
			//
			operator VecView() const { return VecView(items(),sz) ; }
			// accesses
			IdxSz n_items() const { return s_n_items(sz) ; } // to inform AllocFile of the size of the item
			// services
			void shorten_by(Sz by) {
				SWEAR( by<sz , by , sz ) ;
				for( Sz i=sz-by ; i<sz ; i++ ) items()[i].~Item() ;
				sz = sz-by ;
			}
		} ;

	}

	template<bool AutoLock,class Hdr_,class Idx_,class Item_,class Sz=IntIdx<Idx_>,size_t MinSz=1,size_t LinearSz=16*MinSz> struct VectorFile
	:	              AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , Vector::Chunk<Idx_,Item_,Sz,MinSz> , Vector::Chunk<Idx_,Item_,Sz,MinSz>::s_n_items(LinearSz) >
	{	using Base  = AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , Vector::Chunk<Idx_,Item_,Sz,MinSz> , Vector::Chunk<Idx_,Item_,Sz,MinSz>::s_n_items(LinearSz) > ;
		using Chunk =                                              Vector::Chunk<Idx_,Item_,Sz,MinSz>                                                             ;
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
		using VecView = ::c_vector_view    <Item> ;
		using StrView = ::basic_string_view<Char> ;
		using ULock   = UniqueLock<AutoLock>      ;
		using SLock   = SharedLock<AutoLock>      ;
		//
		void at     (Idx   ) = delete ;
		void shorten(Idx,Sz) = delete ;
		//
		using Base::size   ;
		using Base::_mutex ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		bool        empty   (Idx idx) const                 {                            return !idx                            ; }
		Sz          size    (Idx idx) const                 { if (!idx) return 0       ; return Base::at(idx).sz                ; }
		Item const* items   (Idx idx) const                 { if (!idx) return nullptr ; return Base::at(idx).items()           ; }
		Item      * items   (Idx idx)                       { if (!idx) return nullptr ; return Base::at(idx).items()           ; }
		Item const* c_items (Idx idx) const                 { if (!idx) return nullptr ; return Base::at(idx).items()           ; }
		VecView     view    (Idx idx) const                 { if (!idx) return {}      ; return Base::at(idx)                   ; }
		StrView     str_view(Idx idx) const requires(IsStr) { VecView res = view(idx) ;  return StrView(res.begin(),res.size()) ; }
		// services
		template<::convertible_to<Item> I> Idx emplace(::vector_view<I> const& v) {
			if (!v) return 0                                               ;
			ULock lock{_mutex} ;
			return Base::emplace( Chunk::s_n_items(v.size()) , v ) ;
		}
		template<::convertible_to<Item> I0,::convertible_to<Item> I> Idx emplace( I0 const& x0 , ::vector_view<I> const& v) {
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
		template<::convertible_to<Item> I> Idx assign( Idx idx , ::vector_view<I> const& v ) {
			//                            vvvvvvvvvv
			if (!idx) {            return emplace(v) ; }
			if (!v  ) { pop(idx) ; return 0          ; }
			//                            ^^^^^^^^^^
			ULock lock{_mutex} ;
			Chunk& chunk = Base::at(idx)              ;
			IdxSz  old_n = chunk.n_items()            ;
			IdxSz  new_n = Chunk::s_n_items(v.size()) ;
			// reallocate
			if (new_n>old_n) {
				//           vvvvvvvvvvvvvvvv
				/**/   Base::pop    (idx    ) ;
				return Base::emplace(new_n,v) ;
				//           ^^^^^^^^^^^^^^^^
			}
			// in place
			Item* items   = chunk.items()     ;
			bool  shorten = v.size()<chunk.sz ;
			chunk.sz = v.size() ;
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wstringop-overflow"               // chunk manipulations are too fancy for gcc to understand, sorry we'll have to live without this warning
			//                                                           vvvvvvvvvvvvvvv                                               vvvvvvvvvvvvvvvvvvvvvvv
			if      (new_n<old_n) { for( size_t i=0 ; i<v.size() ; i++ ) items[i] = v[i] ; for( size_t i=v.size() ; i<chunk.sz ; i++ ) items[i].~Item()        ; Base::shorten(idx,new_n) ; }
			else if (shorten    ) { for( size_t i=0 ; i<v.size() ; i++ ) items[i] = v[i] ; for( size_t i=v.size() ; i<chunk.sz ; i++ ) items[i].~Item()        ;                            }
			else                  { for( size_t i=0 ; i<chunk.sz ; i++ ) items[i] = v[i] ; for( size_t i=chunk.sz ; i<v.size() ; i++ ) new(items+i) Item{v[i]} ;                            }
			//                                                           ^^^^^^^^^^^^^^^                                               ^^^^^^^^^^^^^^^^^^^^^^^
			#pragma GCC diagnostic pop
			return idx ;
		}
		template<::convertible_to<Item> I> Idx append( Idx idx , ::vector_view<I> const& v ) {
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
				::vector<Item> both = mk_vector(view(idx)) ;
				for( Item const& x : v ) both.emplace_back(x) ;
				//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				/**/   Base::pop    (idx                            ) ;
				return Base::emplace(new_n,::vector_view<Item>(both)) ;
				//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			// in place
			Item* items = chunk.items()+chunk.sz ;
			chunk.sz += v.size() ;
			//                                   vvvvvvvvvvvvvvvvvvvvvvv
			for( size_t i=0 ; i<v.size() ; i++ ) new(items+i) Item{v[i]} ;
			//                                   ^^^^^^^^^^^^^^^^^^^^^^^
			return idx ;
		}
		//
		#define CTI ::convertible_to<Item>
		template<       CTI I> Idx emplace(                   ::vector           <I> const& v) requires( !::is_const_v<I>          ) { return emplace(     ::c_vector_view<I>(v)) ; }
		template<CTI I0,CTI I> Idx emplace(      I0 const& x0,::vector           <I> const& v) requires( !::is_const_v<I>          ) { return emplace(  x0,::c_vector_view<I>(v)) ; }
		template<       CTI I> Idx assign (Idx i,             ::vector           <I> const& v) requires( !::is_const_v<I>          ) { return assign (i,   ::c_vector_view<I>(v)) ; }
		template<       CTI I> Idx append (Idx i,             ::vector           <I> const& v) requires( !::is_const_v<I>          ) { return append (i,   ::c_vector_view<I>(v)) ; }
		template<       CTI I> Idx emplace(                   ::basic_string_view<I> const& s) requires( !::is_const_v<I> && IsStr ) { return emplace(     ::c_vector_view<I>(s)) ; }
		template<CTI I0,CTI I> Idx emplace(      I0 const& c0,::basic_string_view<I> const& s) requires( !::is_const_v<I> && IsStr ) { return emplace(  c0,::c_vector_view<I>(s)) ; }
		template<       CTI I> Idx assign (Idx i,             ::basic_string_view<I> const& s) requires( !::is_const_v<I> && IsStr ) { return assign (i,   ::c_vector_view<I>(s)) ; }
		template<       CTI I> Idx append (Idx i,             ::basic_string_view<I> const& s) requires( !::is_const_v<I> && IsStr ) { return append (i,   ::c_vector_view<I>(s)) ; }
		template<       CTI I> Idx emplace(                   ::basic_string     <I> const& s) requires( !::is_const_v<I> && IsStr ) { return emplace(     ::c_vector_view<I>(s)) ; }
		template<CTI I0,CTI I> Idx emplace(      I0 const& c0,::basic_string     <I> const& s) requires( !::is_const_v<I> && IsStr ) { return emplace(  c0,::c_vector_view<I>(s)) ; }
		template<       CTI I> Idx assign (Idx i,             ::basic_string     <I> const& s) requires( !::is_const_v<I> && IsStr ) { return assign (i,   ::c_vector_view<I>(s)) ; }
		template<       CTI I> Idx append (Idx i,             ::basic_string     <I> const& s) requires( !::is_const_v<I> && IsStr ) { return append (i,   ::c_vector_view<I>(s)) ; }
		#undef CTI
	} ;

}
