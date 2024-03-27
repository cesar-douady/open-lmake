// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "alloc.hh"

ENUM( ItemKind , Terminal, Prefix , Split )

namespace Store {

	//
	// PrefixFile
	//
	// This is an efficent coding of prefix trees (or tries).
	// Each item contains a header and a chunk of data
	// No reconvergence, i.e. idx is unique for a full name.
	// A used bit is set for items corresponding to inserted prefixes, which implies :
	// - it can be returned by at
	// - it is frozen, it cannot be moved
	// - it must have a non empty chunk
	// Chunks of data stored in items are stored in reversed order as this significantly reduces the numbers of copies
	// invariants can be found in the chk() method

	namespace Prefix {

		template<       class Char> using Vec      = ::vector           <       Char >         ;
		template<       class Char> using VecView  = ::c_vector_view    <       Char >         ;
		template<       class Char> using Str      = ::basic_string     <AsChar<Char>>         ;
		template<       class Char> using StrView  = ::basic_string_view<AsChar<Char>>         ;
		template<bool S,class Char> using VecStr   = ::conditional_t<S,Str   <Char>,Vec<Char>> ;
		template<bool S,class Char> using ItemChar = ::conditional_t<S,AsChar<Char>,    Char > ;
		//
		template<class Char> void append( ::vector             <Char> & res , Char const* from , size_t sz ) { for( Char const& c : ::c_vector_view<Char>(from,sz) ) res.push_back(c) ; }
		template<class Char> void append( ::basic_string<AsChar<Char>>& res , Char const* from , size_t sz ) { res.append(from,sz) ;                                                    }

		template<bool Reverse,class Char> Char const& char_at( VecView<Char> const& name , size_t pos ) {
			return name[ Reverse ? name.size()-1-pos : pos ] ;
		}
		template<bool Reverse,class Char> Char const& char_at( VecView<Char> const& name , VecView<Char> const& psfx , size_t pos ) {
			return pos<name.size() ? char_at<Reverse>(name,pos) : char_at<Reverse>(psfx,pos-name.size()) ;
		}
		template<class Char> size_t size( VecView<Char> const& name , VecView<Char> const& psfx ) {
			return name.size() + psfx.size() ;
		}

		// try to be as large as possible as accepted Char
		template<class Char> struct CharRep ;
		template<class Char> static constexpr bool Case1 = ::is_arithmetic_v<Char> || ::is_enum_v<Char> ;
		template<class Char> requires(::is_arithmetic_v<Char>) struct CharRep<Char> {
			using CharUint = ::make_unsigned_t<Char> ;
			static CharUint s_rep(Char c) { return CharUint(c) ; }
		} ;
		template<class Char> requires(::is_enum_v<Char>) struct CharRep<Char> {
			using CharUint = ::make_unsigned_t<::underlying_type_t<Char>> ;
			static CharUint s_rep(Char c) { return CharUint(c) ; }
		} ;
		template<class Char> static constexpr bool Case2 = ::has_unique_object_representations_v<Char> && sizeof(Char)<=8 ;
		template<class Char> requires( !Case1<Char> && Case2<Char> ) struct CharRep<Char> {
			using CharUint = Uint<NBits<Char>> ;
			static CharUint s_rep(Char c) {
				CharUint r = 0 ;
				::memcpy(&r,&c,sizeof(c)) ;
				return r ;
			}
		} ;
		template<class Char> concept HasPlus = requires(Char c) { {+c} -> ::integral ; } ;
		template<class Char> requires( !Case1<Char> && !Case2<Char> && HasPlus<Char> ) struct CharRep<Char> {
			using CharUint = ::make_unsigned_t<decltype(+Char())> ;
			static_assert(sizeof(CharUint)==sizeof(Char)) ;
			static CharUint s_rep(Char c) { return CharUint(+c) ; }
		} ;
		template<class Char> using CharUint = typename CharRep<Char>::CharUint ;
		template<class Char> CharUint<Char> rep(Char c) { return CharRep<Char>::s_rep(c) ; }

		struct Nxt {
			Nxt(ItemKind k) : val(2-+k) {}
			uint8_t val ;
		} ;
		struct KindIterator {
			// cxtors & casts
			KindIterator(Nxt n) : val{n.val} {}
			// services
			bool operator== (KindIterator const& other) const = default ;
			bool operator*  (                         ) const { SWEAR(val!=2) ; return val   ; }
			void operator++ (                         )       {                        val++ ; }
			// data
			uint8_t val ;
		} ;
		inline KindIterator begin(Nxt n) { return n                       ; }
		inline KindIterator end  (Nxt  ) { return Nxt(ItemKind::Terminal) ; }

		template<class Idx,class Char> struct ItemBase {
			static_assert(IsTrivial<Char>) ;
			using CharUint = Prefix::CharUint<Char> ;
			using ChunkIdx = uint8_t                ;
			using ItemOfs  = uint32_t               ;
			using ChunkBit = uint8_t                ;
			using Sz       = uint8_t                ;
			static constexpr uint8_t  CharSizeOf    = sizeof(CharUint)                                              ;
			static constexpr uint8_t  LogSizeOfChar = CharSizeOf==1 ? 0 : CharSizeOf==2 ? 1 : CharSizeOf<=4 ? 2 : 3 ;
			static constexpr ChunkIdx MaxChunkSz    = lsb_msk(7-LogSizeOfChar)                                      ;
			static constexpr ItemOfs  ChunkOfs      = round_up( round_up(sizeof(Idx),2)+2 , alignof(Char) )         ; // cannot find a way to rely on compiler
			static constexpr Sz       MaxSz         = 4                                                             ; // number of ItemSizeOf in the largest Item
			// cxtors
			ItemBase() = default ;
			ItemBase( Sz sz_ , ItemKind kind_ , bool used_ , ChunkIdx chunk_sz , size_t cmp_bit_=0 ) :
				_sz1    (sz_-1   )
			,	_kind   (+kind_  )
			,	used    (used_   )
			,	cmp_bit (cmp_bit_)
			,	chunk_sz(chunk_sz)
			{	SWEAR(sz_>=1) ;
			}
			// accesses
			Sz       sz     (          ) const { return _sz1+1 ;                }
			void     sz     (Sz sz_    )       { SWEAR(sz_>=1) ; _sz1 = sz_-1 ; }
			Sz       n_items(          ) const { return sz() ;                  }
			ItemKind kind   (          ) const { return ItemKind(_kind) ;       }
			void     kind   (ItemKind k)       { _kind = +k ;                   }
			// data
			Idx      prev                        {}  ;
		private :
			uint16_t _sz1       :2               ;                                                                    // actual sz-1, counted in ItemSizeOf
			uint16_t _kind      :2               ;
		public :
			uint16_t used       :1               = 0 ;
			uint16_t cmp_bit    :LogSizeOfChar+3 = 0 ;                                                                // bits up to cmp_bit must match, and this bit indexes nxt to get next item
			uint16_t chunk_sz   :7-LogSizeOfChar ;
			uint16_t prev_is_eq :1               = 1 ;                                                                // false if prev is Split and points to use through its nxt_if(false) side
			// Char     chunk[whatever_fits] ;                                                                        // in reverse order so that adding or suppressing a prefix requires no copy
			// CharUint cmp_val              ;                                                                        // if Split
			// Idx      nxt  [n_nxt(kind())] ;                                                                        // if Split, indexed by is_eq
			// Data     data ?               ;                                                                        // if used, data after or before nxt depending on which alignment is highest
		} ;

