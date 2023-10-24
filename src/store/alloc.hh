// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "struct.hh"

namespace Store {

	// free list sizes are linear until LinearSz, then logarithmic
	// single allocation is LinearSz==0
	namespace Alloc {
		template<class H,class I,size_t LinearSz=0,bool HasData=true> struct Hdr {
			static constexpr uint8_t NFree = LinearSz ? LinearSz+NBits<I>-n_bits(LinearSz+1) : 1 ;
			NoVoid<H>        hdr    ;
			uint8_t          n_free = 0 ;                  // starting at n_free, there is no element in free list
			::array<I,NFree> free   ;
		} ;
		template<class H,class I,size_t LinearSz> struct Hdr<H,I,LinearSz,false> {
			static constexpr uint8_t NFree = 0 ;
			NoVoid<H> hdr ;
		} ;
		template<class I,class D> struct Data {
			static_assert( sizeof(NoVoid<D,I>)>=sizeof(I) ) ;                       // else waste memory
			template<class... A> Data(A&&... args) : data{::forward<A>(args)...} {}
			union {
				NoVoid<D> data ;       // when data is used
				I         nxt  ;       // when data is in free list
			} ;
			~Data() { data.~NoVoid<D>() ; }
		} ;
	}
	template<bool AutoLock,class Hdr_,class Idx_,class Data_,size_t LinearSz=0> struct AllocFile
	:	                 StructFile< false/*AutoLock*/ , Alloc::Hdr<Hdr_,Idx_,LinearSz,IsNotVoid<Data_>> , Idx_ , Alloc::Data<Idx_,Data_> , true/*Multi*/ >   // if !LinearSz, Multi is useless
	{	using Base     = StructFile< false/*AutoLock*/ , Alloc::Hdr<Hdr_,Idx_,LinearSz,IsNotVoid<Data_>> , Idx_ , Alloc::Data<Idx_,Data_> , true/*Multi*/ > ; // but easier to code
		using BaseHdr  =                                 Alloc::Hdr<Hdr_,Idx_,LinearSz,IsNotVoid<Data_>> ;
		using BaseData =                                                                                          Alloc::Data<Idx_,Data_> ;
		//
		using Hdr    = Hdr_                 ;
		using Idx    = Idx_                 ;
		using Data   = Data_                ;
		using HdrNv  = NoVoid<Hdr >         ;
		using DataNv = NoVoid<Data>         ;
		using Sz     = typename Base::Sz    ;
		using ULock  = UniqueLock<AutoLock> ;
		using SLock  = SharedLock<AutoLock> ;
		//
		static constexpr bool HasHdr    = !is_void_v<Hdr >         ;
		static constexpr bool HasData   = !is_void_v<Data>         ;
		static constexpr bool HasDataSz = ::Store::HasDataSz<Data> ;
		static constexpr bool Multi     = LinearSz && HasData      ;
		//
		template<class... A> Idx emplace_back( Idx n , A&&... args ) = delete ;
		using Base::clear    ;
		using Base::size     ;
		using Base::writable ;
		using Base::_mutex   ;

		struct Lst {
			using value_type = Idx ;
			struct Iterator {
				using iterator_categorie = ::input_iterator_tag ;
				using value_type         = Idx                  ;
				using difference_type    = ptrdiff_t            ;
				using pointer            = value_type*          ;
				using reference          = value_type&          ;
				// cxtors & casts
				Iterator( Lst const& s , Idx i ) : _self(&s) , _idx{i} { _legalize() ; }
				// services
				bool      operator==(Iterator const& other) const { return _self==other._self && _idx==other._idx ; }
				Idx       operator* (                     ) const { return _idx ;                                   }
				Iterator& operator++(                     )       { _advance() ; _legalize() ; return *this ;       }
				Iterator  operator++(int                  )       { Iterator res = *this ; ++*this ; return res ;   }
			private :
				void _advance() {
					SWEAR(+_idx) ;
					_idx = Idx(+_idx+1) ;
					if (_idx==_self->size()) _idx = {} ;
				}
				bool _is_legal() const { return !_self->_frees.contains(+_idx) ; }
				bool _at_end  () const { return !_idx ; }
				void _legalize() {
					for( ; !_at_end() ; _advance() ) if (_is_legal()) return ;
				}
				// data
				Lst const* _self ;
				Idx        _idx  = {} ;
			} ;
			// cxtors & casts
			Lst( AllocFile const& s ) : _self{&s} , _lock{s._mutex} {
				for( Idx i=_self->_free(0) ; +i ; i = _self->Base::at(i).nxt ) _frees.insert(+i) ;
			}
			// accesses
			Sz size() const { return _self->size() ; }
			// services
			Iterator begin () const { return Iterator(*this,Idx(1)) ; }
			Iterator cbegin() const { return Iterator(*this,Idx(1)) ; }
			Iterator end   () const { return Iterator(*this,{}    ) ; }
			Iterator cend  () const { return Iterator(*this,{}    ) ; }
			// data
		private :
			AllocFile const*    _self  ;
			::uset<IntIdx<Idx>> _frees ;
			mutable SLock       _lock  ;
		} ;

		// statics
	private :
		static uint8_t _s_bucket(Sz      sz    ) requires( Multi) { return sz<=LinearSz ? sz-1 : LinearSz+n_bits(sz)-n_bits(LinearSz+1)             ; }
		static uint8_t _s_bucket(Sz      sz    ) requires(!Multi) { SWEAR(sz==1,sz) ; return 0 ;                                                      }
		static Sz      _s_sz    (uint8_t bucket) requires( Multi) { return bucket<LinearSz ? bucket+1 : Sz(1)<<(bucket+n_bits(LinearSz+1)-LinearSz) ; } // inverse _s_bucket & return max possible val
		static Sz      _s_sz    (uint8_t bucket) requires(!Multi) { SWEAR(bucket==0,bucket) ; return 1 ;                                              } // .
		// cxtors
	public :
		using StructFile< false/*AutoLock*/ , Alloc::Hdr<Hdr_,Idx_,LinearSz,IsNotVoid<Data_>> , Idx_ , Alloc::Data<Idx_,Data_> , true/*Multi*/ >::StructFile ;
		// accesses
		HdrNv  const& hdr  (       ) const requires(HasHdr ) { return Base::hdr(   ).hdr  ; }
		HdrNv       & hdr  (       )       requires(HasHdr ) { return Base::hdr(   ).hdr  ; }
		HdrNv  const& c_hdr(       ) const requires(HasHdr ) { return Base::hdr(   ).hdr  ; }
		DataNv const& at   (Idx idx) const requires(HasData) { return Base::at (idx).data ; }
		DataNv      & at   (Idx idx)       requires(HasData) { return Base::at (idx).data ; }
		DataNv const& c_at (Idx idx) const requires(HasData) { return Base::at (idx).data ; }
		//
		Idx idx(DataNv const& at) const requires(HasData) {
			// the fancy following expr does a very simple thing : it transforms at into the corresponding ref for our Base
			uintptr_t DataOffset = reinterpret_cast<uintptr_t>(&reinterpret_cast<Base::Data*>(4096)->data) - 4096 ;                      // cannot use 0 as gcc refuses to "dereference" null
			typename Base::Data const& base_at = *reinterpret_cast<Base::Data const*>( reinterpret_cast<uintptr_t>(&at) - DataOffset ) ;
			return Base::idx(base_at) ;
		}
	private :
		Idx const& _free(uint8_t bucket) const requires(HasData) { return Base::hdr().free[bucket] ; }
		Idx      & _free(uint8_t bucket)       requires(HasData) { return Base::hdr().free[bucket] ; }
	public :
		Lst lst() const requires( !Multi && HasData ) { return Lst(*this) ; }
		// services
		template<class... A> Idx emplace( Sz sz , A&&... args ) requires(  Multi && !HasDataSz ) { Idx res = _emplace(sz,::forward<A>(args)...) ;                   return res ; }
		template<class... A> Idx emplace( Sz sz , A&&... args ) requires(  Multi &&  HasDataSz ) { Idx res = _emplace(sz,::forward<A>(args)...) ; _chk_sz(res,sz) ; return res ; }
		template<class... A> Idx emplace(         A&&... args ) requires( !Multi &&  HasData   ) { Idx res = _emplace(1 ,::forward<A>(args)...) ;                   return res ; }
		//
		void shorten( Idx idx , Sz old_sz , Sz new_sz ) requires(  Multi && !HasDataSz ) {                       _shorten( idx , old_sz        , new_sz        ) ; }
		void shorten( Idx idx , Sz old_sz , Sz new_sz ) requires(  Multi &&  HasDataSz ) { _chk_sz(idx,new_sz) ; _shorten( idx , old_sz        , new_sz        ) ; }
		void shorten( Idx idx , Sz old_sz             ) requires(  Multi &&  HasDataSz ) {                       _shorten( idx , old_sz        , _n_items(idx) ) ; }
		void pop    ( Idx idx , Sz sz                 ) requires(  Multi && !HasDataSz ) {                       _pop    ( idx , sz                            ) ; }
		void pop    ( Idx idx , Sz sz                 ) requires(  Multi &&  HasDataSz ) { _chk_sz(idx,sz    ) ; _pop    ( idx , sz                            ) ; }
		void pop    ( Idx idx                         ) requires(  Multi &&  HasDataSz ) {                       _pop    ( idx , _n_items(idx)                 ) ; }
		void pop    ( Idx idx                         ) requires( !Multi &&  HasData   ) {                       _pop    ( idx , 1                             ) ; }
		void clear() {
			ULock lock{_mutex} ;
			Base::clear() ;
			for( Idx& f : Base::hdr().free ) f = 0 ;
		}
		void chk() const requires(!HasData        ) {}
		void chk() const requires(!is_void_v<Data>) ;                          // XXX : why cant we use HasData here with clang ?!?
	private :
		Sz   _n_items( Idx idx         ) requires(HasDataSz) { if (!idx) return 0 ; return at(idx).n_items() ;  }
		void _chk_sz ( Idx idx , Sz sz ) requires(HasDataSz) { SWEAR(sz==Idx(_n_items(idx)),sz,_n_items(idx)) ; }
		template<class... A> Idx _emplace( Sz sz , A&&... args ) requires(HasData) {
			ULock lock{_mutex} ;
			SWEAR(writable) ;
			uint8_t bucket = _s_bucket(sz    ) ;                               // XXX : implement smaller granularity than 2x
			Idx&    free   = _free    (bucket) ;
			Idx     res    = free              ;
			if (+res) {
				free = Base::at(res).nxt ;
			} else {                                                           // try find a larger bucket and split it if above linear zone
				uint8_t avail_bucket = BaseHdr::NFree ;
				if (bucket>=LinearSz) {
					for( avail_bucket = bucket+1 ; avail_bucket<Base::hdr().n_free ; avail_bucket++ ) if (+_free(avail_bucket)) break ;
				}
				//                                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (avail_bucket>=Base::hdr().n_free) return Base::emplace_back( _s_sz(bucket) , ::forward<A>(args)... ) ; // no space available
				//                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				res                 = _free(avail_bucket) ;
				_free(avail_bucket) = Base::at(res).nxt   ;
				fence() ;                                                      // ensure free list is always consistent
				for( uint8_t i=avail_bucket-1 ; i>=bucket ; i-- ) {            // put upper half in adequate free list
					Idx upper(+res+_s_sz(i)) ;
					Base::at(upper).nxt = 0 ;                                  // free list was initially empty
					fence() ;                                                  // ensure free list is always consistent
					_free(i) = upper ;
				}
			}
			fence() ;                                                          // ensure free list is always consistent
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Base::emplace(res,::forward<A>(args)...) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return res ;
		}
		void _shorten( Idx idx , Sz old_sz , Sz new_sz ) requires(Multi) {
			if (!new_sz) { _pop(idx,old_sz) ; return ; }
			SWEAR( writable && new_sz<=old_sz , new_sz , old_sz ) ;
			ULock lock{_mutex} ;
			uint8_t old_bucket = _s_bucket(old_sz) ;
			uint8_t new_bucket = _s_bucket(new_sz) ;
			//
			new_sz = _s_sz(new_bucket) ;
			while (old_bucket>new_bucket) {
				Sz old_sz   = _s_sz(old_bucket) ;
				Sz extra_sz = old_sz-new_sz     ;
				if (extra_sz<=LinearSz) {
					uint8_t b = _s_bucket(extra_sz) ;
					SWEAR( _s_sz(b)==extra_sz , _s_sz(b) , extra_sz ) ;        // ensure allocation is perfect, which must be the case in the linear zone
					_dealloc( static_cast<Sz>(+idx+new_sz) , b ) ;             // free remaining all at once, which is possible as soon as extra_sz is a perfect fit
					break ;
				}
				old_sz     /= 2                 ;                              // reduce by 2x and retry
				old_bucket  = _s_bucket(old_sz) ;
				_dealloc( static_cast<Sz>(+idx+old_sz) , old_bucket ) ;
			}
			SWEAR( new_bucket<=old_bucket , new_bucket , old_bucket ) ;
		}
		void _pop( Idx idx , Sz sz ) requires( HasData) {
			if (!idx) return ;
			ULock lock{_mutex} ;
			SWEAR(writable) ;
			Base::pop(idx) ;
			_dealloc(idx,_s_bucket(sz)) ;
		}
		void _dealloc( Idx idx , uint8_t bucket ) requires( HasData) {
			Idx& free = _free(bucket) ;
			Base::at(idx).nxt  = free                             ;
			Base::hdr().n_free = ::max(Base::hdr().n_free,bucket) ;
			fence() ;                                                          // ensure free list is always consistent
			free = idx ;
		}
	} ;
	template<bool AutoLock,class Hdr,class Idx,class Data,size_t LinearSz> void AllocFile<AutoLock,Hdr,Idx,Data,LinearSz>::chk() const requires(!is_void_v<Data>) {
		SLock lock{_mutex} ;
		Base::chk() ;
		::vector<bool> free_map ;
		free_map.resize(size()) ;
		for( uint8_t bucket = 0 ; bucket<BaseHdr::NFree ; bucket++ ) {
			Sz sz = _s_sz(bucket) ;
			for( Idx idx=_free(bucket) ; +idx ; idx=Base::at(idx).nxt ) {
				SWEAR( static_cast<Sz>(+idx+_s_sz(bucket))<=size() , idx , _s_sz(bucket) , size() ) ;
				for( Sz i=0 ; i<sz ; i++ ) {
					SWEAR(!free_map[+idx+i]) ;
					free_map[+idx+i] = true ;
				}
			}
		}
	}

}
