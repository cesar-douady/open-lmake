// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "alloc.hh"

namespace Store {

	template< bool AutoLock , class Hdr_ , IsIdx Idx_ , class Data_ , class SideCar_ , size_t LinearSz=0 > struct SideCarFile
	:	             AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , NoVoid<Data_,SideCar_> , LinearSz >
	{	using Base = AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , NoVoid<Data_,SideCar_> , LinearSz > ;
		//
		using Hdr       = Hdr_                 ;
		using Idx       = Idx_                 ;
		using Data      = Data_                ;
		using SideCar   = SideCar_             ;
		using DataNv    = NoVoid<Data   >      ;
		using SideCarNv = NoVoid<SideCar>      ;
		using Sz        = typename Base::Sz    ;
		using ULock     = UniqueLock<AutoLock> ;
		using SLock     = SharedLock<AutoLock> ;
		//
		static constexpr bool Multi          = LinearSz                ;
		static constexpr bool HasData        = !::is_void_v<Data   >   ;
		static constexpr bool HasSideCar     = !::is_void_v<SideCar>   ;
		static constexpr bool HasDataOnly    =  HasData && !HasSideCar ;
		static constexpr bool HasSideCarOnly = !HasData &&  HasSideCar ;
		static constexpr bool HasOne         =  HasData !=  HasSideCar ;
		static constexpr bool HasBoth        =  HasData &&  HasSideCar ;
		//
		using Base::HasDataSz ;
		using Base::size      ;
		using Base::clear     ;
		using Base::_mutex    ;
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
		void _fix_side_car() requires( !HasBoth           ) {                                                                                                                                  }
		void _fix_side_car() requires(  HasBoth &&  Multi ) { Sz scs = _side_car.size() ; SWEAR(scs<=size()) ; if (scs<size()) {                        _side_car.emplace_back(size()-scs) ; } }
		void _fix_side_car() requires(  HasBoth && !Multi ) { Sz scs = _side_car.size() ; SWEAR(scs<=size()) ; if (scs<size()) { SWEAR(size()-scs==1) ; _side_car.emplace_back(          ) ; } }
		// accesses
		DataNv    const& at          (Idx              idx) const requires(HasData       ) { return Base::at     (idx) ; } // suppress method if there is no data
		DataNv         & at          (Idx              idx)       requires(HasData       ) { return Base::at     (idx) ; } // .
		DataNv    const& c_at        (Idx              idx) const requires(HasData       ) { return Base::at     (idx) ; } // .
		Idx              idx         (DataNv    const& at ) const requires(HasData       ) { return Base::idx    (at ) ; } // .
		SideCarNv const& side_car    (Idx              idx) const requires(HasBoth       ) { return _side_car.at (idx) ; }
		SideCarNv      & side_car    (Idx              idx)       requires(HasBoth       ) { return _side_car.at (idx) ; }
		SideCarNv const& c_side_car  (Idx              idx) const requires(HasBoth       ) { return _side_car.at (idx) ; }
		Idx              side_car_idx(SideCarNv const& at ) const requires(HasBoth       ) { return _side_car.idx(at ) ; }
		SideCarNv const& side_car    (Idx              idx) const requires(HasSideCarOnly) { return Base::at     (idx) ; }
		SideCarNv      & side_car    (Idx              idx)       requires(HasSideCarOnly) { return Base::at     (idx) ; }
		SideCarNv const& c_side_car  (Idx              idx) const requires(HasSideCarOnly) { return Base::at     (idx) ; }
		Idx              side_car_idx(SideCarNv const& at ) const requires(HasSideCarOnly) { return Base::idx    (at ) ; }
		// services
		template<class... A> Idx emplace( Sz sz , A&&... args ) requires( HasDataOnly    &&  Multi ) { ULock lock{_mutex} ; return                  Base::emplace(sz,::forward<A>(args)...)  ; }
		template<class... A> Idx emplace(         A&&... args ) requires( HasDataOnly    && !Multi ) { ULock lock{_mutex} ; return                  Base::emplace(   ::forward<A>(args)...)  ; }
		template<class... A> Idx emplace( Sz sz , A&&... args ) requires( HasBoth        &&  Multi ) { ULock lock{_mutex} ; return _cxt_side_car(sz,Base::emplace(sz,::forward<A>(args)...)) ; }
		template<class... A> Idx emplace(         A&&... args ) requires( HasBoth        && !Multi ) { ULock lock{_mutex} ; return _cxt_side_car(   Base::emplace(   ::forward<A>(args)...)) ; }
		/**/                 Idx emplace( Sz sz               ) requires( HasSideCarOnly &&  Multi ) { ULock lock{_mutex} ; return                  Base::emplace(sz                      )  ; }
		/**/                 Idx emplace(                     ) requires( HasSideCarOnly && !Multi ) { ULock lock{_mutex} ; return                  Base::emplace(                        )  ; }
		//
		void shorten( Idx idx , Sz old_sz , Sz new_sz ) requires(  Multi && !HasDataSz ) { ULock lock{_mutex} ; return Base::shorten(idx,old_sz,new_sz)  ; }
		void shorten( Idx idx , Sz old_sz , Sz new_sz ) requires(  Multi &&  HasDataSz ) { ULock lock{_mutex} ; return Base::shorten(idx,old_sz,new_sz)  ; }
		void shorten( Idx idx , Sz old_sz             ) requires(  Multi &&  HasDataSz ) { ULock lock{_mutex} ; return Base::shorten(idx,old_sz       )  ; }
		//
		/**/ void pop  ( Idx idx , Idx sz ) requires( HasOne  &&  Multi              ) { ULock lock{_mutex} ; Base::pop(              idx ,sz) ;        }
		/**/ void pop  ( Idx idx          ) requires( HasOne  &&  Multi && HasDataSz ) { ULock lock{_mutex} ; Base::pop(              idx    ) ;        }
		/**/ void pop  ( Idx idx          ) requires( HasOne  && !Multi              ) { ULock lock{_mutex} ; Base::pop(              idx    ) ;        }
		/**/ void pop  ( Idx idx , Idx sz ) requires( HasBoth &&  Multi              ) { ULock lock{_mutex} ; Base::pop(_dxt_side_car(idx),sz) ;        }
		/**/ void pop  ( Idx idx          ) requires( HasBoth &&  Multi && HasDataSz ) { ULock lock{_mutex} ; Base::pop(_dxt_side_car(idx)   ) ;        }
		/**/ void pop  ( Idx idx          ) requires( HasBoth && !Multi              ) { ULock lock{_mutex} ; Base::pop(_dxt_side_car(idx)   ) ;        }
		/**/ void clear( Idx idx          ) requires( HasBoth                        ) { ULock lock{_mutex} ; Base::clear(idx) ; _side_car.clear(idx) ; }
		/**/ void clear(                  ) requires( HasBoth                        ) { ULock lock{_mutex} ; Base::clear(   ) ; _side_car.clear(   ) ; }
		//
		/**/ void chk() const {
			SLock lock{_mutex} ;
			Base::chk() ;
			SWEAR( size()==_side_car.size() , size() , _side_car.size() ) ;
		}
	private :
		bool _at_end      (         Idx idx ) const requires(HasBoth) { SWEAR( idx<=_side_car.size() , idx , _side_car.size() ) ; return idx==_side_car.size() ; }
		Idx  _cxt_side_car( Sz sz , Idx idx )       requires(HasBoth) { if (_at_end(idx)) _side_car.emplace_back(sz) ; else _side_car.emplace(idx) ; return idx ;    }
		Idx  _cxt_side_car(         Idx idx )       requires(HasBoth) { if (_at_end(idx)) _side_car.emplace_back(  ) ; else _side_car.emplace(idx) ; return idx ;    }
		Idx  _dxt_side_car(         Idx idx )       requires(HasBoth) { SWEAR(!_at_end(idx)) ;                              _side_car.pop    (idx) ; return idx ;    }
		// data
		StructFile< false/*AutoLock*/ , void , Idx , ::conditional_t<HasBoth,SideCar,void> , (HasBoth?Multi:false) > _side_car ;
	} ;

}
