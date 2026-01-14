// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "raw_file.hh"

namespace Store {

	namespace Struct {

		template<class Hdr_,class Idx,class Data> struct Hdr {
			using HdrNv = NoVoid<Hdr_> ;
			using Sz    = UintIdx<Idx> ;
			// cxtors & casts
			template<class... A> Hdr(A&&... args) : hdr{::forward<A>(args)...} {}
			// data
			// we need to force alignment for subsequent data
			// ideally we would like to put the alignas constraints on the type, but this does not seem to be allowed (and does not work)
			// also, putting a constraint less than the natural constraint is not supported
			// so the idea is to put the alignment constraint on the first item (minimal room lost) and to also put the natural alignment at as constraint
			alignas(Data) alignas(Sz) Sz    sz  = 1 ; // logical size, i.e. first non-allocated idx ==> account for unused idx 0
			[[no_unique_address]]     HdrNv hdr ;     // no need to allocate space if header is empty
		} ;

		template<class Hdr_,class Idx,class Data> static constexpr size_t _offset(size_t idx) {
			// unsigned types handle negative values modulo 2^n, which is ok
			// round up to ensure cache alignment if Data is properly sized
			// START_OF_VERSIONING REPO CACHE
			constexpr size_t CacheLineSz = 64                                                               ; // hint only, defined independently of ::hardware_destructive_interference_size ...
			constexpr size_t Offset0     = round_up<CacheLineSz>( sizeof(Hdr<Hdr_,Idx,Data>)-sizeof(Data) ) ; // ... to ensure inter-operability
			// END_OF_VERSIONING
			//
			return Offset0 + sizeof(Data)*idx ;
		}

	}

