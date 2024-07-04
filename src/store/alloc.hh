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

		// sz to bucket mapping
		// bucket and sz functions must be such that :
		// = bucket(1   )==0            i.e. first bucket is for size 1
		// - bucket(sz+1)>=bucket(sz)   i.e. buckets are sorted
		// - bucket(sz+1)<=bucket(sz)+1 i.e. there are less buckets than sizes
		// - bucket(sz(b)  )==b         i.e. sz is the inverse function of bucket
		// - bucket(sz(b)+1)==b+1       i.e. sz returns the largest size that first in a bucket
		// This implementation chooses a integer to float conversion.
		// LinearSz is the largest size up to which there is a bucket for each size.
		// For example, if LinearSz==4, bucket sizes are : 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, ...

		template<uint8_t Mantissa> constexpr size_t bucket(size_t sz) {
			if (Mantissa==0) return 0 ;
			// linear area
			if (sz<=(1<<Mantissa)) return sz-1 ;
			// logarithmic range
			constexpr uint8_t Mantissa1 = Mantissa ? Mantissa-1 : 0 ; // actually Mantissa-1, with a protection to avoid compilation error when Mantissa==0
			uint8_t sz_bits  = n_bits(sz)                       ;
			uint8_t exp      = sz_bits-Mantissa                 ;
			uint8_t mantissa = ((sz-1)>>exp)+1 - (1<<Mantissa1) ;     // msb is always 1, mask it as in IEEE float format
			return (size_t(exp+1)<<Mantissa1) + mantissa - 1 ;        // adjust the +1's and -1's to provide a regular function
		}

		template<uint8_t Mantissa> constexpr size_t sz(size_t bucket) {
			if (Mantissa==0) return 1 ;
			// linear area
			if (bucket<(1<<Mantissa)) return bucket+1 ;
			// logarithmic range
			constexpr uint8_t Mantissa1 = Mantissa ? Mantissa-1 : 0 ; // actually Mantissa-1, with a protection to avoid compilation error when Mantissa==0
			uint8_t exp      = ((bucket+1)>>Mantissa1) - 1                                      ; SWEAR(exp<NBits<size_t>) ;
			size_t  mantissa = (size_t(1)<<Mantissa1) + bucket - (size_t(exp+1)<<Mantissa1) + 1 ;
			return mantissa<<exp ;
		}

		#if 0
			// static check (enable to debug)
			template<size_t Mantissa> constexpr bool chk() {
				constexpr size_t N = 4<<Mantissa ;
				/**/                                            if ( bucket<Mantissa>(1                ) != 0                     ) return false ;
				for( size_t s=1 ; s<                 N  ; s++ ) if ( bucket<Mantissa>(s+1              ) <  bucket<Mantissa>(s)   ) return false ;
				for( size_t s=1 ; s<                 N  ; s++ ) if ( bucket<Mantissa>(s+1              ) >  bucket<Mantissa>(s)+1 ) return false ;
				for( size_t b=0 ; b<bucket<Mantissa>(N) ; b++ ) if ( bucket<Mantissa>(sz<Mantissa>(b)  ) != b                     ) return false ;
				for( size_t b=0 ; b<bucket<Mantissa>(N) ; b++ ) if ( bucket<Mantissa>(sz<Mantissa>(b)+1) != b+1                   ) return false ;
				return true ;
			}
			static_assert(chk<1>()) ;
			static_assert(chk<2>()) ;
			static_assert(chk<3>()) ;
			static_assert(chk<4>()) ;
		#endif

		template<class H,class I,uint8_t Mantissa=0,bool HasData=true> struct Hdr {
			static constexpr size_t NFree = bucket<Mantissa>(lsb_msk(8*sizeof(I)))+1 ; // number of necessary slot is highest possible index + 1
			NoVoid<H>        hdr  ;
			::array<I,NFree> free ;
		} ;
		template<class H,class I,uint8_t Mantissa> struct Hdr<H,I,Mantissa,false> {
			static constexpr size_t NFree = 0 ;
			NoVoid<H> hdr ;
		} ;
		template<class I,class D> struct Data {
			static_assert( sizeof(NoVoid<D,I>)>=sizeof(I) ) ;                          // else waste memory
			template<class... A> Data(A&&... args) : data{::forward<A>(args)...} {}
			union {
				NoVoid<D> data ;                                                       // when data is used
				I         nxt  ;                                                       // when data is in free list
			} ;
			~Data() { data.~NoVoid<D>() ; }
		} ;

	}
	template<bool AutoLock,class Hdr_,class Idx_,class Data_,uint8_t Mantissa=0> struct AllocFile
	:	                 StructFile< false/*AutoLock*/ , Alloc::Hdr<Hdr_,Idx_,Mantissa,IsNotVoid<Data_>> , Idx_ , Alloc::Data<Idx_,Data_> , true/*Multi*/ >   // if !Mantissa, Multi is useless ...
	{	using Base     = StructFile< false/*AutoLock*/ , Alloc::Hdr<Hdr_,Idx_,Mantissa,IsNotVoid<Data_>> , Idx_ , Alloc::Data<Idx_,Data_> , true/*Multi*/ > ; // ... but easier to code
		using BaseHdr  =                                 Alloc::Hdr<Hdr_,Idx_,Mantissa,IsNotVoid<Data_>> ;
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
		static constexpr bool Multi     = Mantissa && HasData      ;
		//
		template<class... A> Idx emplace_back( Idx n , A&&... args ) = delete ;
		using Base::clear    ;
		using Base::name     ;
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
				Iterator( Lst const& s , Idx i ) : _self(&s) , _idx{i} { _fix_end() ; _legalize() ; }
				// services
				bool      operator==(Iterator const& other) const { return _self==other._self && _idx==other._idx ; }
				Idx       operator* (                     ) const { return _idx ;                                   }
				Iterator& operator++(                     )       { _advance() ; _legalize() ; return *this ;       }
				Iterator  operator++(int                  )       { Iterator res = *this ; ++*this ; return res ;   }
			private :
				void _advance() {
					SWEAR(+_idx) ;
					_idx = Idx(+_idx+1) ;
					_fix_end() ;
				}
                void _fix_end ()       { if (_idx==_self->size()) _idx = {} ;                       }
				bool _is_legal() const { return !_self->_frees.contains(+_idx) ;                    }
				bool _at_end  () const { return !_idx                          ;                    }
				void _legalize()       { for( ; !_at_end() ; _advance() ) if (_is_legal()) return ; }
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
		static Sz _s_bucket(Sz sz    ) { return Alloc::bucket<Mantissa>(sz    ) ; }
		static Sz _s_sz    (Sz bucket) { return Alloc::sz    <Mantissa>(bucket) ; }
		// cxtors
	public :
		using Base::Base ;
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
			uintptr_t DataOffset = reinterpret_cast<uintptr_t>(&reinterpret_cast<typename Base::Data*>(4096)->data) - 4096 ;                      // cannot use 0 as gcc refuses to "dereference" null
			typename Base::Data const& base_at = *reinterpret_cast<typename Base::Data const*>( reinterpret_cast<uintptr_t>(&at) - DataOffset ) ;
			return Base::idx(base_at) ;
		}
	private :
		Idx const& _free(Sz bucket) const requires(HasData) { return Base::hdr().free[bucket] ; }
		Idx      & _free(Sz bucket)       requires(HasData) { return Base::hdr().free[bucket] ; }
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
		//
		void clear()                                  { ULock lock{_mutex} ; _clear() ; }
		void chk  () const requires(!HasData        ) {                                 }
		void chk  () const requires(!is_void_v<Data>) ;                                                                        // XXX : why cant we use HasData here with clang ?!?
	protected :
		void _clear() {
			Base::_clear() ;
			for( Idx& f : Base::hdr().free ) f = 0 ;
		}
	private :
		Sz   _n_items( Idx idx         ) requires(HasDataSz) { if (!idx) return 0 ; return at(idx).n_items() ;  }
		void _chk_sz ( Idx idx , Sz sz ) requires(HasDataSz) { SWEAR(sz==Idx(_n_items(idx)),sz,_n_items(idx)) ; }
		template<class... A> Idx _emplace( Sz sz , A&&... args ) requires(HasData) {
			if (!writable) throw "cannot allocate item in read-only file "+name ;
			ULock lock   { _mutex }          ;
			Sz    bucket = _s_bucket(sz    ) ;
			Idx&  free   = _free    (bucket) ;
			//                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if (!free) return Base::emplace_back( _s_sz(bucket) , ::forward<A>(args)... ) ;
			//                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			Idx res = free ;
			free = Base::at(res).nxt ;
			fence() ;                                                                                                          // ensure free list is always consistent
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Base::emplace(res,::forward<A>(args)...) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return res ;
		}
		void _shorten( Idx idx , Sz old_sz , Sz new_sz ) requires(Multi) {
			if (!new_sz       ) { _pop(idx,old_sz) ; return ; }
			if (new_sz==old_sz)                      return ;
			if (!writable     ) throw "cannot shorten item in read-only file "+name ;
			SWEAR( new_sz<=old_sz , new_sz , old_sz ) ;
			ULock lock{_mutex} ;
			Sz old_bucket = _s_bucket(old_sz) ;
			Sz new_bucket = _s_bucket(new_sz) ;
			// deallocate extra room
			new_sz = _s_sz(new_bucket) ;
			old_sz = _s_sz(old_bucket) ;
			while (new_sz<old_sz) {                                                                                            // deallocate as much as possible in a single bucket and iterate
				Sz extra_sz        = old_sz-new_sz       ;
				Sz extra_bucket    = _s_bucket(extra_sz) ;                                                                     // the bucket that can contain extra_sz
				Sz extra_bucket_sz = _s_sz(extra_bucket) ;                                  SWEAR(extra_bucket_sz>=extra_sz) ; // _s_sz returns the largest size that fits in extra_bucket
				{ if (extra_bucket_sz>extra_sz) extra_bucket_sz = _s_sz(--extra_bucket) ; } SWEAR(extra_bucket_sz<=extra_sz) ; // but we want the largest bucket that fits in extra_sz
				//
				old_sz -= extra_bucket_sz ;
				_dealloc( Idx(+idx+old_sz) , extra_bucket ) ;
			}
			SWEAR( new_bucket<=old_bucket , new_bucket , old_bucket ) ;
		}
		void _pop( Idx idx , Sz sz ) requires( HasData) {
			if (!idx) return ;
			if (!writable) throw "cannot pop item in read-only file "+name ;
			ULock lock{_mutex} ;
			Base::pop(idx) ;
			_dealloc(idx,_s_bucket(sz)) ;
		}
		void _dealloc( Idx idx , Sz bucket ) requires( HasData) {
			Idx& free = _free(bucket) ;
			Base::at(idx).nxt = free ;
			fence() ;                                                                                                          // ensure free list is always consistent
			free = idx ;
		}
	} ;
	template<bool AutoLock,class Hdr,class Idx,class Data,uint8_t Mantissa> void AllocFile<AutoLock,Hdr,Idx,Data,Mantissa>::chk() const requires(!is_void_v<Data>) {
		Base::chk() ;
		::vector<bool> free_map ;
		free_map.resize(size()) ;
		for( Sz bucket = 0 ; bucket<BaseHdr::NFree ; bucket++ ) {
			Sz sz = _s_sz(bucket) ;
			for( Idx idx=_free(bucket) ; +idx ; idx=Base::at(idx).nxt ) {
				throw_unless( static_cast<Sz>(+idx+_s_sz(bucket))<=size() , "free list out of range at ",idx ) ;
				for( Sz i=0 ; i<sz ; i++ ) {
					SWEAR(!free_map[+idx+i]) ;
					free_map[+idx+i] = true ;
				}
			}
		}
	}

}