		template<class Idx,class Char,class Data=void,bool Reverse=false> struct Item
		:	             ItemBase<Idx,Char>
		{	using Base = ItemBase<Idx,Char> ;
			enum class Dvg  : uint8_t { Cont , Dvg , Long , Match , Short , Unused } ;
			using Kind     = ItemKind                ;
			using Nxt      = Prefix::Nxt             ;
			using VecView  = Prefix::VecView<Char>   ;
			using CharUint = typename Base::CharUint ;
			using ChunkBit = typename Base::ChunkBit ;
			using ChunkIdx = typename Base::ChunkIdx ;
			using ItemOfs  = typename Base::ItemOfs  ;
			using Sz       = typename Base::Sz       ;
			using DataNv   = NoVoid<Data>            ;

			using Base::CharSizeOf ;
			using Base::MaxChunkSz ;
			using Base::MaxSz      ;
			using Base::prev       ;
			using Base::prev_is_eq ;
			using Base::sz         ;
			using Base::used       ;
			using Base::cmp_bit    ;
			using Base::ChunkOfs   ;
			using Base::chunk_sz   ;
			using Base::kind       ;

			static constexpr bool    HasData             = !is_void_v<Data>                                                  ;
			static constexpr ItemOfs DataSizeOf          = HasData ? sizeof(DataNv) : 0                                      ;
			static constexpr bool    BigData             = alignof(DataNv)>alignof(Idx)                                      ;
			static constexpr ItemOfs MinSplitSizeOf      = round_up(ChunkOfs,alignof(CharUint)) + CharSizeOf + sizeof(Idx)*2 ;
			static constexpr ItemOfs MinPrefixSizeOf     =          ChunkOfs                    + CharSizeOf + sizeof(Idx)   ; // Prefix items (unless root) must have >=1 Char
			static constexpr ItemOfs MinUsedPrefixSizeOf = MinPrefixSizeOf + DataSizeOf                                      ; // used items must have >=1 Char
			static constexpr ItemOfs MinUsedSplitSizeOf  = MinSplitSizeOf  + DataSizeOf + CharSizeOf                         ; // .
			static constexpr ItemOfs ItemSizeOf =                                                                              // number of bytes in the smallest Item
				round_up(
					::max({
						ItemOfs(MinSplitSizeOf)                                                                                // empty Split items are typical small items
					,	ItemOfs(sizeof(Idx)*4)                                                                                 // a reasonable value
					,	div_up(MinUsedSplitSizeOf,MaxSz)                                                                       // ensure we have room to represent needed items in a maximum sized item
					})
				,	::max({ alignof(Base) , alignof(Char) , alignof(CharUint) , alignof(Idx) , alignof(DataNv) })
				)
			;
			static constexpr Sz   MinUsedSz = div_up( MinUsedSplitSizeOf  , ItemSizeOf )             ;                         // if used, we must be able to transform into a Split w/o moving
			static constexpr bool NeedSzChk = ChunkOfs + MaxChunkSz*sizeof(Char) <  ItemSizeOf*MaxSz ;                         // largest Item is larger than max representable chunk

			static_assert( ItemSizeOf>sizeof(Base) ) ; // this is not a constraint, it must always hold
			static_assert( DataSizeOf<=(1<<31)     ) ; // this is pretty comfortable, else, we must define ItemOfs as uint64_t

			static ChunkBit s_cmp_bit ( CharUint cmp_val , CharUint dvg_val )       { return ::countl_zero(CharUint(cmp_val^dvg_val))                              ; }
			/**/   bool     dvg_before(                    CharUint dvg_val ) const { return CharUint(cmp_val()^dvg_val) & (msb_msk(NBits<CharUint>-1-cmp_bit)<<1) ; }
			/**/   bool     dvg_at    (                    CharUint dvg_val ) const { return CharUint(cmp_val()^dvg_val) &  bit_msk(NBits<CharUint>-1-cmp_bit)     ; }

			// data
		private :
			[[no_unique_address]] ::array<Char,(ItemSizeOf-ChunkOfs)/sizeof(Char)> _extra ; // ensure underlying AllocFile has adequate quantum

			// cxtors
			void _new_data(                ) requires(!HasData) {                                                                                             }
			void _new_data(                ) requires( HasData) { if (!used) return ; new(&data()) Data{} ;                                                   } // value initialize
			void _del_data(                ) requires(!HasData) {                                                                                             }
			void _del_data(                ) requires( HasData) { if (!used) return ; data().~Data() ;                                                        }
			void _mv_data ( Sz    , Kind   ) requires(!HasData) {                                                                                             }
			void _mv_data ( Sz sz , Kind k ) requires( HasData) { if (!used) return ; new(&_at<Data>(_s_data_ofs(sz,k))) Data{::move(data())} ; _del_data() ; }
			//
			Item( Sz sz , Kind kind_ , bool used_ , ChunkIdx chunk_sz_ , CharUint cmp_val_=0 , ChunkBit cmp_bit_=0 ) :
				Base( sz , kind_ , used_ , chunk_sz_ , cmp_bit_ )
			{	SWEAR( chunk_sz_<=max_chunk_sz() , chunk_sz , max_chunk_sz() ) ;
				if (cmp_val_) SWEAR( kind()==Kind::Split , kind() ) ;
				for( bool is_eq : Nxt(kind_) ) nxt_if(is_eq) = Idx() ;
				if (kind()==Kind::Split) cmp_val() = cmp_val_ ;
				_new_data() ;
			}
			Item(Item const&) = delete ;
		public :
			Item( Sz sz , Kind kind_ , CharUint cmp_val_=0 , ChunkBit cmp_bit_=0 ) :
				Item( sz , kind_ , false/*used*/ , 0/*chunk_sz*/ , cmp_val_ , cmp_bit_ )
			{}
			Item( Sz sz , Kind kind_ , bool used_ , VecView const& name , VecView const& psfx , size_t start , ChunkIdx chunk_sz_ ) :
				Item( sz , kind_ , used_ , chunk_sz_ )
			{	ChunkIdx i ;
				for( i=0 ; i<chunk_sz && start+i<name.size() ; i++ ) chunk(i) = Prefix::char_at<Reverse>(name,start+i            ) ;
				for(     ; i<chunk_sz                        ; i++ ) chunk(i) = Prefix::char_at<Reverse>(psfx,start+i-name.size()) ;
			}
			Item( Sz sz , Kind kind_ , bool used_ , ChunkIdx chunk_sz_ , Item const& from , ChunkIdx start , CharUint cmp_val_=0 , ChunkBit cmp_bit_=0 ) :
				Item(sz,kind_,used_,chunk_sz_,cmp_val_,cmp_bit_)
			{	fill_from( 0 , chunk_sz , from , start ) ;
			}
			void fill_from( ChunkIdx start , ChunkIdx sz , Item const& from , ChunkIdx from_start ) {
				for( ChunkIdx i=0 ; i<sz ; i++ ) chunk(start+i) = from.chunk(from_start+i) ;
			}
			void prepend_from( Item const& from , ChunkIdx start ) {
				chunk_sz += from.chunk_sz-start ;
				fill_from( 0 , from.chunk_sz-start , from , start ) ; // reverse order, prepending is just appending to chunk
			}
			void append_from( Item const& from , ChunkIdx sz ) {
				ChunkIdx prev_sz = chunk_sz ;
				chunk_sz += sz ;
				fill_from( 0       , prev_sz , *this , sz ) ;         // reverse order,mv data as adjusting chunk_sz preserves end of chunk, not start of chunk
				fill_from( prev_sz , sz      , from  , 0  ) ;         // copy over from item
			}
			~Item() { _del_data() ; }
			// accesses
			Char const& chunk(ChunkIdx i) const { return _at<Char const>(ChunkOfs+sizeof(Char)*(chunk_sz-1-i)) ; }                                  // in reverse order
			Char      & chunk(ChunkIdx i)       { return _at<Char      >(ChunkOfs+sizeof(Char)*(chunk_sz-1-i)) ; }                                  // .
			//
			CharUint const& cmp_val(          ) const                   { SWEAR(kind()==Kind::Split       ,kind()      ) ; return _at<CharUint const>(_s_cmp_val_ofs(sz(),used      )) ; }
			CharUint      & cmp_val(          )                         { SWEAR(kind()==Kind::Split       ,kind()      ) ; return _at<CharUint      >(_s_cmp_val_ofs(sz(),used      )) ; }
			DataNv   const& data   (          ) const requires(HasData) { SWEAR(used                                   ) ; return _at<Data     const>(_s_data_ofs   (sz(),kind()    )) ; }
			DataNv        & data   (          )       requires(HasData) { SWEAR(used                                   ) ; return _at<Data          >(_s_data_ofs   (sz(),kind()    )) ; }
			Idx      const& nxt_if (bool is_eq) const                   { SWEAR(is_eq>=*begin(Nxt(kind())),kind(),is_eq) ; return _at<Idx      const>(_s_nxt_if_ofs (sz(),used,is_eq)) ; }
			Idx           & nxt_if (bool is_eq)                         { SWEAR(is_eq>=*begin(Nxt(kind())),kind(),is_eq) ; return _at<Idx           >(_s_nxt_if_ofs (sz(),used,is_eq)) ; }
			Idx      const& nxt    (          ) const                   { SWEAR(kind()==Kind::Prefix      ,kind()      ) ; return nxt_if(true)                                         ; }
			Idx           & nxt    (          )                         { SWEAR(kind()==Kind::Prefix      ,kind()      ) ; return nxt_if(true)                                         ; }
			CharUint dvg_char( ChunkIdx pos , CharUint dvg_val ) const {
				if (!( kind()==Kind::Split && chunk_sz==pos )) return rep(chunk(pos)) ;                                                             // chunk is stored in reverse order
				SWEAR( s_cmp_bit(cmp_val(),dvg_val) < cmp_bit , cmp_val() , dvg_val , cmp_bit ) ;                                                   // cannot diverge past cmp_bit
				return cmp_val() ;
			}
			static constexpr ChunkIdx s_max_chunk_sz( Sz sz , Kind k , bool used ) {
				ItemOfs chunk_end = _s_chunk_end_ofs(sz,k,used) ;
				ChunkIdx chunk_sz = ( chunk_end -  ChunkOfs ) / sizeof(Char) ;
				if (NeedSzChk) return ::min(chunk_sz,MaxChunkSz) ;
				else           return       chunk_sz             ;
			}
			static constexpr ChunkIdx s_max_chunk_sz( Kind k , bool used ) {
				return s_max_chunk_sz(MaxSz,k,used) ;
			}
			static Sz s_min_sz( Kind k , bool used , ChunkIdx chunk_sz ) {
				ChunkIdx max_chunk_sz = s_max_chunk_sz(k,used) ;
				SWEAR( max_chunk_sz>=chunk_sz , max_chunk_sz , chunk_sz ) ;
				Sz min_sz = MaxSz - (max_chunk_sz- chunk_sz)/ItemSizeOf ;
				if ( k==Kind::Terminal && min_sz<MinUsedSz ) return MinUsedSz ;
				else                                         return min_sz    ;
			}
		private :
			template<class T> T      & _at(size_t ofs)       { return *reinterpret_cast<      T*>(reinterpret_cast<char      *>(this)+ofs) ; }
			template<class T> T const& _at(size_t ofs) const { return *reinterpret_cast<const T*>(reinterpret_cast<char const*>(this)+ofs) ; }
			//
			static constexpr ItemOfs _s_end_ofs(Sz sz) { return ItemSizeOf*sz ; }
			static ItemOfs _s_data_ofs( Sz sz , Kind /*k*/ ) requires(BigData) {                                                                    // data is after  nxt
				SWEAR( _s_end_ofs(sz) >= DataSizeOf + ChunkOfs , sz ) ;                                                                             // check no overlap with metadata
				return _s_end_ofs(sz) -  DataSizeOf ;
			}
			static ItemOfs _s_data_ofs( Sz sz , Kind k ) requires(!BigData) {                                                                       // data is before nxt
				SWEAR( _s_end_ofs(sz) >= ( sizeof(Idx)*+k + DataSizeOf ) + ChunkOfs , sz , k ) ;                                                    // check no overlap with metadata
				return _s_end_ofs(sz) -  ( sizeof(Idx)*+k + DataSizeOf ) ;
			}
			static ItemOfs _s_nxt_if_ofs( Sz sz ,  bool u , bool is_eq ) requires(BigData) {                                                        // data is after  nxt
				SWEAR( _s_end_ofs(sz) >= ( DataSizeOf*u + sizeof(Idx)*2 ) + ChunkOfs , sz , u ) ;                                                   // check no overlap with metadata
				return _s_end_ofs(sz) -  ( DataSizeOf*u + sizeof(Idx)*2 ) + sizeof(Idx)*is_eq ;
			}
			static ItemOfs _s_nxt_if_ofs( Sz sz , bool /*u*/ , bool is_eq ) requires(!BigData) {                                                    // data is before nxt
				SWEAR( _s_end_ofs(sz) >= sizeof(Idx)*2 + ChunkOfs , sz ) ;                                                                          // check no overlap with metadata
				return _s_end_ofs(sz) -  sizeof(Idx)*2 + sizeof(Idx)*is_eq ;
			}
			static ItemOfs _s_cmp_val_ofs( Sz sz , bool u ) {
				ItemOfs end_ofs = round_up( DataSizeOf*u + sizeof(Idx)*2 , CharSizeOf ) + CharSizeOf ;
				SWEAR(             _s_end_ofs(sz) >= end_ofs + ChunkOfs , sz ,end_ofs ) ;                                                           // check no overlap with metadata
				return round_down( _s_end_ofs(sz) -  end_ofs , alignof(CharUint) ) ;
			}
			static constexpr ItemOfs _s_chunk_end_ofs( Sz sz , Kind k , bool u ) {
				if (k==Kind::Split) return round_down( _s_cmp_val_ofs(sz,u) , alignof(Char) ) ;
				ItemOfs end_ofs = DataSizeOf*u + sizeof(Idx)*+k ;                                                                                   // space for data, nxt & cmp_val
				SWEAR(             _s_end_ofs(sz) >= end_ofs + ChunkOfs , sz , end_ofs ) ;                                                          // check no overlap with metadata
				return round_down( _s_end_ofs(sz) -  end_ofs , alignof(Char) ) ;                                                                    // align
			}
			static constexpr bool _s_large_enough_empty( Sz sz , Kind k , bool u ) {
				if (!u) return true ;                                                                                                               // an unused Split fits in a minimal item
				ItemOfs at_end = DataSizeOf + sizeof(Idx)*+k ;
				if (k==Kind::Split) at_end  = round_up(at_end,CharSizeOf) + CharSizeOf ;
				return _s_end_ofs(sz) >= ChunkOfs + sizeof(Char) + at_end ;                                                                         // used items must have a non-null chunk
			}
			size_t _data_ofs   () const { SWEAR(used                         ) ; return _s_data_ofs   (sz(),kind()     ) ; }
			size_t _nxt_ofs    () const { SWEAR(kind()!=Kind::Terminal       ) ; return _s_nxt_ofs    (sz(),kind(),used) ; }
			size_t _cmp_val_ofs() const { SWEAR(kind()==Kind::Split   ,kind()) ; return _s_cmp_val_ofs(sz(),       used) ; }
			// services
			void _mk_down( bool keep_is_eq=true) {
				if (!keep_is_eq) _at<Idx>(_s_nxt_if_ofs(sz(),used,true/*is_eq*/)) = nxt_if(false/*is_eq*/) ;
				if (!BigData   ) _mv_data(sz(),kind()-1) ;
				kind(kind()-1) ;
			}
			void _mk_up( CharUint cmp_val_=0 , CharUint dvg_val=0 ) {
				if (!BigData) _mv_data(sz(),kind()+1) ;
				kind(kind()+1) ;
				nxt_if(*begin(Nxt(kind()))) = Idx() ;                                                                                               // init new nxt field
				if (kind()==Kind::Split) {
					cmp_val() = cmp_val_                    ;
					cmp_bit   = s_cmp_bit(cmp_val_,dvg_val) ;
				} else {
					SWEAR( !dvg_val , dvg_val ) ;
				}
				SWEAR( chunk_sz<=max_chunk_sz() , chunk_sz , max_chunk_sz() ) ;
			}
		public :
			bool     may_mk_up_empty() const { return _s_large_enough_empty(sz(),kind()+1,used         ) ; }
			bool     may_use_empty  () const { return _s_large_enough_empty(sz(),kind()  ,true         ) ; }
			ChunkIdx max_chunk_sz   () const { return s_max_chunk_sz       (sz(),kind()  ,used         ) ; }
			Sz       min_sz         () const { return s_min_sz             (     kind()  ,used,chunk_sz) ; }
			//
			void mk_down(bool keep_is_eq                       ) { SWEAR(kind()==Kind::Split   ,kind()) ; SWEAR(!nxt_if(!keep_is_eq)) ; _mk_down(keep_is_eq      ) ; }
			void mk_down(                                      ) { SWEAR(kind()==Kind::Prefix  ,kind()) ; SWEAR(!nxt   (           )) ; _mk_down(                ) ; }
			void mk_up  ( CharUint cmp_val_ , CharUint dvg_val ) { SWEAR(kind()==Kind::Prefix  ,kind()) ;                               _mk_up  (cmp_val_,dvg_val) ; }
			void mk_up  (                                      ) { SWEAR(kind()==Kind::Terminal,kind()) ;                               _mk_up  (                ) ; }
			//
			void mk_used(bool used_) {
				if (used_==used) return ;
				_del_data() ;
				CharUint cmp_val_{} ;
				if (kind()==Kind::Split) cmp_val_ = cmp_val() ;
				if (BigData) {                                                                                                                      // data is after nxt
					if ( kind()==Kind::Split && used ) for( bool is_eq : Nxt(kind()) ) _at<Idx>(_s_nxt_if_ofs(sz(),true,!is_eq)) = nxt_if(!is_eq) ; // walk backward if moving forward
					else                               for( bool is_eq : Nxt(kind()) ) _at<Idx>(_s_nxt_if_ofs(sz(),true, is_eq)) = nxt_if( is_eq) ;
				}
				//vvvvvvvvvv
				used = used_ ;
				//^^^^^^^^^^
				if (kind()==Kind::Split) cmp_val() = cmp_val_ ;
				_new_data() ;
				SWEAR( chunk_sz<=max_chunk_sz() , chunk_sz , max_chunk_sz() ) ;
			}
			bool need_mk_min_sz() {
				SWEAR( min_sz()<=sz() , min_sz() , sz() ) ;
				return min_sz()<sz() ;
			}
			void mk_min_sz() {
				SWEAR(need_mk_min_sz()) ;
				Sz min_sz_ = min_sz() ;
				if (kind()==Kind::Split) _at<CharUint>(_s_cmp_val_ofs(min_sz_,used)) = cmp_val() ;
				if (BigData) {                                                                                                                      // data is after nxt
					for( bool is_eq : Nxt(kind()) ) _at<Idx>(_s_nxt_if_ofs(min_sz_,used,is_eq)) = nxt_if(is_eq) ;
					_mv_data(min_sz_,kind()) ;
				} else {                                                                                                                            // data is before nxt
					_mv_data(min_sz_,kind()) ;
					for( bool is_eq : Nxt(kind()) ) _at<Idx>(_s_nxt_if_ofs(min_sz_,used,is_eq)) = nxt_if(is_eq) ;
				}
				//vvvvvvvvv
				sz(min_sz_) ;
				//^^^^^^^^^
			}
			void shorten_by(ChunkIdx start) {
				SWEAR( start<=chunk_sz , start , chunk_sz ) ;
				chunk_sz -= start ;                           // because chunks are stored in reverse order, suppressing a prefix is merely adjusting the chunk_sz
			}
			// find diverging point and return status. chunk_pos is always updated, idx only if status is Dvg::Cont
			Dvg find_dvg( Idx& idx/*out*/ , ChunkIdx& chunk_pos/*out*/ , VecView const& name , VecView const& psfx , size_t name_pos ) const { // psfx is prefix (Reverse) / suffix (!Reverse)
				size_t total_sz      = Prefix::size(name,psfx) ;
				size_t total_end_pos = ::min( total_sz      , name_pos+chunk_sz ) ;
				size_t name_end_pos  = ::min( total_end_pos , name.size()       ) ;
				for( chunk_pos=0 ; name_pos<name_end_pos  ; chunk_pos++,name_pos++ ) { if ( Prefix::char_at<Reverse>(name,name_pos            ) != chunk(chunk_pos) ) return Dvg::Dvg ; }
				for(             ; name_pos<total_end_pos ; chunk_pos++,name_pos++ ) { if ( Prefix::char_at<Reverse>(psfx,name_pos-name.size()) != chunk(chunk_pos) ) return Dvg::Dvg ; }
				if (chunk_pos< chunk_sz) return Dvg::Short                      ;
				if (name_pos ==total_sz) return used ? Dvg::Match : Dvg::Unused ;
				switch (kind()) {
					case Kind::Terminal :               return Dvg::Long ;
					case Kind::Prefix   : idx = nxt() ; return Dvg::Cont ;
					default : ;
				}
				CharUint dvg_val = Prefix::rep(Prefix::char_at<Reverse>(name,psfx,name_pos)) ;
				if (dvg_before(dvg_val)) return Dvg::Dvg ;
				idx = nxt_if(!dvg_at(dvg_val)) ;
				return Dvg::Cont ;
			}
		} ;

