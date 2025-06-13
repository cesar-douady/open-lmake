// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "file.hh"

namespace Store {

	namespace Struct {

		template<class Hdr_,class Idx,class Data> struct Hdr {
			using HdrNv  = NoVoid<Hdr_> ;
			using Sz     = UintIdx<Idx> ;
			using DataNv = NoVoid<Data> ;
			// cxtors & casts
			template<class... A> Hdr(A&&... args) : hdr{::forward<A>(args)...} {}
			// data
			// we need to force alignment for subsequent data
			// ideally we would like to put the alignas constraints on the type, but this does not seem to be allowed (and does not work)
			// also, putting a constraint less than the natural constraint is undefined behavior
			// so the idea is to put the alignment constraint on the first item (minimal room lost) and to also put the natural alignment at as constraint
			[[no_unique_address]] alignas(DataNv) alignas(HdrNv) HdrNv hdr ;     // no need to allocate space if header is empty
			[[                 ]]                                Sz    sz  = 1 ; // logical size, i.e. first non-allocated idx ==> account for unused idx 0
		} ;

		template<class Hdr_,class Idx,class Data> static constexpr size_t _offset(size_t idx) {
			using DataNv =NoVoid<Data> ;
			constexpr size_t Offset0 = sizeof(Hdr<Hdr_,Idx,Data>)-sizeof(DataNv) ; // unsigned types handle negative values modulo 2^n, which is ok
			return Offset0 + sizeof(DataNv)*idx ;
		}

	}

