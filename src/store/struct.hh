// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "file.hh"

namespace Store {

	//
	// StructFile
	//

	template<bool AutoLock,class Hdr_,IsIdx Idx_,class Data_,bool Multi=false> struct StructFile : File<false/*AutoLock*/> { // we manage the mutex
		using Base   = File                 ;
		using Hdr    = Hdr_                 ;
		using Idx    = Idx_                 ;
		using Data   = Data_                ;
		using HdrNv  = NoVoid<Hdr >         ;
		using DataNv = NoVoid<Data>         ;
		using Sz     = IntIdx<Idx>          ;
		using ULock  = UniqueLock<AutoLock> ;
		using SLock  = SharedLock<AutoLock> ;
		//
		static constexpr bool HasHdr    = !::is_void_v<Hdr >       ;
		static constexpr bool HasData   = !::is_void_v<Data>       ;
		static constexpr bool HasDataSz = ::Store::HasDataSz<Data> ;
		static constexpr bool HasFile   = HasHdr || HasData        ;
		//
		static_assert( !Multi || HasData ) ;
		//
		struct StructHdr {
			// cxtors & casts
			template<class... A> StructHdr(A&&... args) : hdr(::forward<A>(args)...) {}
			// data
			// we need to force alignment for subsequent data
			// ideally we would like to put the alignas constraints on the type, but this does not seem to be allowed (and does not work)
			// also, putting a constraint less than the natural constraint is undefined behavior
			// so the idea is to put the alignment constraint on the first item (minimal room lost) and to also put the natural alignment at as constraint
			[[no_unique_address]] alignas(DataNv) alignas(HdrNv) HdrNv hdr ;     // no need to allocate space if header is empty
			[[                 ]]                                Sz    sz  = 1 ; // logical size, i.e. first non-allocated idx ==> account for unused idx 0

		} ;
		void expand(size_t) = delete ;
		//
		using Base::base     ;
		using Base::writable ;
		// statics
	private :
		static size_t _offset(Sz idx) requires(HasFile) { SWEAR(idx) ; return sizeof(StructHdr)+sizeof(DataNv)*(idx-1) ; }
		// cxtors & casts
		template<class... A> void _alloc_hdr(A&&... hdr_args) requires(HasFile) {
			Base::expand(_offset(1)) ;                                 // 1 is the first used idx
			new(&_struct_hdr()) StructHdr{::forward<A>(hdr_args)...} ;
		}
	public :
		/**/                 StructFile(                                                        )                    = default ;
		/**/                 StructFile( NewType                                                ) requires(!HasFile) { init( New                                         ) ; }
		template<class... A> StructFile( NewType                              , A&&... hdr_args ) requires( HasFile) { init( New             , ::forward<A>(hdr_args)... ) ; }
		/**/                 StructFile( ::string const& name , bool writable                   ) requires(!HasFile) { init( name , writable                             ) ; }
		template<class... A> StructFile( ::string const& name , bool writable , A&&... hdr_args ) requires( HasFile) { init( name , writable , ::forward<A>(hdr_args)... ) ; }
		/**/                 void init( NewType                                                        ) requires(!HasFile) { init( "" , true                             ) ; }
		template<class... A> void init( NewType                                      , A&&... hdr_args ) requires( HasFile) { init( "" , true , ::forward<A>(hdr_args)... ) ; }
		/**/                 void init( ::string const& /*name*/ , bool /*writable*/                   ) requires(!HasFile) {}
		template<class... A> void init( ::string const&   name   , bool   writable   , A&&... hdr_args ) requires( HasFile) {
			Base::init( name , HasHdr*sizeof(HdrNv) + HasData*sizeof(DataNv)*(size_t(1)<<NBits<Idx>) , writable ) ;
			if (!Base::empty()) return ;
			SWEAR(writable) ;
			_alloc_hdr(::forward<A>(hdr_args)...) ;
		}
		// accesses
	public :
		Sz            size (       ) const requires(HasFile) { return _struct_hdr().sz ; }
		bool          empty(       ) const                   { return size()==1        ; }
		HdrNv  const& hdr  (       ) const requires(HasHdr ) {                   return _struct_hdr().hdr                                  ; }
		HdrNv       & hdr  (       )       requires(HasHdr ) { SWEAR(writable) ; return _struct_hdr().hdr                                  ; }
		HdrNv  const& c_hdr(       ) const requires(HasHdr ) {                   return _struct_hdr().hdr                                  ; }
		DataNv const& at   (Idx idx) const requires(HasData) {                   return *reinterpret_cast<Data const*>(base+_offset(+idx)) ; }
		DataNv      & at   (Idx idx)       requires(HasData) { SWEAR(writable) ; return *reinterpret_cast<Data      *>(base+_offset(+idx)) ; }
		DataNv const& c_at (Idx idx) const requires(HasData) {                   return *reinterpret_cast<Data const*>(base+_offset(+idx)) ; }
		void          clear(Idx idx)       requires(HasData) { if (!idx) return ; at(idx) = Data() ; }
	private :
		StructHdr const& _struct_hdr() const requires(HasFile) {                   return *reinterpret_cast<StructHdr const*>(base) ; }
		StructHdr      & _struct_hdr()       requires(HasFile) { SWEAR(writable) ; return *reinterpret_cast<StructHdr      *>(base) ; }
		Sz             & _size      ()       requires(HasFile) { SWEAR(writable) ; return _struct_hdr().sz                          ; }
		// services
	public :
		/**/                 void pop         ( Idx idx               ) requires(           HasData ) { if (+idx) at(idx).~Data() ;                      }
		template<class... A> void emplace     ( Idx idx , A&&... args ) requires(           HasData ) { new(&at(idx)) Data{::forward<A>(args)...} ;      }
		template<class... A> Idx  emplace_back( Sz  sz  , A&&... args ) requires(  Multi            ) { return _emplace_back(sz,::forward<A>(args)...) ; }
		template<class... A> Idx  emplace_back(           A&&... args ) requires( !Multi && HasData ) { return _emplace_back(1 ,::forward<A>(args)...) ; }
		void clear() {
			ULock lock{_mutex} ;
			Base::clear(sizeof(StructHdr)) ;
			_size() = 1 ;
		}
		void chk() const requires(HasFile) {
			SLock lock{_mutex} ;
			Base::chk() ;
			SWEAR( _offset(size())<=Base::size ) ;
		}
	private :
		void _chk_sz( Idx   idx   , Sz   sz   ) requires(   HasDataSz && Multi  ) { SWEAR(sz==Idx(_at(idx).n_items())) ; }
		void _chk_sz( Idx /*idx*/ , Sz /*sz*/ ) requires(!( HasDataSz && Multi )) {                                      }
		template<class... A> Idx _emplace_back( Sz sz , A&&... args ) requires(HasData) {
			Sz old_sz ;
			Sz new_sz ;
			{	ULock lock{_mutex} ;
				old_sz = size()      ;
				new_sz = old_sz + sz ;
				Base::expand(_offset(new_sz)) ;
				fence() ;                               // update state when it is legal to do so
				_size() = new_sz ;                      // once allocation is done, no reason to maintain lock
			}
			swear(old_sz+1>old_sz,"index overflow on ",name) ;
			Idx res{old_sz} ;
			new(&at(res)) Data(::forward<A>(args)...) ;
			_chk_sz(res,sz) ;
			return res ;
		}
	} ;

}