		template<class Idx,class Char,class Data=void,bool Reverse=false> struct SaveItem
		:	              ItemBase<Idx,Char>
		{	using Base  = ItemBase<Idx,Char> ;
			using Item_ = Item<Idx,Char,Data,Reverse> ;
			//
			using Kind     = ItemKind                ;
			using CharUint = typename Base::CharUint ;
			static constexpr bool HasData = Item_::HasData ;
			//
			using Base::kind ;
			using Base::used ;
			// services
			void save(Item_ const& from) {
				static_cast<Base&>(*this) = from ;
				for( bool is_eq : Nxt(kind()) ) _nxt[is_eq] = from.nxt_if (is_eq) ;
				if ( kind()==Kind::Split ) _cmp_val    = from.cmp_val(     ) ;
				_save_data(from) ;
			}
			void restore(Item_& to) const {
				static_cast<Base&>(to) = *this ;
				for( bool is_eq : Nxt(kind()) ) to.nxt_if (is_eq) = _nxt[is_eq] ;
				if ( kind()==Kind::Split ) to.cmp_val(     ) = _cmp_val    ;
				_restore_data(to) ;
			}
		private :
			void _save_data   (Item_ const& /*from*/)       requires(!HasData) {                                     }
			void _save_data   (Item_ const&   from  )       requires( HasData) { if (used) _data     = from.data() ; }
			void _restore_data(Item_      & /*to  */) const requires(!HasData) {                                     }
			void _restore_data(Item_      &   to    ) const requires( HasData) { if (used) to.data() = _data       ; }
			// data
			CharUint     _cmp_val ;
			Idx          _nxt[2]  ;
			NoVoid<Data> _data    ;
		} ;

		template<class H,class I,class C,class D,bool R> struct Hdr {
			using SaveItem_ = SaveItem<I,C,D,R> ;
			using Item_     = Item    <I,C,D,R> ;
			// services
			void commit() {
				fence() ;
				n_saved = 0 ;
			}
			void backup( I idx , Item_ const& item ) {
				SWEAR( n_saved<::size(save) , n_saved , ::size(save) ) ;
				save[n_saved].first = idx ;
				save[n_saved].second.save(item) ;
				fence() ;
				n_saved++ ;
			}
			// data
			NoVoid<H>           hdr       ;
			uint8_t             n_saved=0 ;
			::pair<I,SaveItem_> save[64]  ; // there are recursive loops to backup, but 64 is more than extreme (need ~6+loops, loops may be 1 or 2)
		} ;

	}

	//
	// MultiPrefixFile
	//

