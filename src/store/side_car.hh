// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "alloc.hh"

namespace Store {

	template< char ThreadKey , class Hdr_ , IsIdx Idx_ , uint8_t NIdxBits, class Data_ , class SideCar_ , size_t LinearSz=0 > struct SideCarFile
	:	             AllocFile< ThreadKey , Hdr_ , Idx_ , NIdxBits , Data_ , LinearSz >
	{	using Base = AllocFile< ThreadKey , Hdr_ , Idx_ , NIdxBits , Data_ , LinearSz > ;
		//
		using Hdr       = Hdr_              ;
		using Idx       = Idx_              ;
		using Data      = Data_             ;
		using SideCar   = SideCar_          ;
		using DataNv    = NoVoid<Data   >   ;
		using SideCarNv = NoVoid<SideCar>   ;
		using Sz        = typename Base::Sz ;
		//
		static constexpr bool Multi = LinearSz ;
		//
		using Base::HasDataSz ;
		using Base::size      ;
		// cxtors
		/**/                 SideCarFile(                                                        ) = default ;
		template<class... A> SideCarFile( NewType                              , A&&... hdr_args ) { init(New          ,::forward<A>(hdr_args)...) ; }
		template<class... A> SideCarFile( ::string const& name , bool writable , A&&... hdr_args ) { init(name,writable,::forward<A>(hdr_args)...) ; }
		template<class... A> void init( NewType , A&&... hdr_args ) {
			Base::    init(New,::forward<A>(hdr_args)...) ;
			_side_car.init(New                          ) ;
		}
		template<class... A> void init( ::string const& name , bool writable , A&&... hdr_args ) {
			Base::    init(name+".data"    ,writable,::forward<A>(hdr_args)...) ;
			_side_car.init(name+".side_car",writable                          ) ;
			_fix_side_car() ;                                                     // in case of crash between main and side_car expand
		}
		void _fix_side_car() requires( Multi) { Sz scs = _side_car.size() ; SWEAR(scs<=size()) ; if (scs<size()) {                        _side_car.emplace_back(size()-scs) ; } }
		void _fix_side_car() requires(!Multi) { Sz scs = _side_car.size() ; SWEAR(scs<=size()) ; if (scs<size()) { SWEAR(size()-scs==1) ; _side_car.emplace_back(          ) ; } }
		// accesses
		DataNv    const& at          (Idx              idx) const { return Base::at     (idx) ; }
		DataNv         & at          (Idx              idx)       { return Base::at     (idx) ; }
		DataNv    const& c_at        (Idx              idx) const { return Base::at     (idx) ; }
		Idx              idx         (DataNv    const& at ) const { return Base::idx    (at ) ; }
		SideCarNv const& side_car    (Idx              idx) const { return _side_car.at (idx) ; }
		SideCarNv      & side_car    (Idx              idx)       { return _side_car.at (idx) ; }
		SideCarNv const& c_side_car  (Idx              idx) const { return _side_car.at (idx) ; }
		Idx              side_car_idx(SideCarNv const& at ) const { return _side_car.idx(at ) ; }
		// services
		template<class... A> Idx emplace( Sz sz , A&&... args ) requires( Multi) { return _cxt_side_car(sz,Base::emplace(sz,::forward<A>(args)...)) ; }
		template<class... A> Idx emplace(         A&&... args ) requires(!Multi) { return _cxt_side_car(   Base::emplace(   ::forward<A>(args)...)) ; }
		//
		void shorten( Idx idx , Sz old_sz , Sz new_sz ) requires( Multi && !HasDataSz ) { return Base::shorten(idx,old_sz,new_sz)  ; }
		void shorten( Idx idx , Sz old_sz , Sz new_sz ) requires( Multi &&  HasDataSz ) { return Base::shorten(idx,old_sz,new_sz)  ; }
		void shorten( Idx idx , Sz old_sz             ) requires( Multi &&  HasDataSz ) { return Base::shorten(idx,old_sz       )  ; }
		//
		/**/ void pop  ( Idx idx , Idx sz ) requires(  Multi              ) { Base::pop(_dxt_side_car(idx),sz) ;        }
		/**/ void pop  ( Idx idx          ) requires(  Multi && HasDataSz ) { Base::pop(_dxt_side_car(idx)   ) ;        }
		/**/ void pop  ( Idx idx          ) requires( !Multi              ) { Base::pop(_dxt_side_car(idx)   ) ;        }
		/**/ void clear( Idx idx          )                                 { Base::clear(idx) ; _side_car.clear(idx) ; }
		/**/ void clear(                  )                                 { Base::clear(   ) ; _side_car.clear(   ) ; }
		//
		/**/ void chk() const {
			Base::chk() ;
			throw_unless( size()==_side_car.size() , "side_car size differs from main size" ) ;
		}
	private :
		bool _at_end      (         Idx idx ) const { SWEAR( idx<=_side_car.size() , idx , _side_car.size() ) ; return idx==_side_car.size() ; }
		Idx  _cxt_side_car( Sz sz , Idx idx )       { if (_at_end(idx)) _side_car.emplace_back(sz) ; else _side_car.emplace(idx) ; return idx ;    }
		Idx  _cxt_side_car(         Idx idx )       { if (_at_end(idx)) _side_car.emplace_back(  ) ; else _side_car.emplace(idx) ; return idx ;    }
		Idx  _dxt_side_car(         Idx idx )       { SWEAR(!_at_end(idx)) ;                              _side_car.pop    (idx) ; return idx ;    }
		// data
		StructFile< ThreadKey , void , Idx , NIdxBits , SideCar , Multi > _side_car ;
	} ;

}