	template<char ThreadKey,class Hdr_,IsIdx Idx_,uint8_t NIdxBits,class Data_,bool Multi=false> struct StructFile
	:	              RawFile<ThreadKey,Struct::_offset<Hdr_,Idx_,Data_>(size_t(1)<<NIdxBits)>
	{	using Base  = RawFile<ThreadKey,Struct::_offset<Hdr_,Idx_,Data_>(size_t(1)<<NIdxBits)> ;
		using Hdr   = Hdr_         ;
		using Idx   = Idx_         ;
		using Data  = Data_        ;
		using HdrNv = NoVoid<Hdr > ;
		using Sz    = UintIdx<Idx> ;
		//
		static constexpr bool HasHdr    = !::is_void_v<Hdr >       ;
		static constexpr bool HasDataSz = ::Store::HasDataSz<Data> ;
		//
		static_assert( !::is_void_v<Data> ) ;
		//
		using StructHdr = Struct::Hdr<Hdr,Idx,Data> ; static_assert(alignof(StructHdr)%alignof(Data)==0) ; // check alignment constraints
		void expand(size_t) = delete ;
		//
		using Base::chk_thread   ;
		using Base::chk_writable ;
		using Base::base         ;
		using Base::name         ;
		using Base::writable     ;

		struct Lst {
			struct Iterator {
				using value_type      = Idx       ;
				using difference_type = ptrdiff_t ;
				// cxtors & casts
				Iterator(Idx i) : _idx{i} {}
				// services
				bool      operator==(Iterator const& other) const = default ;
				Idx       operator* (                     ) const { SWEAR(+_idx) ;                 return _idx ; }
				Iterator& operator++(                     )       {_idx = Idx(+_idx+1)           ; return self ; }
				Iterator  operator++(int                  )       { Iterator res = self ; ++self ; return res  ; }
				// data
				Idx _idx = {} ;
			} ;
			// cxtors & casts
			Lst(StructFile const& sf) : _self{&sf} {}
			// accesses
			Sz size() const { return _self->size() ; }
			// services
			Iterator begin () const { return Iterator(Idx(1)) ; }
			Iterator cbegin() const { return Iterator(Idx(1)) ; }
			Iterator end   () const { return Iterator(size()) ; }
			Iterator cend  () const { return Iterator(size()) ; }
			// data
		private :
			StructFile const* _self ;
		} ;

		// statics
	private :
		static constexpr size_t _Offset1 = Struct::_offset<Hdr,Idx,Data>(1) ;
		static constexpr size_t _s_offset(Sz idx) { return Struct::_offset<Hdr,Idx,Data>(idx) ; }
		// cxtors & casts
	public :
		StructFile() = default ;
		template<class... A> StructFile( NewType                              , A&&... hdr_args ) { init( New             , ::forward<A>(hdr_args)... ) ; }
		template<class... A> StructFile( ::string const& name , bool writable , A&&... hdr_args ) { init( name , writable , ::forward<A>(hdr_args)... ) ; }
		//
		template<class... A> void init( NewType                              , A&&... hdr_args ) { init( "" , true/*writable*/ , ::forward<A>(hdr_args)... ) ; }
		template<class... A> void init( ::string const& name , bool writable , A&&... hdr_args ) {
			Base::init( name , writable ) ;
			if (Base::operator+()) return ;
			throw_unless( writable , "cannot init read-only file ",name ) ;
			Base::expand( _s_offset(1) , false/*thread_chk*/ ) ;                                          // 1 is the first used idx
			new(&_struct_hdr()) StructHdr{::forward<A>(hdr_args)...} ;
		}
		// accesses
		bool         operator+(               ) const                  {               return size()>1                                                        ; }
		Sz           size     (               ) const                  {               return _struct_hdr().sz                                                ; }
		HdrNv const& hdr      (               ) const requires(HasHdr) {               return _struct_hdr().hdr                                               ; }
		HdrNv      & hdr      (               )       requires(HasHdr) {               return _struct_hdr().hdr                                               ; }
		HdrNv const& c_hdr    (               ) const requires(HasHdr) {               return _struct_hdr().hdr                                               ; }
		Data  const& at       (Idx         idx) const                  { SWEAR(+idx) ; return *::launder(reinterpret_cast<Data const*>(base+_s_offset(+idx))) ; }
		Data       & at       (Idx         idx)                        { SWEAR(+idx) ; return *::launder(reinterpret_cast<Data      *>(base+_s_offset(+idx))) ; }
		Data  const& c_at     (Idx         idx) const                  {               return at(idx)                                                         ; }
		Idx          idx      (Data const& at_) const                  {               return Idx((&at_-&at(Idx(1)))+1)                                       ; }
		void         clear    (Idx         idx)                        { if (+idx) at(idx) = {} ;                                                               }
		Lst          lst      (               ) const requires(!Multi) { chk_thread() ; return Lst(self) ;                                                      }
	private :
		StructHdr const& _struct_hdr() const { return *::launder(reinterpret_cast<StructHdr const*>(base)) ; }
		StructHdr      & _struct_hdr()       { return *::launder(reinterpret_cast<StructHdr      *>(base)) ; }
		Sz             & _size      ()       { return _struct_hdr().sz                                     ; }
		// services
	public :
		template<class... A> Idx emplace_back( Sz sz , A&&... args ) requires( Multi) { return _emplace_back(sz,::forward<A>(args)...) ; }
		template<class... A> Idx emplace_back(         A&&... args ) requires(!Multi) { return _emplace_back(1 ,::forward<A>(args)...) ; }
		void clear() {
			Base::clear(sizeof(StructHdr)) ;
			_size() = 1 ;
		}
		void chk() const {
			Base::chk() ;
			throw_unless( size()                        , "incoherent size info"                      ) ; // size is 1 for an empty file
			throw_unless( _s_offset(size())<=Base::size , "logical size is larger than physical size" ) ;
		}
	protected :
		/**/                 void _pop    ( Idx idx               ) { chk_writable() ; if (+idx) at(idx).~Data() ;                 }
		template<class... A> void _emplace( Idx idx , A&&... args ) { chk_writable() ; new(&at(idx)) Data{::forward<A>(args)...} ; }
	private :
		void _chk_sz( Idx   idx   , Sz   sz   ) requires(   HasDataSz && Multi  ) { SWEAR( sz==Idx(_at(idx).n_items()) , sz , _at(idx).n_items() ) ; }
		void _chk_sz( Idx /*idx*/ , Sz /*sz*/ ) requires(!( HasDataSz && Multi )) {                                                                  }
		//
		template<class... A> Idx _emplace_back( Sz sz , A&&... args ) {
			chk_thread() ;
			Sz old_sz = size()      ;
			Sz new_sz = old_sz + sz ;
			swear( new_sz>=old_sz && new_sz<(size_t(1)<<NIdxBits) ,"index overflow on ",name) ;           // ensure no arithmetic overflow before checking capacity
			Base::expand(_s_offset(new_sz)) ;
			fence() ;                                                                                     // update state when it is legal to do so
			_size() = new_sz ;                                                                            // once allocation is done, no reason to maintain lock
			Idx res { old_sz } ;
			_emplace( res , ::forward<A>(args)... ) ;
			_chk_sz( res , sz ) ;
			return res ;
		}
	} ;

}