	template<bool AutoLock,class Hdr_,class Idx_,class Char_=char,class Data_=void,bool Reverse_=false> struct MultiPrefixFile
	:	              AllocFile< false/*AutoLock*/ , Prefix::Hdr<Hdr_,Idx_,Char_,Data_,Reverse_> , Idx_ , Prefix::Item<Idx_,Char_,Data_,Reverse_> , Prefix::Item<Idx_,Char_,Data_>::MaxSz >
	{	using Base  = AllocFile< false/*AutoLock*/ , Prefix::Hdr<Hdr_,Idx_,Char_,Data_,Reverse_> , Idx_ , Prefix::Item<Idx_,Char_,Data_,Reverse_> , Prefix::Item<Idx_,Char_,Data_>::MaxSz > ;
		using Item  =                                                                                     Prefix::Item<Idx_,Char_,Data_,Reverse_>                                           ;
		//
		using Hdr   = Hdr_                 ;
		using Idx   = Idx_                 ;
		using Char  = Char_                ;
		using Data  = Data_                ;
		using ULock = UniqueLock<AutoLock> ;
		using SLock = SharedLock<AutoLock> ;
		//
		static_assert(sizeof(Item)==Item::ItemSizeOf) ;
		static constexpr bool Reverse = Reverse_ ;
		using SaveItem = Prefix::SaveItem<Idx,Char,Data,Reverse> ;
		//
		using CharUint = typename Item::CharUint ;
		using ChunkBit = typename Item::ChunkBit ;
		using ChunkIdx = typename Item::ChunkIdx ;
		using Dvg      = typename Item::Dvg      ;
		using Sz       = typename Item::Sz       ;
		using HdrNv    = NoVoid<Hdr >            ;
		using DataNv   = NoVoid<Data>            ;
		using IdxSz    = typename Base::Sz       ;
		//
		static constexpr bool HasHdr  = !is_void_v<Hdr > ;
		static constexpr bool HasData = !is_void_v<Data> ;
		static constexpr bool IsStr   = IsChar<Char>     ;
		//
		using Kind    = ItemKind              ;
		using Nxt     = Prefix::Nxt           ;
		using Vec     = Prefix::Vec    <Char> ;
		using VecView = Prefix::VecView<Char> ;
		using Str     = Prefix::Str    <Char> ;
		using StrView = Prefix::StrView<Char> ;
		//
		template<bool S> using VecStr   = Prefix::VecStr  <S,Char> ;
		template<bool S> using ItemChar = Prefix::ItemChar<S,Char> ;
		//
		static Char const& _s_char_at( VecView const& name ,                       size_t pos ) { return Item::s_char_at(name,     pos) ; }
		static Char const& _s_char_at( VecView const& name , VecView const& psfx , size_t pos ) { return Item::s_char_at(name,psfx,pos) ; }
		//
		template<class... A> Idx emplace(A&&...) = delete ;
		//
		using Base::clear  ;
		using Base::size   ;
		using Base::_mutex ;

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
				// accesses
			private :
				Item const& _item() const { return _self->_self->_at(_idx) ; }
				// services
			public :
				bool      operator==(Iterator const& other) const { return _self==other._self && _idx==other._idx ; }
				Idx       operator* (                     ) const { SWEAR(_item().used) ; return _idx ;             }
				Iterator& operator++(                     )       { _advance() ; _legalize() ; return *this ;       }
				Iterator  operator++(int                  )       { Iterator res = *this ; ++*this ; return res ;   }
			private :
				void _advance() {
					SWEAR(+_idx) ;
					Item const* item = &_item()     ;
					Kind        k    = item->kind() ;
					for(;;) {
						if (k!=Kind::Terminal  ) { _idx = item->nxt_if(k==Kind::Prefix) ; return ; }
						if (_idx==_self->_start) { _idx = Idx()                         ; return ; } // done
						k    = item->prev_is_eq ? Kind::Terminal : Kind::Prefix ;
						_idx = item->prev                                       ;
						item = &_item()                                         ;
					}
				}
				bool _is_legal() const { return _item().used ; }
				bool _at_end  () const { return !_idx        ; }
				void _legalize() {
					for( ; !_at_end() ; _advance() ) if (_is_legal()) return ;
				}
				// data
				Lst const* _self ;
				Idx        _idx  = {} ;
			} ;
			// cxtors & casts
			Lst( MultiPrefixFile const& s , Idx st ) : _self{&s} , _start{st} , _lock{s._mutex} {}
			// services
			Iterator begin () const { return Iterator(*this,_start) ; }
			Iterator cbegin() const { return Iterator(*this,_start) ; }
			Iterator end   () const { return Iterator(*this,{}    ) ; }
			Iterator cend  () const { return Iterator(*this,{}    ) ; }
			// data
		private :
			MultiPrefixFile const* _self  ;
			Idx                    _start ;
			mutable SLock          _lock  ;
		} ;

		struct DvgDigest {
			// cxtors & casts
			DvgDigest( Idx root , MultiPrefixFile const& file , VecView const& name , VecView const& psfx ) { // psfx is prefix (Reverse) / suffix (!Reverse)
				for( idx = root ; dvg==Dvg::Cont ; name_pos+=chunk_pos ) {
					Idx         prev_idx = idx           ;
					Item const& item     = file._at(idx) ;
					dvg = item.find_dvg( idx/*out*/ , chunk_pos/*out*/ , name , psfx , name_pos ) ;
					if ( item.used && chunk_pos==item.chunk_sz ) {
						used_idx = prev_idx         ;
						used_pos = name_pos+chunk_pos ;
					}
				}
			}
			// accesses
			bool is_match() const { return dvg==Dvg::Match ; }
			// data
			Dvg      dvg       = Dvg::Cont ;
			size_t   name_pos  = 0         ;
			Idx      idx       = Idx()     ;
			ChunkIdx chunk_pos = 0         ;
			size_t   used_pos  = 0         ;
			Idx      used_idx  = Idx()     ;
		} ;

		// cxtors
		/**/                 MultiPrefixFile(                                                           ) = default ;
		template<class... A> MultiPrefixFile( NewType                                 , A&&... hdr_args ) { init( New              , ::forward<A>(hdr_args)... ) ; }
		template<class... A> MultiPrefixFile( ::string const&   name , bool writable_ , A&&... hdr_args ) { init( name , writable_ , ::forward<A>(hdr_args)... ) ; }
		template<class... A> void init( NewType , A&&... hdr_args ) {
			Base::init(New,::forward<A>(hdr_args)...) ;
		}
		template<class... A> void init( ::string const&   name_ , bool writable_ , A&&... hdr_args ) {
			Base::init( name_ , writable_ , ::forward<A>(hdr_args)... ) ;
			// fix in case of crash during an operation
			if (!writable_) { SWEAR(!_n_saved(),_n_saved()) ; return ; } // cannot fix if not writable
			for( uint8_t i=0 ; i<_n_saved() ; i++ ) {
				auto const& [idx,save_item] = _save()[i] ;
				save_item.restore(_at(idx)) ;
			}
			_commit() ;                                                  // until this point, nothing is commited and program may crash without impact
		}

		// data
	private :
		::vector<Idx   > _scheduled_pop     ;
		::vmap  <Idx,Sz> _scheduled_shorten ;

		// accesses
	public :
		HdrNv  const& hdr  (       ) const requires(HasHdr ) { return Base::hdr(   ).hdr    ;                  }
		HdrNv       & hdr  (       )       requires(HasHdr ) { return Base::hdr(   ).hdr    ;                  }
		HdrNv  const& c_hdr(       ) const requires(HasHdr ) { return Base::hdr(   ).hdr    ;                  }
		DataNv const& at   (Idx idx) const requires(HasData) { return Base::at (idx).data() ;                  }
		DataNv      & at   (Idx idx)       requires(HasData) { return Base::at (idx).data() ;                  }
		DataNv const& c_at (Idx idx) const requires(HasData) { return Base::at (idx).data() ;                  }
		void          clear(Idx idx)       requires(HasData) { ULock{_mutex} ; Base::at(idx).data() = Data() ; }
	private :
		Item const& _at(Idx idx) const { return Base::at(idx) ; }
		Item      & _at(Idx idx)       { return Base::at(idx) ; }

		// services
		uint8_t                     _n_saved() const { return Base::hdr().n_saved ; }
		::pair<Idx,SaveItem> const* _save   () const { return Base::hdr().save    ; }
		template<bool Bu> void _backup(Idx idx) {
			if (Bu) Base::hdr().backup(idx,_at(idx)) ;
		}
		void _commit() {
			Base::hdr().commit() ;
			fence() ;
			for( Idx   idx         : _scheduled_pop     ) Base::pop    (idx                     ) ;
			for( auto [idx,old_sz] : _scheduled_shorten ) Base::shorten(idx,old_sz,_at(idx).sz()) ;
			_scheduled_pop    .clear() ;
			_scheduled_shorten.clear() ;
		}
	public :
		// globals
		Idx emplace_root() {
			ULock lock{_mutex} ;
			return Base::emplace( Item::MinUsedSz , Item::MinUsedSz , Kind::Terminal ) ;
		}
		Lst lst(Idx root) const {
			return Lst(*this,root) ;
		}
		void chk(Idx root) const {
			Base::chk() ;
			if (+root) _chk(root,false/*recurse_backward*/,true/*recurse_forward*/) ;
		}
		// per item
	private :
		void  _append_lst( ::vector<Idx>& idx_lst/*out*/ , Idx                                                ) const ;
		IdxSz _chk       (                                 Idx , bool recurse_backward , bool recurse_forward ) const ;
		// cannot provide insert_data as insert requires unlocked while data requires locked
	public :
		Idx           search   ( Idx root , VecView const& n , VecView const& psfx={} ) const ;
		DataNv      * search_at( Idx root , VecView const& n , VecView const& psfx={} )       requires( HasData          ) { Idx idx=search(root,n,psfx) ; return +idx?&at(idx):nullptr ; }
		DataNv const* search_at( Idx root , VecView const& n , VecView const& psfx={} ) const requires( HasData          ) { Idx idx=search(root,n,psfx) ; return +idx?&at(idx):nullptr ; }
		Idx           insert   ( Idx root , VecView const& n , VecView const& psfx={} ) ;
		DataNv      & insert_at( Idx root , VecView const& n , VecView const& psfx={} )                                    { return at(insert(root,n,psfx)) ; }
		Idx           erase    ( Idx root , VecView const& n , VecView const& psfx={} ) ;
		Idx           search   ( Idx root , StrView const& n , StrView const& psfx={} ) const requires(            IsStr ) { return search   ( root , VecView(n) , VecView(psfx) ) ; }
		DataNv      * search_at( Idx root , StrView const& n , StrView const& psfx={} )       requires( HasData && IsStr ) { return search_at( root , VecView(n) , VecView(psfx) ) ; }
		DataNv const* search_at( Idx root , StrView const& n , StrView const& psfx={} ) const requires( HasData && IsStr ) { return search_at( root , VecView(n) , VecView(psfx) ) ; }
		Idx           insert   ( Idx root , StrView const& n , StrView const& psfx={} )       requires(            IsStr ) { return insert   ( root , VecView(n) , VecView(psfx) ) ; }
		DataNv      & insert_at( Idx root , StrView const& n , StrView const& psfx={} )       requires(            IsStr ) { return insert_at( root , VecView(n) , VecView(psfx) ) ; }
		Idx           erase    ( Idx root , StrView const& n , StrView const& psfx={} )       requires(            IsStr ) { return erase    ( root , VecView(n) , VecView(psfx) ) ; }
		//
		::pair<Idx,size_t/*pos*/> longest( Idx root , VecView const& n , VecView const& psfx={} ) const ; // longest existing prefix(!Reverse) / suffix(Reverse)
		::pair<Idx,size_t/*pos*/> longest( Idx root , StrView const& n , StrView const& psfx={} ) const requires(IsStr) { return longest( root , VecView(n) , VecView(psfx) ) ; }
		//
		::vector<Idx> path             ( Idx              ) const ;                                       // path of existing items
		void          pop              ( Idx              ) ;
		Idx           insert_shorten_by( Idx , size_t by  ) ;
		Idx           insert_dir       ( Idx , Char   sep ) ;
		//
		bool            empty         ( Idx i                    ) const                               { if (!i) return true ; SLock lock{_mutex} ; return !_at            (i).prev    ; }
		size_t          key_sz        ( Idx i , size_t psfx_sz=0 ) const                               {                       SLock lock{_mutex} ; return _key_sz         (i,psfx_sz) ; }
		Vec             key           ( Idx i , size_t psfx_sz=0 ) const                               {                       SLock lock{_mutex} ; return _key     <false>(i,psfx_sz) ; }
		Vec             prefix        ( Idx i , size_t pfx_sz    ) const requires(  Reverse          ) {                       SLock lock{_mutex} ; return _psfx    <false>(i,pfx_sz ) ; }
		Vec             suffix        ( Idx i , size_t sfx_sz    ) const requires( !Reverse          ) {                       SLock lock{_mutex} ; return _psfx    <false>(i,sfx_sz ) ; }
		::pair<Vec,Vec> key_prefix    ( Idx i , size_t pfx_sz    ) const requires(  Reverse          ) {                       SLock lock{_mutex} ; return _key_psfx<false>(i,pfx_sz ) ; }
		::pair<Vec,Vec> key_suffix    ( Idx i , size_t sfx_sz    ) const requires( !Reverse          ) {                       SLock lock{_mutex} ; return _key_psfx<false>(i,sfx_sz ) ; }
		Str             str_key       ( Idx i , size_t psfx_sz=0 ) const requires(             IsStr ) {                       SLock lock{_mutex} ; return _key     <true >(i,psfx_sz) ; }
		Str             str_prefix    ( Idx i , size_t pfx_sz    ) const requires(  Reverse && IsStr ) {                       SLock lock{_mutex} ; return _psfx    <true >(i,pfx_sz ) ; }
		Str             str_suffix    ( Idx i , size_t sfx_sz    ) const requires( !Reverse && IsStr ) {                       SLock lock{_mutex} ; return _psfx    <true >(i,sfx_sz ) ; }
		::pair<Str,Str> str_key_prefix( Idx i , size_t pfx_sz    ) const requires(  Reverse && IsStr ) {                       SLock lock{_mutex} ; return _key_psfx<true >(i,pfx_sz ) ; }
		::pair<Str,Str> str_key_suffix( Idx i , size_t sfx_sz    ) const requires( !Reverse && IsStr ) {                       SLock lock{_mutex} ; return _key_psfx<true >(i,sfx_sz ) ; }
	private :
		/**/             size_t                      _key_sz  ( Idx , size_t /*psfx_sz*/=0 ) const ;
		template<bool S> VecStr<S>                   _key     ( Idx , size_t /*psfx_sz*/=0 ) const ;
		template<bool S> VecStr<S>                   _psfx    ( Idx , size_t /*psfx_sz*/   ) const ;
		template<bool S> ::pair<VecStr<S>,VecStr<S>> _key_psfx( Idx , size_t /*psfx_sz*/   ) const ;

		Idx _emplace( Kind k , bool used , VecView const& name , VecView const& psfx , size_t start , ChunkIdx chunk_sz ) {
			Sz sz = Item::s_min_sz( k , used , chunk_sz ) ;
			return Base::emplace( sz , sz , k , used , name , psfx , start , chunk_sz ) ;
		}
		Idx _emplace( Kind k , bool used , Idx idx , ChunkIdx start , ChunkIdx chunk_sz , CharUint cmp_val , ChunkBit cmp_bit ) {
			Sz sz = Item::s_min_sz( k , used , chunk_sz ) ;
			return Base::emplace( sz , sz , k , used , chunk_sz , _at(idx) , start , cmp_val , cmp_bit ) ;
		}
		Idx _emplace( Kind k , bool used , Idx idx , ChunkIdx start , ChunkIdx chunk_sz ) {
			if (k==Kind::Split) {
				Item const& item = _at(idx) ;
				SWEAR( item.kind()==Kind::Split , item.kind() ) ;                                      // else cannot find adequate cmp_val & cmp_bit
				return _emplace( k , used , idx , start , chunk_sz , item.cmp_val() , item.cmp_bit ) ;
			} else {
				return _emplace( k , used , idx , start , chunk_sz , 0/*cmp_val*/   , 0/*cmp_bit*/ ) ;
			}
		}
		Idx _emplace( Kind k , CharUint cmp_val=0 , ChunkBit cmp_bit=0 ) {
			Sz sz = Item::s_min_sz( k , false/*used*/ , 0/*chunk_sz*/ ) ;                              // no name, cannot be used
			return Base::emplace( sz , sz , k , cmp_val , cmp_bit ) ;
		}

		// from(is_eq)->0->to ==> from(is_eq)->to
		template<bool BuF,bool BuT> void _lnk( Idx from , bool is_eq , Idx to ) {
			_backup<BuF>(from) ;
			_backup<BuT>(to  ) ;
			Item& from_item = _at(from)               ;
			Item& to_item   = _at(to  )               ; SWEAR( !to_item.prev && to_item.prev_is_eq ) ;
			Idx & nxt       = from_item.nxt_if(is_eq) ; SWEAR( !nxt                                ) ;
			nxt                = to    ;
			to_item.prev       = from  ;
			to_item.prev_is_eq = is_eq ;
		}
		// from->0->to ==> from->to
		template<bool BuF,bool BuT> void _lnk( Idx from , Idx to ) {
			SWEAR( _at(from).kind()==Kind::Prefix , _at(from).kind() ) ;
			_lnk<BuF,BuT>(from,true,to) ;
		}

		// from(is_eq)->to ==> from(is_eq)->0->to
		template<bool BuF,bool BuT> void _unlnk( Idx from , bool is_eq , Idx to ) {
			_backup<BuF>(from) ;
			_backup<BuT>(to  ) ;
			Idx & nxt     = _at(from).nxt_if(is_eq) ;
			Item& to_item = _at(nxt)                ;
			SWEAR( nxt==to , nxt , to ) ;
			nxt                = Idx() ;
			to_item.prev       = Idx() ;
			to_item.prev_is_eq = true  ;
		}
		// from->to ==> from->0->to
		template<bool BuF,bool BuT> void _unlnk( Idx from , Idx to ) {
			SWEAR( _at(from).kind()==Kind::Prefix , _at(from).kind() ) ;
			_unlnk<BuF,BuT>(from,true,to) ;
		}
		// idx(is_eq)->... ==> idx(is_eq)->0->...
		template<bool BuI,bool BuNxt> void _unlnk_after( Idx idx , bool is_eq ) {
			_unlnk<BuI,BuNxt>(idx,is_eq,_at(idx).nxt_if(is_eq)) ;
		}
		// idx->... ==> idx->0->...
		template<bool BuI,bool BuNxt> void _unlnk_after(Idx idx) {
			SWEAR( _at(idx).kind()==Kind::Prefix , _at(idx).kind() ) ;
			_unlnk_after<BuI,BuNxt>(idx,true) ;
		}
		// ...->idx ==> ...->0->idx
		template<bool BuPrev,bool BuI> void _unlnk_before(Idx idx) {
			Item& item = _at(idx) ;
			_unlnk<BuPrev,BuI>(item.prev,item.prev_is_eq,idx) ;
		}

		// from(is_eq)->... , 0->to ==> from(is_eq)->to , 0->...
		template<bool BuF,bool BuOldNxt,bool BuT> void _mv_lnk_after( Idx from , bool is_eq , Idx to ) {
			_unlnk_after<BuF  ,BuOldNxt>(from,is_eq   ) ;
			_lnk        <false,BuT     >(from,is_eq,to) ; // from already backed up
		}

		// from(is_eq)->0 , ...->to ==> ...->0 , from(is_eq)->to
		template<bool BuOldPrev,bool BuF,bool BuT> void _mv_lnk_before( Idx from , bool is_eq , Idx to ) {
			_unlnk_before<BuOldPrev,BuT  >(           to) ;
			_lnk         <BuF      ,false>(from,is_eq,to) ; // to already backed up
		}

		// ...()->idx ==> ...()->0
		template<bool BuPrev,bool BuI> void _pop_item(Idx idx) {
			Item& item = _at(idx) ;
			for( bool is_eq : Nxt(item.kind()) ) SWEAR(!item.nxt_if(is_eq)) ;
			_unlnk_before<BuPrev,BuI>(idx) ;
			_scheduled_pop.push_back(idx) ;
		}

		// ...()->idx->... ==> ...()->...
		template<bool BuPrev,bool BuI,bool BuNxt> Idx/*prev*/ _erase_prefix(Idx idx) {
			Item& item = _at(idx) ;
			SWEAR( item.kind()==Kind::Prefix && !item.used , item.kind() , item.used ) ;
			Idx  prev  = item.prev       ;
			bool is_eq = item.prev_is_eq ;
			Idx  nxt   = item.nxt()      ;
			_unlnk_after<BuI   ,BuNxt>(idx)            ;
			_pop_item   <BuPrev,false>(idx)            ; // idx already backed up
			_lnk        <false ,false>(prev,is_eq,nxt) ; // prev & nxt already backup up
			return prev ;
		}

		// before(before_is_eq)->after ==> before(before_is_eq)->idx(is_eq)->after
		template<bool BuB,bool BuA> void _insert_between( Idx before , bool before_is_eq , Idx idx , bool is_eq , Idx after ) {
			_unlnk<BuB   ,BuA >(before,before_is_eq,after) ;
			_lnk  <false,false>(before,before_is_eq,idx  ) ; // before already backed up, idx was not in tree
			_lnk  <false,false>(idx   ,is_eq       ,after) ; //                           .                  , after already backed up
		}
		// ...()->after ==> ...()->idx(is_eq)->after
		template<bool BuPrev,bool BuA> void _insert_before( Idx idx , bool is_eq , Idx after ) {
			Item& after_item = _at(after) ;
			_insert_between<BuPrev,BuA>( after_item.prev , after_item.prev_is_eq , idx , is_eq , after ) ;
		}
		// before(before_is_eq)->... ==> before(before_is_eq)->idx(is_eq)->...
		template<bool BuB,bool BuNxtEq> void _insert_after( Idx before , bool before_is_eq , Idx idx , bool is_eq ) {
			SWEAR(_at(idx).kind()!=Kind::Terminal) ;
			_insert_between<BuB,BuNxtEq>( before , before_is_eq , idx , is_eq , _at(before).nxt_if(before_is_eq) ) ;
		}
		// before(*)->... ==> before(before_is_eq)->idx(*)->...
		template<bool BuB,bool BuNxtEq,bool BuNxtNeq> void _insert_after( Idx before , bool before_is_eq , Idx idx ) {
			SWEAR( _at(idx).kind()==Kind::Split , _at(idx).kind() ) ;
			_insert_between<BuB  ,BuNxtEq       >( before , before_is_eq , idx ,  before_is_eq , _at(before).nxt_if( before_is_eq) ) ;
			_mv_lnk_before <false,false,BuNxtNeq>(                         idx , !before_is_eq , _at(before).nxt_if(!before_is_eq) ) ; // from already backed up,idx was not in tree
		}

		// ...()->old_idx(*)->... ==> ...()->new_idx(*)->...
		template<bool BuPrev,bool BuO,bool BuNxt0,bool BuNxt1> void _mv( Idx old_idx , Idx new_idx ) {
			Item& old_item = _at(old_idx) ;
			Item& new_item = _at(new_idx) ;
			SWEAR( old_item.kind()==new_item.kind() , old_item.kind() , new_item.kind() ) ;
			/**/                              _mv_lnk_after <BuPrev,BuO  ,false >( old_item.prev , old_item.prev_is_eq , new_idx                                  ) ; // new_idx was not in tree
			if(old_item.kind()>=Kind::Split ) _mv_lnk_before<false ,false,BuNxt0>(                                       new_idx , false , old_item.nxt_if(false) ) ; // . + old_idx already backed up
			if(old_item.kind()>=Kind::Prefix) _mv_lnk_before<false ,false,BuNxt1>(                                       new_idx , true  , old_item.nxt_if(true ) ) ; // . + old_idx already backed up
			_scheduled_pop.push_back(old_idx) ;
		}

		// decrement kind from Split to Prefix, keeping keep_is_eq
		template<bool BuI,bool BuNxt> void _mk_down( Idx idx , bool keep_is_eq ) {
			_backup<BuI>(idx) ;
			Item& item = _at(idx) ;
			if (!keep_is_eq) {
				Idx nxt = item.nxt_if(keep_is_eq) ;
				_backup<BuNxt>(nxt) ;
				_at(nxt).prev_is_eq = true ;
			}
			item.mk_down(keep_is_eq) ;
		}

		// decrement kind from Prefix to Terminal, keeping keep_is_eq
		template<bool BuI> void _mk_down(Idx idx) {
			_backup<BuI>(idx) ;
			_at(idx).mk_down() ;
		}

		// backup if minimized
		template<bool Bu_> bool/*minimized*/ _minimize_sz(Idx idx) {
			Item& item   = _at(idx)  ;
			if (!item.prev) return false ; // root is not minimized as it must stay prepared to hold its max info w/o moving
			Sz old_sz = item.sz() ;
			if (!item.need_mk_min_sz()) return false ;
			_backup<Bu_>(idx) ;
			//vvvvvvvvvvvvvv
			item.mk_min_sz() ;
			//^^^^^^^^^^^^^^
			_scheduled_shorten.emplace_back(idx,old_sz) ;
			return true ;
		}

		// backup if compressed
		template<bool BuPrev_,bool BuI_,bool BuNxt_> ::pair<bool/*compressed*/,Idx/*new_idx*/> _compress_after(Idx idx) {
			Item& item = _at(idx) ;
			if (!item.prev                ) return {false,idx} ;                                    // root is not compressed as it must stay prepared to hold its max info w/o moving
			if (item.kind()!=Kind::Prefix ) return {false,idx} ;
			if (item.used                 ) return {false,idx} ;
			Idx   nxt      = item.nxt() ;
			Item& nxt_item = _at(nxt)   ;
			if ( item.chunk_sz + nxt_item.chunk_sz > nxt_item.max_chunk_sz() ) return {false,idx} ; // check there is enough room
			_backup<BuNxt_>(nxt) ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvv
			nxt_item.prepend_from(item,0) ;
			idx = _erase_prefix<BuPrev_,BuI_,false>(idx) ;                                          // nxt already backed up
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^i^^^^^^^^^^
			if (+idx) _compress_after<true,false,false>(idx) ;                                      // nxt may have some room for the new prev, idx & nxt already backed up, but new prev is not
			return {true,nxt} ;
		}

		// backup if compressed
		template<bool BuPrev2_,bool BuPrev_,bool BuI_> bool/*compressed*/ _compress_before(Idx idx) {
			Idx prev = _at(idx).prev ;
			if (!prev) return false ;
			return _compress_after<BuPrev2_,BuPrev_,BuI_>(prev).first ;
		}

		// prev.prev backed up if compressed, prev & idx backed up if prefix added
		template<bool BuPrev2_,bool BuPrev_,bool BuI_> ChunkIdx _add_prefix( Idx idx , ChunkIdx chunk_sz , ChunkIdx max_chunk_sz ) {
			if (chunk_sz<=max_chunk_sz) return 0 ;                                                                                   // if root, chunk_sz==0 and we stop here
			ChunkIdx prefix_chunk_sz = chunk_sz-max_chunk_sz ;
			Item&    item            = _at(idx      )        ;
			Item&    prev_item       = _at(item.prev)        ;
			if ( !prev_item.used && +prev_item.prev && prev_item.kind()==Kind::Prefix && prev_item.chunk_sz+prefix_chunk_sz<=prev_item.max_chunk_sz() ) {
				// directly copy data from item to prev_item
				_backup<BuPrev_>(item.prev) ;
				_backup<BuI_   >(idx      ) ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				prev_item.append_from(item,prefix_chunk_sz) ;
				item.shorten_by(prefix_chunk_sz) ;               // dont minimize idx as it will be done by caller
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} else {
				Idx prefix = _emplace( Kind::Prefix , false/*used*/ , idx , 0/*start*/ , prefix_chunk_sz ) ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				_insert_before<BuPrev_,BuI_>(prefix,true,idx) ;
				item.shorten_by(prefix_chunk_sz) ;               // dont minimize idx as it will be done by caller
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				_compress_before<BuPrev2_,false,false>(prefix) ; // should be useless but too difficult to prove. prev already backed up, prefix was not in tree
			}
			return prefix_chunk_sz ;
		}
		// prev.prev backed up if compressed, prev & idx backed up if prefix added
		template<bool BuPrev2_,bool BuPrev_,bool BuI_> ChunkIdx _add_prefix( Idx idx , ChunkIdx max_chunk_sz ) {
			return _add_prefix<BuPrev2_,BuPrev_,BuI_>( idx , _at(idx).chunk_sz , max_chunk_sz ) ;
		}

		// cut item at pos : first part is of kind k, with nxt(true) pointing to second part
		// prev.prev backed up if necessary, prev & idx always backed up, nxt backed up if necessary
		template<bool BuPrev2_,bool BuPrev,bool BuI,bool BuNxt_> Idx _cut_with( Idx idx , ChunkIdx pos , Kind k , bool used , CharUint cmp_val=0 , CharUint dvg_val=0 ) {
			Item& item = _at(idx) ;
			_backup<BuI   >(idx      ) ;
			_backup<BuPrev>(item.prev) ;
			/**/                 SWEAR( pos<item.chunk_sz , pos , item.chunk_sz ) ;
			if (k==Kind::Prefix) SWEAR( pos>0                                   ) ;
			pos -= _add_prefix<BuPrev2_,false,false>( idx , pos , Item::s_max_chunk_sz(k,used) ) ; // prev & idx already backed up
			//        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Idx dvg = _emplace( k , used , idx , 0/*start*/ , pos , cmp_val , Item::s_cmp_bit(cmp_val,dvg_val) ) ;
			item.shorten_by(pos) ;
			//^^^^^^^^^^^^^^^^^^
			_insert_before<false,false>(dvg,true/*is_eq*/,idx) ;
			bool compressed ;
			::tie(compressed,idx) = _compress_after<false,false,BuNxt_>(idx) ;                     // idx already backed up
			if (!compressed) _minimize_sz<false>(idx) ;                                            // idx already backed up, minimize if it has not been merged
			_compress_before<false,false,false>(dvg) ;                                             // all already backed up
			return dvg ;
		}

		// idx always backed up, others as necessary (max 4 backups)
		template<bool BuPrev2_,bool BuPrev_,bool BuI,bool BuNxt0_,bool BuNxt1_> Idx _branch( Idx idx , ChunkIdx pos , CharUint dvg_val ) {
			_backup<BuI>(idx) ;
			Item& item = _at(idx) ;
			if (pos<item.chunk_sz) return _cut_with<BuPrev2_,BuPrev_,false,BuNxt1_>( idx , pos , Kind::Split , false/*used*/ , Prefix::rep(item.chunk(pos)) , dvg_val ) ; // idx already backed up
			//
			Kind kind = item.kind() ;
			if (kind==Kind::Split) {
				ChunkBit cmp_bit = Item::s_cmp_bit( item.cmp_val() , dvg_val ) ;
				SWEAR( cmp_bit<item.cmp_bit , cmp_bit , item.cmp_bit ) ;
				//        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				Idx cpy = _emplace( Kind::Split , item.cmp_val() , item.cmp_bit ) ;
				//        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				item.cmp_bit = cmp_bit ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				_insert_after<false,BuNxt1_,BuNxt0_>( idx , true/*is_eq*/ , cpy ) ;                             // idx already backed up
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				return idx ;
			}
			CharUint cmp_val{} ;
			if (kind==Kind::Prefix) cmp_val = _at(item.nxt()).dvg_char(0,dvg_val) ;
			if (item.may_mk_up_empty()) {
				_add_prefix<BuPrev2_,BuPrev_,false>( idx , Item::s_max_chunk_sz(item.sz(),kind+1,item.used) ) ; // idx already backed up
				if (kind==Kind::Prefix) item.mk_up(cmp_val,dvg_val) ;
				else                    item.mk_up(               ) ;
				_minimize_sz<false>(idx) ;                                                                      // idx already backed up
				return idx ;
			}
			SWEAR( +item.prev                ) ;                                                                // root has is always may_mk_up_empty()
			SWEAR( kind==Kind::Prefix , kind ) ;                                                                // single char Terminal *must* be transformable into Prefix
			SWEAR( item.used                 ) ;                                                                // empty unused items are transformable into Split
			//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Idx new_idx = _emplace( Kind::Split , cmp_val , Item::s_cmp_bit(cmp_val,dvg_val) ) ;
			_insert_after<false,BuNxt1_>( idx , true/*before_is_eq*/ , new_idx , true/*is_eq*/ ) ;              // idx already backed up
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			return new_idx ;
		}

		// below, operations are transactional (i.e. they are fully performed or not at all, even in case of crash)

		// idx always backed up, others as necessary
		Idx _use(Idx idx) {
			_backup<true>(idx) ;
			Item&    item     = _at(idx)      ;
			Kind     kind     = item.kind()   ;
			ChunkIdx chunk_sz = item.chunk_sz ;
			SWEAR( !item.used             ) ;
			SWEAR( chunk_sz || !item.prev ) ;
			if (item.may_use_empty()                            )    goto InPlaceWithPrefix ;
			SWEAR(+item.prev) ;
			if (kind!=Kind::Split                               )    goto EnlargeItem       ;
			if (!item.prev                                      )    goto InsertSplitAfter  ;  // root cannot move
			if (Item::s_max_chunk_sz(kind,true/*used*/)<chunk_sz)    goto InsertSplitAfter  ;  // chunk is too large to become used, even max sized
			/**/                                                  /* goto EnlargeItem       */
		EnlargeItem :
			{	//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				Idx new_idx = _emplace( kind , true/*used*/ , idx , 0/*start*/ , chunk_sz ) ;
				_mv<true,false,true,true>( idx , new_idx ) ;                                   // root cannot be here as with empty chunk it would be caught by previous case
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				_compress_before<true,false,true>(new_idx) ;                                   // prev already backed up
				_commit() ;
				return new_idx ;
			}
		InsertSplitAfter :
			SWEAR( kind==Kind::Split , kind ) ;
			_insert_after<false,true,true>( idx , true/*is_eq*/ , _emplace( Kind::Split , item.cmp_val() , item.cmp_bit ) ) ; // insert empty Split after
			item.mk_down(true/*keep_is_eq*/) ;
		InPlaceWithPrefix :
			_add_prefix<true,true,false>( idx , Item::s_max_chunk_sz( item.sz() , item.kind() , true/*used*/ ) ) ;            // item.kind may have changed, so cannot use kind
			//vvvvvvvvvvvvvvvvvvvvvvvv
			item.mk_used(true/*used*/) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^
			_minimize_sz<false>(idx) ;                                                                                        // idx already backed up
			_commit() ;
			return idx ;
		}

		// idx always backed up, others as necessary
		Idx _cut( Idx idx , ChunkIdx pos ) {
			if (pos==_at(idx).chunk_sz) return _use(idx) ;
			//
			Idx res ;
			//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res = _cut_with<true,true,true,true>( idx , pos , Kind::Prefix , true /*used*/ ) ;
			//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			_commit() ;
			return res ;
		}

		Idx _insert( Idx idx , ChunkIdx chunk_pos , VecView const& name , VecView const& psfx , size_t pos ) {
			static constexpr ChunkIdx MaxPrefixChunkSz   = Item::s_max_chunk_sz(                   Kind::Prefix   , false/*used*/ ) ;
			static constexpr ChunkIdx MaxTerminalChunkSz = Item::s_max_chunk_sz(                   Kind::Terminal , true /*used*/ ) ;
			static constexpr ChunkIdx MinTerminalChunkSz = Item::s_max_chunk_sz( Item::MinUsedSz , Kind::Terminal , true /*used*/ ) ;
			size_t total_sz = Prefix::size(name,psfx) ;
			SWEAR( pos<=total_sz , pos , total_sz ) ;
			//                        vvvvvvvvvvvvvvvvvvv
			if (pos==total_sz) return _cut(idx,chunk_pos) ;                                                 // new item is a prefix of an existing one
			//                        ^^^^^^^^^^^^^^^^^^^
			//
			if ( Idx prev = _at(idx).prev ; !chunk_pos && +prev && _at(prev).kind()==Kind::Prefix ) {       // avoid inserting an empty Split after a Prefix, prefer to transform it in place
				idx       = prev               ;
				chunk_pos = _at(prev).chunk_sz ;
			}
			// create branch
			Idx      branch   {} ;                                                                          // first item of the branch
			Idx      prev_idx {} ;
			CharUint dvg_val  = Prefix::rep(Prefix::char_at<Reverse>(name,psfx,pos)) ;
			while ( pos+MaxTerminalChunkSz < total_sz ) {                                                   // create intermediate items
				ChunkIdx chunk_sz = ::min( total_sz-(pos+MinTerminalChunkSz) , size_t(MaxPrefixChunkSz) ) ;
				//             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				Idx new_idx  = _emplace( Kind::Prefix , false/*used*/ , name , psfx , pos , chunk_sz ) ;
				//             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				pos += chunk_sz ;
				//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (+branch) _lnk<false,false>( prev_idx , true , new_idx ) ;                               // not in tree yet
				//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				else         branch = new_idx ;
				prev_idx = new_idx ;
			}
			//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Idx new_idx = _emplace( Kind::Terminal , true/*used*/ , name , psfx , pos , total_sz-pos ) ;    // create last item
			if (+branch) _lnk<false,false>( prev_idx , true , new_idx ) ;                                   // not in tree yet
			//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			else branch = new_idx ;
			// link branch
			//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			idx = _branch<true,true,true,true,true>(idx,chunk_pos,dvg_val) ;
			_lnk<false,false>( idx , _at(idx).kind()==Kind::Prefix , branch ) ;                             // idx already backed up, branch was not in tree
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			_commit() ;
			return new_idx ;
		}

		void _pop(Idx idx) {
			Item* item = &_at(idx) ;
			SWEAR(item->used) ;
			if ( item->kind()==Kind::Terminal && +item->prev ) {                      // root must remain as a Terminal even if unused
				Idx nxt ;
				do {                                                                  // walk backward to suppress all items whose only purpose is to lead to Terminal
					nxt  = idx        ;
					idx  = item->prev ;
					item = &_at(idx)  ;
				} while( item->kind()==Kind::Prefix && !item->used && +item->prev ) ; // root can be an unused Terminal
				Item* nxt_item = &_at(nxt) ;
				//                                                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (item->kind()==Kind::Split) { bool is_eq = nxt_item->prev_is_eq ; _unlnk_before<true,true>(nxt) ; _mk_down<false,true>(idx,!is_eq) ; }
				else                           {                                     _unlnk_before<true,true>(nxt) ; _mk_down<false     >(idx       ) ; }
				//                                                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				// try compression forward & backward, so use | instead of ||
				bool compressed ;
				::tie(compressed,idx)  = _compress_after <true,false,false>(idx) ;    // idx & nxt  already backed up
				compressed            |= _compress_before<true,false,false>(idx) ;    // idx & prev already backed up
				if(!compressed) _minimize_sz<false>(idx) ;                            // idx already backed up
				_commit() ;
				while(nxt_item->kind()==Kind::Prefix) {                               // now that branch is out of the tree, walk forward to actually collect the items
					nxt      = nxt_item->nxt() ;
					nxt_item = &_at(nxt)       ;
					//    vvvvvvvv
					Base::pop(nxt) ;
					//    ^^^^^^^^
				} ;
				Base::pop(nxt) ;
			} else {
				_backup<true>(idx) ;
				item->mk_used(false) ;
				// try compression forward & backward, so use | instead of ||
				bool compressed ;
				::tie(compressed,idx)  = _compress_after <true,false,true >(idx) ;    // idx already backed up
				compressed            |= _compress_before<true,false,false>(idx) ;    // idx & prev already backed up
				if(!compressed) _minimize_sz<false>(idx) ;                            // idx already backed up
				_commit() ;
			}
		}

	} ;

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		void MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::_append_lst( ::vector<Idx>& idx_lst/*out*/ , Idx idx ) const {
			Item const& item = _at(idx) ;
			if (item.used) idx_lst.push_back(idx) ;
			switch (item.kind()) {
				case Kind::Terminal : break ;
				case Kind::Prefix   : _append_lst( idx_lst , item.nxt() ) ; break ;
				case Kind::Split : {
					bool zero_is_eq = !item.dvg_at(0) ;
					_append_lst( idx_lst , item.nxt_if( zero_is_eq) ) ; // output in increasing order
					_append_lst( idx_lst , item.nxt_if(!zero_is_eq) ) ; // .
				} break ;
			DF}
		}

	// compute both name & suffix in a single pass
	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		// psfx_sz if the size of the prefix (Reverse) / suffix (!Reverse) to suppress
		template<bool S> ::pair<Prefix::VecStr<S,Char>,Prefix::VecStr<S,Char>> MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::_key_psfx( Idx idx , size_t psfx_sz ) const {
			VecStr<S> name ;
			VecStr<S> psfx ;
			::vector<pair<Idx,ChunkIdx/*chunk_sz*/>> name_path ; // when !Reverse, we must walk from root to idx but we gather pathes from idx back to root
			::vector<pair<Idx,ChunkIdx/*chunk_sz*/>> psfx_path ; // .
			for(; +idx ; idx = _at(idx).prev ) {
				Item     const& item     = _at(idx)      ;
				ChunkIdx        chunk_sz = item.chunk_sz ;
				if (psfx_sz>=chunk_sz) {
					if (Reverse) Prefix::append( psfx , &item.chunk(chunk_sz-1) , chunk_sz ) ;                   // both psfx & chunk are stored in reverse order
					else         psfx_path.emplace_back(idx,chunk_sz) ;
					psfx_sz -= chunk_sz ;
				} else {
					if (psfx_sz) {
						if (Reverse) Prefix::append( psfx , &item.chunk(chunk_sz-1) , psfx_sz ) ;                // both psfx & chunk are stored in reverse order
						else         psfx_path.emplace_back(idx,psfx_sz) ;
					}
					if (Reverse) Prefix::append( name , &item.chunk(chunk_sz-1-psfx_sz) , chunk_sz-psfx_sz ) ;   // both name & chunk are stored in reverse order
					else         name_path.emplace_back(idx,chunk_sz-psfx_sz) ;
					psfx_sz = 0 ;
				}
			}
			if (!Reverse) {
				for( auto it=psfx_path.crbegin() ; it!=psfx_path.crend() ; it++ ) {
					Item const& item = _at(it->first) ;
					for( int i=0 ; i<it->second ; i++ ) psfx.push_back(item.chunk(item.chunk_sz-it->second+i)) ; // chunk is stored in reverse order
				}
				for( auto it=name_path.crbegin() ; it!=name_path.crend() ; it++ ) {
					Item const& item = _at(it->first) ;
					for( int i=0 ; i<it->second ; i++ ) name.push_back(item.chunk(i)) ;                          // chunk is stored in reverse order
				}
			}
			return { name , psfx } ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		// psfx_sz if the size of the prefix (Reverse) / suffix (!Reverse) to suppress
		size_t MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::_key_sz( Idx idx , size_t psfx_sz ) const {
			size_t res = 0 ;
			for(; +idx ; idx=_at(idx).prev ) res += _at(idx).chunk_sz ;
			return res-psfx_sz ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		// psfx_sz if the size of the prefix (Reverse) / suffix (!Reverse) to suppress
		template<bool S> Prefix::VecStr<S,Char> MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::_key( Idx idx , size_t psfx_sz ) const {
			VecStr<S> res ;
			::vector<pair<Idx,ChunkIdx/*chunk_sz*/>> path ; // when !Reverse, we must walk from root to idx but we gather path from idx back to root
			for(; +idx ; idx = _at(idx).prev ) {
				Item     const& item     = _at(idx)      ;
				ChunkIdx        chunk_sz = item.chunk_sz ;
				if (psfx_sz>=chunk_sz) {
					psfx_sz -= chunk_sz ;
				} else {
					if (Reverse) Prefix::append( res , &item.chunk(item.chunk_sz-1-psfx_sz) , chunk_sz-psfx_sz ) ; // both res & chunk are stored in reverse order
					else         path.emplace_back(idx,chunk_sz-psfx_sz) ;
					psfx_sz = 0 ;
				}
			}
			if (!Reverse) {
				for( auto it=path.crbegin() ; it!=path.crend() ; it++ ) {
					Item const& item = _at(it->first) ;
					for( int i=0 ; i<it->second ; i++ ) res.push_back(ItemChar<S>(item.chunk(i))) ;                // chunk is stored in reverse order
				}
			}
			return res ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		// psfx_sz if the size of the prefix (Reverse) / suffix (!Reverse) to get
		template<bool S> Prefix::VecStr<S,Char> MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::_psfx( Idx idx , size_t psfx_sz ) const {
			VecStr<S>                                res  ;
			::vector<pair<Idx,ChunkIdx/*chunk_sz*/>> path ;                                                     // when !Reverse, we must walk from root to idx but we gather path from idx back to root
			for(; +idx ; idx = _at(idx).prev ) {
				Item     const& item         = _at(idx)                        ;
				ChunkIdx        chunk_sz     = item.chunk_sz                   ;
				ChunkIdx        min_chunk_sz = ::min(size_t(chunk_sz),psfx_sz) ;
				if (Reverse) Prefix::append( res , &item.chunk(chunk_sz-1) , min_chunk_sz ) ;                   // both res & chunk are stored in reverse order
				else         path.emplace_back(idx,min_chunk_sz) ;
				if (psfx_sz>chunk_sz) psfx_sz -= chunk_sz ;
				else                  break ;
			}
			if (!Reverse) {
				for( auto it=path.crbegin() ; it!=path.crend() ; it++ ) {
					Item const& item = _at(it->first) ;
					for( int i=0 ; i<it->second ; i++ ) res.push_back(item.chunk(item.chunk_sz-it->second+i)) ; // chunk is stored in reverse order
				}
			}
			return res ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		::vector<Idx> MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::path(Idx idx) const {
			::vector<Idx> res ;
			SLock         lock{_mutex} ;
			while (idx) {
				Item const& item = _at(idx) ;
				if (item.used) res.push_back(idx) ;
				idx = item.prev ;
			}
			return res ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		Idx MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::search( Idx root , VecView const& name_ , VecView const& psfx ) const { // psfx is prefix (Reverse) / suffix (!Reverse)
			SLock     lock{_mutex}                        ;
			DvgDigest dvg { root , *this , name_ , psfx } ;
			return dvg.is_match() ? dvg.idx : Idx() ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		Idx MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::insert( Idx root , VecView const& name_ , VecView const& psfx ) { // psfx is prefix (Reverse) / suffix (!Reverse)
			ULock     lock{_mutex}                        ;
			DvgDigest dvg { root , *this , name_ , psfx } ;
			if (dvg.is_match()) return dvg.idx ;
			Idx res = _insert( dvg.idx , dvg.chunk_pos , name_ , psfx , dvg.name_pos ) ;
			if constexpr (HasData) SWEAR(at(res)==DataNv()) ;
			return res ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		Idx MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::erase( Idx root , VecView const& name_ , VecView const& psfx ) { // psfx is prefix (Reverse) / suffix (!Reverse)
			ULock     lock{_mutex}                        ;
			DvgDigest dvg { root , *this , name_ , psfx } ;
			if (!dvg.is_match()) return Idx() ;
			_pop(dvg.idx) ;
			return dvg.idx ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		::pair<Idx,size_t/*size*/> MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::longest( Idx root , VecView const& name_ , VecView const& psfx  ) const {
			SLock     lock{_mutex}                        ;
			DvgDigest dvg { root , *this , name_ , psfx } ;
			return {dvg.used_idx,dvg.used_pos} ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		void MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::pop(Idx idx) {
			ULock lock{_mutex} ;
			_pop(idx) ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		Idx MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::insert_shorten_by( Idx idx , size_t by ) {
			ULock lock{_mutex} ;
			for(; +idx ; idx = _at(idx).prev ) {
				Item const& item = _at(idx) ;
				ChunkIdx chunk_sz = item.chunk_sz ;
				if (by) {
					if (by<chunk_sz) return _cut(idx,chunk_sz-by) ;
					by -= chunk_sz ;
				} else {
					if (item.used) return idx       ;
					if (chunk_sz)  return _use(idx) ;
				}
			}
			return Idx() ;
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		Idx MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::insert_dir( Idx idx , Char sep ) {
			ULock lock{_mutex}           ;
			int   pos = -1/*not_found*/ ;              // ChunkIdx is unsigned and we need a signed type to simplify arithmetic
			for(; +idx ; idx = _at(idx).prev ) {
				Item const& item     = _at(idx)      ;
				ChunkIdx    chunk_sz = item.chunk_sz ;
				if (pos==0) {                          // found in last position on previous chunk, so we just have to output the usable chunk we find
					if (item.used) return idx       ;
					if (chunk_sz ) return _use(idx) ;
				} else {
					for( pos=chunk_sz-1 ; pos>=0 ; pos-- ) if (item.chunk(pos)==sep) break ;
					if (pos>0) return _cut(idx,pos) ;
				}
			}
			return Idx() ;                             // sep not found
		}

	template<bool AutoLock,class Hdr,class Idx,class Char,class Data,bool Reverse>
		typename MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::IdxSz MultiPrefixFile<AutoLock,Hdr,Idx,Char,Data,Reverse>::_chk( Idx idx , bool recurse_backward , bool recurse_forward ) const {
			throw_unless(+idx      ,"idx ",idx," is null"                     ) ;
			throw_unless(idx<size(),"idx ",idx," is out of range (",size(),')') ;
			Item const& item = _at(idx)  ;
			IdxSz       res  = item.used ;
			// root may not be minimized as it must stay prepared to hold its info w/o moving
			if (+item.prev) throw_unless(item.sz()==item.min_sz()  ,"item has size",item.sz(),"not minimum (",item.min_sz()  ,')') ;
			else            throw_unless(item.sz()==Item::MinUsedSz,"root has size",item.sz(),"!="           ,Item::MinUsedSz    ) ;
			if (!item.prev) throw_unless(!item.chunk_sz            ,"root must not have an empty chunk"                          ) ;
			for( bool is_eq : Nxt(item.kind()) ) {
				Idx         nxt      = item.nxt_if(is_eq) ;
				Item const& nxt_item = _at(nxt) ;
				throw_unless(+nxt                      ,"item(",idx,").nxt(",is_eq,") is null"                          ) ;
				throw_unless(nxt<size()                ,"item(",idx,").nxt(",is_eq,") is out of range (",size(),')'     ) ;
				throw_unless(nxt_item.prev==idx        ,"item(",idx,").nxt(",is_eq,").prev is "     ,nxt_item.prev      ) ;
				throw_unless(nxt_item.prev_is_eq==is_eq,"item(",idx,").nxt(",is_eq,").prev_is_eq is",nxt_item.prev_is_eq) ;
				if (item.kind()==Kind::Split) {
					CharUint nxt_first ;
					if ( nxt_item.kind()==Kind::Split && !nxt_item.chunk_sz ) {
						throw_unless(item.cmp_bit<nxt_item.cmp_bit,"item(",idx,").cmp_bit (",item.cmp_bit,") is not lower than .nxt(",is_eq,").cmp_bit (",nxt_item.cmp_bit,')') ;
						nxt_first = nxt_item.cmp_val() ;
					} else {
						nxt_first = Prefix::rep(nxt_item.chunk(0)) ;
					}
					if (is_eq) throw_unless(Item::s_cmp_bit(item.cmp_val(),nxt_first)> item.cmp_bit,"item(",idx,").cmp_val is incompatible with .nxt(true).chunk(0) (",nxt_first,')') ;
					else       throw_unless(Item::s_cmp_bit(item.cmp_val(),nxt_first)==item.cmp_bit,"item(",idx,").cmp_val is incompatible with .nxt(true).chunk(0) (",nxt_first,')') ;
				}
				if (recurse_forward) res += _chk(nxt,false/*recurse_backward*/,true/*recurse_forward*/) ;
			}
			if (+item.prev) {
				Idx         prev      = item.prev ;
				Item const& prev_item = _at(prev) ;
				throw_unless(prev_item.nxt_if(item.prev_is_eq)==idx,"item(",idx,").prev.nxt(",item.prev_is_eq,") is ",prev_item.nxt_if(item.prev_is_eq)) ;
				CharUint first = Prefix::rep(item.chunk(0)) ;
				switch (item.kind()) {
					case Kind::Terminal :
						throw_unless(item.used    ,"item(",idx,") is Terminal and not used"    ) ; // unused Terminal is only accepted to represent an empty tree
						throw_unless(item.chunk_sz,"item(",idx,") is Terminal with empty chunk") ; // empty Terminal is only accepted to represent empty string at root as only element
					break ;
					case Kind::Prefix :
						throw_unless(item.chunk_sz,"item(",idx,") is Prefix with empty chunk") ;   // empty Prefix is only accepted at root
						if (!item.used) {
							Item const& nxt = _at(item.nxt()) ;
							// should have been compressed, (as not root)
							throw_unless( item.chunk_sz+nxt.chunk_sz>nxt.max_chunk_sz() ,"item(",idx,").chunk_sz (",item.chunk_sz,") makes it mergeable with .nxt.chunk_sz (",nxt.max_chunk_sz(),')' ) ;
						}
					break ;
					case Kind::Split :
						if (!item.chunk_sz) {
							Item const& prev_item = _at(prev) ;
							first = item.cmp_val() ;
							if (prev_item.kind()==Kind::Split)
								throw_unless(prev_item.cmp_bit<item.cmp_bit,"item(",idx,").prev.cmp_bit (",prev_item.cmp_bit,") is not lower than .cmp_bit (",item.cmp_bit,')') ;
						}
					break ;
				DF}
				if (prev_item.kind()==Kind::Split) {
					if (item.prev_is_eq) throw_unless(Item::s_cmp_bit(prev_item.cmp_val(),first)> prev_item.cmp_bit,"item(",idx,").prev.cmp_val is incompatible with .chunk(0) (",first,')') ;
					else                 throw_unless(Item::s_cmp_bit(prev_item.cmp_val(),first)==prev_item.cmp_bit,"item(",idx,").prev.cmp_val is incompatible with .chunk(0) (",first,')') ;
				}
				if (recurse_backward) res += _chk(prev,true/*recurse_backward*/,false/*recurse_forward*/) ;
			} else {
				throw_unless(item.prev_is_eq,"item(",idx,") is root with !prev_is_eq") ;
			}
			return res ;
		}

	//
	// SinglePrefixFile
	//

	template<bool AutoLock,class Hdr_,class Idx_,class Char_=char,class Data_=void,bool Reverse_=false> struct SinglePrefixFile
	:	             MultiPrefixFile< AutoLock , Hdr_ , Idx_ , Char_ , Data_ , Reverse_ >
	{	using Base = MultiPrefixFile< AutoLock , Hdr_ , Idx_ , Char_ , Data_ , Reverse_ > ;
		using Hdr     = Hdr_                   ;
		using Idx     = typename Base::Idx     ;
		using Char    = typename Base::Char    ;
		using DataNv  = typename Base::DataNv  ;
		using VecView = typename Base::VecView ;
		using StrView = typename Base::StrView ;
		using ULock   = typename Base::ULock   ;
		using HdrNv   = NoVoid<Hdr>            ;
		using Base::HasData ;
		using Base::IsStr   ;
		using Base::clear   ;
		using Base::_mutex  ;
		static constexpr bool HasHdr = !is_void_v<Hdr> ;
		static constexpr Idx  Root   {1}               ;
		Idx emplace_root() = delete ;
		// cxtors
		using Base::Base ;
		/**/                 SinglePrefixFile(                                                        ) = default ;
		template<class... A> SinglePrefixFile( NewType                              , A&&... hdr_args ) { init( New             , ::forward<A>(hdr_args)... ) ; }
		template<class... A> SinglePrefixFile( ::string const& name , bool writable , A&&... hdr_args ) { init( name , writable , ::forward<A>(hdr_args)... ) ; }
		template<class... A> void init( NewType , A&&... hdr_args ) {
			init( "" , true , ::forward<A>(hdr_args)... ) ;
		}
		template<class... A> void init( ::string const& name , bool writable , A&&... hdr_args ) {
			Base::init( name , writable , ::forward<A>(hdr_args)... ) ;
			if (!*this) _boot() ;
		}
	private :
		void _boot() {
			Idx root = Base::emplace_root() ;
			SWEAR( root==Root , root ) ;
		}
		// services
	public :
		//
		void               clear()       { ULock lock{_mutex} ; _clear() ; }
		typename Base::Lst lst  () const { return Base::lst(Root) ;        }
		void               chk  () const {        Base::chk(Root) ;        }
		//
		Idx                       search   ( VecView const& n , VecView const& psfx={} ) const                              { return Base::search   ( Root , n , psfx ) ; }
		DataNv      *             search_at( VecView const& n , VecView const& psfx={} )       requires( HasData          ) { return Base::search_at( Root , n , psfx ) ; }
		DataNv const*             search_at( VecView const& n , VecView const& psfx={} ) const requires( HasData          ) { return Base::search_at( Root , n , psfx ) ; }
		Idx                       insert   ( VecView const& n , VecView const& psfx={} )                                    { return Base::insert   ( Root , n , psfx ) ; }
		DataNv      &             insert_at( VecView const& n , VecView const& psfx={} )                                    { return Base::insert_at( Root , n , psfx ) ; }
		Idx                       erase    ( VecView const& n , VecView const& psfx={} )                                    { return Base::erase    ( Root , n , psfx ) ; }
		Idx                       search   ( StrView const& n , StrView const& psfx={} ) const requires(            IsStr ) { return Base::search   ( Root , n , psfx ) ; }
		DataNv      *             search_at( StrView const& n , StrView const& psfx={} )       requires( HasData && IsStr ) { return Base::search_at( Root , n , psfx ) ; }
		DataNv const*             search_at( StrView const& n , StrView const& psfx={} ) const requires( HasData && IsStr ) { return Base::search_at( Root , n , psfx ) ; }
		Idx                       insert   ( StrView const& n , StrView const& psfx={} )       requires(            IsStr ) { return Base::insert   ( Root , n , psfx ) ; }
		DataNv      &             insert_at( StrView const& n , StrView const& psfx={} )       requires(            IsStr ) { return Base::insert_at( Root , n , psfx ) ; }
		Idx                       erase    ( StrView const& n , StrView const& psfx={} )       requires(            IsStr ) { return Base::erase    ( Root , n , psfx ) ; }
		::pair<Idx,size_t/*pos*/> longest  ( VecView const& n , VecView const& psfx={} ) const                              { return Base::longest  ( Root , n , psfx ) ; }
		::pair<Idx,size_t/*pos*/> longest  ( StrView const& n , StrView const& psfx={} ) const requires(            IsStr ) { return Base::longest  ( Root , n , psfx ) ; }
	protected :
		void _clear() { Base::_clear() ; _boot() ; }
	} ;

}