	template<bool AutoLock,class Hdr_,IsIdx Idx_,uint8_t NIdxBits,class Data_,bool Multi=false> struct StructFile
	:	               File<false/*AutoLock*/,Struct::_offset<Hdr_,Idx_,Data_>(size_t(1)<<NIdxBits)>              // we manage the mutex
	{	using Base   = File<false/*AutoLock*/,Struct::_offset<Hdr_,Idx_,Data_>(size_t(1)<<NIdxBits)> ;
		using Hdr    = Hdr_                 ;
		using Idx    = Idx_                 ;
		using Data   = Data_                ;
		using HdrNv  = NoVoid<Hdr >         ;
		using DataNv = NoVoid<Data>         ;
		using Sz     = UintIdx<Idx>         ;
		using ULock  = UniqueLock<AutoLock> ;
		//
		static constexpr bool HasHdr    = !::is_void_v<Hdr >       ;
		static constexpr bool HasData   = !::is_void_v<Data>       ;
		static constexpr bool HasDataSz = ::Store::HasDataSz<Data> ;
		static constexpr bool HasFile   = HasHdr || HasData        ;
		//
		static_assert( !Multi || HasData         ) ;
		static_assert( HasData == bool(NIdxBits) ) ;
		//
		using StructHdr = Struct::Hdr<Hdr,Idx,Data> ;
		void expand(size_t) = delete ;
		//
		using Base::chk_writable ;
		using Base::base         ;
		using Base::name         ;
		using Base::writable     ;
		// statics
	private :
		static constexpr size_t _Offset0 = Struct::_offset<Hdr,Idx,Data>(0) ;
		static constexpr size_t _s_offset(Sz idx) requires(HasFile) { SWEAR(idx) ; return Struct::_offset<Hdr,Idx,Data>(idx) ; }
		// cxtors & casts
		template<class... A> void _alloc_hdr(A&&... hdr_args) requires(HasFile) {
			Base::expand(_s_offset(1)) ;                                                                  // 1 is the first used idx
			new(&_struct_hdr()) StructHdr{::forward<A>(hdr_args)...} ;
		}
	public :
		/**/                 StructFile(                                                        )                    = default ;
		/**/                 StructFile( NewType                                                ) requires(!HasFile) { init( New                                         ) ; }
		template<class... A> StructFile( NewType                              , A&&... hdr_args ) requires( HasFile) { init( New             , ::forward<A>(hdr_args)... ) ; }
		/**/                 StructFile( ::string const& name , bool writable                   ) requires(!HasFile) { init( name , writable                             ) ; }
		template<class... A> StructFile( ::string const& name , bool writable , A&&... hdr_args ) requires( HasFile) { init( name , writable , ::forward<A>(hdr_args)... ) ; }
		//
		/**/                 void init( NewType                                                        ) requires(!HasFile) { init( "" , true                             ) ; }
		template<class... A> void init( NewType                                      , A&&... hdr_args ) requires( HasFile) { init( "" , true , ::forward<A>(hdr_args)... ) ; }
		/**/                 void init( ::string const& /*name*/ , bool /*writable*/                   ) requires(!HasFile) {}
		template<class... A> void init( ::string const&   name   , bool   writable   , A&&... hdr_args ) requires( HasFile) {
			Base::init( name , writable ) ;
			if (Base::operator+()) return ;
			throw_unless( writable , "cannot init read-only file ",name ) ;
			_alloc_hdr(::forward<A>(hdr_args)...) ;
		}
		// accesses
		bool          operator+(                 ) const                   { return size()>1                                                        ; }
		Sz            size     (                 ) const requires(HasFile) { return _struct_hdr().sz                                                ; }
		HdrNv  const& hdr      (                 ) const requires(HasHdr ) { return _struct_hdr().hdr                                               ; }
		HdrNv       & hdr      (                 )       requires(HasHdr ) { return _struct_hdr().hdr                                               ; }
		HdrNv  const& c_hdr    (                 ) const requires(HasHdr ) { return _struct_hdr().hdr                                               ; }
		DataNv const& at       (Idx           idx) const requires(HasData) { return *::launder(reinterpret_cast<Data const*>(base+_s_offset(+idx))) ; }
		DataNv      & at       (Idx           idx)       requires(HasData) { return *::launder(reinterpret_cast<Data      *>(base+_s_offset(+idx))) ; }
		DataNv const& c_at     (Idx           idx) const requires(HasData) { return *::launder(reinterpret_cast<Data const*>(base+_s_offset(+idx))) ; }
		Idx           idx      (DataNv const& at ) const requires(HasData) { return Idx(&at-reinterpret_cast<Data const*>(base+_Offset0))           ; }
		void          clear    (Idx           idx)       requires(HasData) { if (!idx) return ; else at(idx) = {} ;                                   }
	private :
		StructHdr const& _struct_hdr() const requires(HasFile) { return *::launder(reinterpret_cast<StructHdr const*>(base)) ; }
		StructHdr      & _struct_hdr()       requires(HasFile) { return *::launder(reinterpret_cast<StructHdr      *>(base)) ; }
		Sz             & _size      ()       requires(HasFile) { return _struct_hdr().sz                                     ; }
		// services
	public :
		/**/                 void pop         ( Idx idx               ) requires(           HasData ) { chk_writable() ; if (+idx) at(idx).~Data() ;                      }
		template<class... A> void emplace     ( Idx idx , A&&... args ) requires(           HasData ) { chk_writable() ; new(&at(idx)) Data{::forward<A>(args)...} ;      }
		template<class... A> Idx  emplace_back( Sz  sz  , A&&... args ) requires(  Multi            ) { chk_writable() ; return _emplace_back(sz,::forward<A>(args)...) ; }
		template<class... A> Idx  emplace_back(           A&&... args ) requires( !Multi && HasData ) { chk_writable() ; return _emplace_back(1 ,::forward<A>(args)...) ; }
		void clear() {
			Base::clear() ;                                                                               // Base::clear has an argument with a default value, this forces clear to accept no argument
		}
		void chk() const requires(HasFile) {
			Base::chk() ;
			throw_unless( size()                        , "incoherent size info"                      ) ; // size is 1 for an empty file
			throw_unless( _s_offset(size())<=Base::size , "logical size is larger than physical size" ) ;
		}
	protected :
		void _clear() {
			Base::_clear(sizeof(StructHdr)) ;
			_size() = 1 ;
		}
	private :
		void _chk_sz( Idx   idx   , Sz   sz   ) requires(   HasDataSz && Multi  ) { SWEAR( sz==Idx(_at(idx).n_items()) , sz , _at(idx).n_items() ) ; }
		void _chk_sz( Idx /*idx*/ , Sz /*sz*/ ) requires(!( HasDataSz && Multi )) {                                                                  }
		//
		template<class... A> Idx _emplace_back( Sz sz , A&&... args ) requires(HasData) {
			Sz old_sz ;
			Sz new_sz ;
			{	ULock lock{_mutex} ;
				old_sz = size()      ;
				new_sz = old_sz + sz ;
				swear( new_sz>=old_sz && new_sz<(size_t(1)<<NIdxBits) ,"index overflow on ",name) ;       // ensure no arithmetic overflow before checking capacity
				Base::expand(_s_offset(new_sz)) ;
				fence() ;                                                                                 // update state when it is legal to do so
				_size() = new_sz ;                                                                        // once allocation is done, no reason to maintain lock
			}
			Idx res { old_sz } ;
			new(&at(res)) Data{::forward<A>(args)...} ;
			_chk_sz(res,sz) ;
			return res ;
		}
		// data
	private :
		UniqueMutex<AutoLock> mutable _mutex ;
	} ;

}
