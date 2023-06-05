// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "alloc.hh"

namespace Store {

	// XXX : always consistency not implemented

	//
	// MultiRedBlackFile  : multi-root red-black file
	// SingleRedBlackFile : single-root red-black file
	//
	// Principle of the red-black tree :
	// - it is a binary tree
	// - each item is either red or black
	// - the number of black items is identical along all branches
	// - a red item cannot have a red parent
	// - root is always black (this is optional, but easier to code)
	// To maintain invariants upon insertion, a item is first inserted red (which respects the 1st invariant) then items are recolored/rotated to ensure 2nd invariant.
	// This way, you can guarantee that a branch is at most twice as long as another, which guarantees log(n) search and insert
	// a good full explanation can be found here : https://www.geeksforgeeks.org/red-black-tree-set-1-introduction-2

	namespace RedBlack {
		template<class Idx,class Key,class Data=void> struct Item {
			template<class... A> Item( bool black_ , Key const& key_ , bool bit_ , A&&... args ) : _subs() , key(key_) , data(::forward<A>(args)...) { black(black_) ; bit(bit_) ; }
			Idx  subs ( bool left         ) const { return _subs[left].val     ; }
			void subs ( bool left , Idx v )       {        _subs[left].val = v ; }
			bool black(                   ) const { return _subs[0   ].bit     ; }
			void black( bool b            )       {        _subs[0   ].bit = b ; }
			bool bit  (                   ) const { return _subs[1   ].bit     ; }
			void bit  ( bool b            )       {        _subs[1   ].bit = b ; }
			// data
		private :
			struct {
				Idx val:NBits<Idx>-1 = 0 ;
				Idx bit:           1 = 0 ;
			} _subs[2] ;
			static_assert( sizeof(_subs[0]) == sizeof(Idx) ) ;                 // ensure space is not wasted
		public :
			[[                 ]] Key          key  ;
			[[no_unique_address]] NoVoid<Data> data ;
		} ;
	}

	//
	// MultiRedBlackFile
	//

	template<class Hdr_,class Idx_,class Key_,class Data_=void,bool BitIsKey_=false>
		struct MultiRedBlackFile
		:	             AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , RedBlack::Item<Idx_,Key_,Data_> >   // we need idx 0 for left/right management
		{	using Base = AllocFile< false/*AutoLock*/ , Hdr_ , Idx_ , RedBlack::Item<Idx_,Key_,Data_> > ;
			using Item =                          RedBlack::Item<Idx_,Key_,Data_>   ;
			using Hdr    = Hdr_         ;
			using Idx    = Idx_         ;
			using Key    = Key_         ;
			using Data   = Data_        ;
			static_assert(!is_void_v<Key>) ;
			static constexpr bool BitIsKey = BitIsKey_          ;
			static constexpr bool HasData  = !::is_void_v<Data> ;
			using DataNv = NoVoid<Data> ;
			//
			using Base::size  ;
			using Base::clear ;
			// cxtors
			using Base::Base ;
			// accesses
			Key    const& key  ( Idx idx          ) const                     { return Base::at(idx).key           ; }
			DataNv const& at   ( Idx idx          ) const requires(HasData  ) { return Base::at(idx).data          ; }
			DataNv      & at   ( Idx idx          )       requires(HasData  ) { return Base::at(idx).data          ; }
			DataNv const& c_at ( Idx idx          ) const requires(HasData  ) { return Base::at(idx).data          ; }
			bool          bit  ( Idx idx          ) const                     { return Base::at(idx).bit( )        ; }
			void          bit  ( Idx idx , bool b )       requires(!BitIsKey) {        Base::at(idx).bit(b)        ; }
			void          clear( Idx idx          )       requires(HasData  ) {        Base::at(idx).data = Data() ; }
		private :
			Item const& _at(Idx idx) const { return Base::at(idx) ; }
			Item      & _at(Idx idx)       { return Base::at(idx) ; }
		public :
			::vector<Idx> lst(Idx root) const {                                // XXX : implement iterator rather than vector
				::vector<Idx> res ;
				_append_lst(res,root) ;
				return res ;
			}
		private :
			void _append_lst( ::vector<Idx>& idx_lst/*out*/ , Idx idx ) const {
				if (!idx) return ;
				Item const& item = _at(idx) ;
				_append_lst( idx_lst/*out*/ , item.subs(true /*left*/) ) ;
				idx_lst.push_back(idx) ;
				_append_lst( idx_lst/*out*/ , item.subs(false/*left*/) ) ;
			}
			// services
			// cannot provide insert_data as insert requires unlocked while data requires locked
		public :
			template<class... A> Idx emplace(             Key const& key , bool bit , A&&... args ) requires( BitIsKey) { return Base::emplace(true,key,bit  ,::forward<A>(args)...) ; } // root
			template<class... A> Idx emplace(             Key const& key ,            A&&... args ) requires(!BitIsKey) { return Base::emplace(true,key,false,::forward<A>(args)...) ; } // root
			template<class... A> Idx insert ( Idx& root , Key const& key , bool bit , A&&... args ) requires( BitIsKey) { return _insert      (root,key,bit  ,::forward<A>(args)...) ; }
			template<class... A> Idx insert ( Idx& root , Key const& key ,            A&&... args ) requires(!BitIsKey) { return _insert      (root,key,false,::forward<A>(args)...) ; }
			//
			Idx           search   ( Idx  root , Key const& key , bool bit ) const requires(  BitIsKey            ) { return      _search(root,key,bit  )  ; }
			Idx           search   ( Idx  root , Key const& key            ) const requires( !BitIsKey            ) { return      _search(root,key,false)  ; }
			DataNv const* search_at( Idx  root , Key const& key , bool bit ) const requires(  BitIsKey && HasData ) { if(Idx idx= _search(root,key,bit  )) return &at(idx) ; else return nullptr ; }
			DataNv const* search_at( Idx  root , Key const& key            ) const requires( !BitIsKey && HasData ) { if(Idx idx= _search(root,key,false)) return &at(idx) ; else return nullptr ; }
			DataNv      * search_at( Idx  root , Key const& key , bool bit )       requires(  BitIsKey && HasData ) { if(Idx idx= _search(root,key,bit  )) return &at(idx) ; else return nullptr ; }
			DataNv      * search_at( Idx  root , Key const& key            )       requires( !BitIsKey && HasData ) { if(Idx idx= _search(root,key,false)) return &at(idx) ; else return nullptr ; }
			void          pop      ( Idx  root                             )                                        {        Base::pop   (root                  ) ; }
			void          pop      ( Idx  root , Idx idx                   )       requires(  BitIsKey            ) {             _erase (root,key(idx),bit(idx)) ; }
			void          pop      ( Idx  root , Idx idx                   )       requires( !BitIsKey            ) {             _erase (root,key(idx),false   ) ; }
			Idx           erase    ( Idx& root , Key const& key , bool bit )       requires(  BitIsKey            ) { return      _erase (root,key     ,bit     ) ; }
			Idx           erase    ( Idx& root , Key const& key            )       requires( !BitIsKey            ) { return      _erase (root,key     ,false   ) ; }
			//
			void chk(Idx root) const {
				Base::chk() ;
				SWEAR(_is_black(root)) ;
				_chk(root) ;
			}
		private :
			uint8_t _chk(            Idx idx , bool chk_color=true ) const                     { ::uset<Idx> seen ;                            return _chk(idx,seen,chk_color) ; }
			uint8_t _chk( Idx root , Idx idx , bool chk_color=true ) const requires( BitIsKey) { SWEAR(_search(root,key(idx),bit(idx))==idx) ; return _chk(idx,     chk_color) ; }
			uint8_t _chk( Idx root , Idx idx , bool chk_color=true ) const requires(!BitIsKey) { SWEAR(_search(root,key(idx)         )==idx) ; return _chk(idx,     chk_color) ; }
			uint8_t _chk( Idx idx , ::uset<Idx>& seen , bool chk_color=true ) const ;
			bool _is_black( Idx idx ) const {                                  // null items are deemed black
				return !idx || _at(idx).black() ;
			}
			// perform rotation, return new origin
			Idx _rot( Idx& root , Idx idx , bool left1 , bool left2 ) {
				Idx   idx1  =  idx ? _at(idx).subs(left1) : root ;
				Item& item1 = _at(idx1) ;
				Idx   idx2  = item1.subs(left2) ;
				Item& item2 = _at(idx2) ;
				item1.subs(  left2 , item2.subs(!left2) ) ;
				item2.subs( !left2 , idx1               ) ;
				_fix_parent( root , idx , left1 , idx2 ) ;
				return idx2 ;
			}
			void _fix_parent( Idx& root , Idx parent , bool left , Idx son ) {
				if (parent) {
					_at(parent).subs( left , son ) ;
				} else {
					if (son) _at(son).black(true) ;                            // root is always black
					root = son ;
				}
			}
			void _fix_parent( Idx& root , ::vmap<Idx,bool/*left*/> const& path , uint8_t lvl , Idx son ) {
				if (lvl) _fix_parent( root , path[lvl-1].first , path[lvl-1].second , son ) ;
				else     _fix_parent( root , 0                 , false/*unused*/    , son ) ;
			}
			template<bool Record> Idx _search_path( ::vmap<Idx,bool/*left*/>& path/*out*/ , Idx root  , Key const& key , bool bit ) const ; // search a key starting at root
			Idx _search( Idx const& root , Key const& key , bool bit ) const { // search a key starting at root, return 0 if not found
				static ::vmap<Idx,bool/*left*/> _ ;
				return _search_path<false/*record*/>(_,root,key,bit) ;
			}
			template<class... A> Idx _emplace(             Key const& key , bool bit , A&&... args ) { return Base::emplace(key,bit,::forward<A>(args)...) ; }
			template<class... A> Idx _insert ( Idx& root , Key const& key , bool bit , A&&... args ) ; // search a key starting at root, insert if not found (which may modify root)
			/**/                 Idx _erase  ( Idx& root , Key const& key , bool bit               ) ; // search a key starting at root, erase  if     found (which may modify root)
		} ;

	// search item and output path to it in path
	template<class Hdr,class Idx,class Key,class Data,bool BitIsKey>
		template<bool Record>
			Idx MultiRedBlackFile<Hdr,Idx,Key,Data,BitIsKey>::_search_path( ::vmap<Idx,bool/*left*/>& path/*out*/ , Idx root , Key const& key , bool bit ) const {
				Idx cur ;
				for( cur = root ; cur ; ) {
					Item const& cur_item = _at(cur) ;
					bool eq_key = key==cur_item.key ;
					if ( eq_key && ( !BitIsKey || bit==cur_item.bit() ) ) break ;                  // found
					bool left = key<cur_item.key || ( BitIsKey && eq_key && bit<cur_item.bit() ) ;
					if (Record) path.emplace_back(cur,left) ;
					cur = cur_item.subs(left) ;
				}
				return cur ;
			}

	// for _insert & _erase tree are described as follows :
	// - grouping is shown using space and () if necessary
	// - red nodes are capitalized.
	// - . are used instead of letters if node color (and existence) is unknown.
	// - remember that null nodes are deemed black

	template<class Hdr,class Idx,class Key,class Data,bool BitIsKey>
		uint8_t MultiRedBlackFile<Hdr,Idx,Key,Data,BitIsKey>::_chk( Idx idx , ::uset<Idx>& seen , bool chk_color ) const {
			if (!idx) return 1 ; // null are deemed black
			SWEAR(idx<size()         ) ;
			SWEAR(!seen.contains(idx)) ;
			seen.insert(idx) ;
			Key  const& key_        = _at(idx).key               ;
			bool        black       = _is_black(idx)             ;
			Idx         left        = _at(idx).subs(true )       ;
			Idx         right       = _at(idx).subs(false)       ;
			uint8_t     left_depth  = _chk(left ,seen,chk_color) ;
			uint8_t     right_depth = _chk(right,seen,chk_color) ;
			if ( chk_color && !black ) SWEAR(  _is_black(left)            &&  _is_black(right)             ) ;
			/**/                       SWEAR( (!left||_at(left).key<key_) && (!right||_at(right).key>key_) ) ;
			if ( chk_color           ) SWEAR(  left_depth                 ==  right_depth                  ) ;
			return left_depth+black ;
		}
	template<class Hdr,class Idx,class Key,class Data,bool BitIsKey>
		template<class... A> Idx MultiRedBlackFile<Hdr,Idx,Key,Data,BitIsKey>::_insert( Idx& root , Key const& key , bool bit , A&&... args ) {
			::vmap<Idx,bool/*left*/> path ;
			// we do considerably more searches than inserts, so optimize this case
			if ( Idx res = _search(root,key,bit) ) return res ;
			// well, we have to insert, do the job
			_search_path<true/*record*/>(path,root,key,bit) ;
			Idx res = Base::emplace(false,key,bit,::forward<A>(args)...) ;     // invalidate references as underlying mapping may move
			_fix_parent( root , path , path.size() , res ) ;
			// recolor/rotate
			Idx xi = res ;
			for( uint8_t lvl=path.size() ; lvl>=2 ; lvl-=2 ) {                 // we walk directly to grand-parent when we need to loop
				uint8_t pl = lvl-1 ;                                           // level of parent
				uint8_t gl = lvl-2 ;                                           // level of grand-parent
				// think as if p was on the left of g, the other case is symetrical
				bool left = path[gl].second ;
				Idx  gi   = path[gl].first  ; Item& g = _at(gi) ;              // grand-parent
				Idx  pi   = path[pl].first  ; Item& p = _at(pi) ;              // parent
				Idx  ui   = g.subs(!left)   ;                                  // uncle
				SWEAR(!_at(xi).black()) ;                                      // if x was black, we should not be here
				if (p.black()) break ;                                         // invariant is ok
				SWEAR(g.black()) ;                                             // because of invariant (red's parents cannot be red and p is red)
				if (_is_black(ui)) {
					// case 1 : (. P .X.) g u -> .P. x .Gu
					// case 2 : (.X. P .) g u -> .X. p .Gu
					if (path[pl].second!=left) _rot( root ,    path[pl-1].first   ,    path[pl-1].second       , !left ) ; // case 1 : (. P .X.) g u -> (.P. X .) g u -> .P. X .gu
					Idx bi =                   _rot( root , gl?path[gl-1].first:0 , gl?path[gl-1].second:false ,  left ) ; // case 2 : (.X. P .) g u -> .X. P .gu
					g      .black(false) ;                                     // case 1 : .P. X .gu -> .P. X .Gu // case 2 :  .X. P .gu -> .X. P .Gu
					_at(bi).black(true ) ;                                     // case 1 : .P. X .Gu -> .P. x .Gu // case 2 :  .X. P .Gu -> .X. p .Gu
					break ;
				}
				// p is red, u is red, recoloring is enough but we need to walk up
				g      .black(!gl ) ;                                          // if we are root then we can keep it black with no consequence and we need respect invariants
				p      .black(true) ;
				_at(ui).black(true) ;
				xi = gi ;                                                      // walk to grand-parent as both x and p have been sorted out
			}
			return res ;
		}

	template<class Hdr,class Idx,class Key,class Data,bool BitIsKey>
		Idx MultiRedBlackFile<Hdr,Idx,Key,Data,BitIsKey>::_erase( Idx& root , Key const& key , bool bit ) {
			::vmap<Idx,bool/*left*/> path    ;
			Idx                      res_idx = _search_path<true/*record*/>(path,root,key,bit) ;
			if (!res_idx) return 0 ;
			Item&   res         = _at(res_idx) ;
			uint8_t res_lvl     = path.size()  ;
			bool    extra_black ;
			if ( Idx son_idx = res.subs(true/*left*/) ) {                      // if necessary, find predecessor (walk left, then always right) & put it lieu of res
				// res has a left son :
				// - find predecessor (called last as it terminal in the sens that it has a null son)
				// - extend path
				// - move it in lieu of res and suppress it from its original place
				// this way, net result is suppression of res and we have to manage the tree as if a terminal node was suppressed
				Idx last_idx ;
				path.emplace_back(res_idx,true/*left*/) ;
				for( Idx i=son_idx ; i ; i=_at(i).subs(false/*left*/) )
					path.emplace_back(last_idx=i,false/*left*/) ;
				path.pop_back() ;                                              // last entry is in last_idx
				Item& last = _at(last_idx) ;
				extra_black = last.black() ;                                   // gather suppressed color : because last inherits color from res, the actual suppressed color is last's one
				// suppress last
				uint8_t lvl = path.size() ;
				_at(path[lvl-1].first).subs( path[lvl-1].second , last.subs(true/*left*/) ) ; // right son of last is null : this is what makes it last
				// and move it in lieu of res
				last.black(res.black()) ;
				for( uint8_t left=0 ; left<2 ; left++ ) last.subs( left , res.subs(left) ) ;
				_fix_parent( root , path , res_lvl , last_idx ) ;
				path[res_lvl].first = last_idx ;                               // fix path to reflect updated tree
			} else {
				extra_black = res.black() ;                                      // gather suppressed color
				_fix_parent( root , path , res_lvl , res.subs(false/*left*/) ) ; // suppress res, left son is null or we would not be here
			}
			// situation is the following :
			// - we have suppressed the element past path whose color is held by extra_black
			// - we have to rebalance the tree until extra_black is solved
			for( uint8_t lvl=path.size() ; extra_black && lvl ; lvl-- ) {
				// Name nodes a, b, c, d, e, f, g in key order.
				// Double black is a, its parent is b, sibling is f with sons d (with sons c & e) & g.
				// Initial situation (excluding colors) : "a b (cde f g)" if fully populated, or more precisely "abf" or "a b .f."
				// which means that a, c, e and g may have unnamed sons.
				Idx  pi    = lvl>1 ? path[lvl-2].first  : 0     ;
				bool pleft = lvl>1 ? path[lvl-2].second : false ;              // if at root, pleft is unused
				bool left  = path[lvl-1].second ;                              // trees are drawn as if left==true. If false, the tree order is reversed but algo is the same
				Idx  bi    = path[lvl-1].first  ; Item& b = _at(bi) ;
				Idx  fi    = b.subs(!left)      ; Item& f = _at(fi) ;          // f is guaranteed to exist as there must be at least 1 black on the branch
				Idx  di    = f.subs( left)      ;
				Idx  gi    = f.subs(!left)      ;
				extra_black = false ;
				if (b.black()) {
					if (f.black()) {
						if (_is_black(gi)) {
							if (_is_black(di)) {
								// a b f -> a b F and double black on b
								f.black(false) ;                               // a b f -> a b F
								extra_black = true ;                           // double black on b
							} else {
								// a b (.D. f .) -> ab. d .f.
								_rot( root , bi , !left  ,  left ) ;           // a b (.D. f .) -> a b (. D .f.)
								_rot( root , pi ,  pleft , !left ) ;           // a b (. D .f.) -> ab. D .f.
								_at(di).black(true) ;                          // ab. D .f.     -> ab. d .f.
							}
						} else {
							// a b .fG -> ab. f g
							_rot( root , pi , pleft ,!left ) ;                 // a b .fG -> ab. f G
							_at(gi).black(true) ;                              // ab. f G -> ab. f g
						}
					} else {
						Item& d = _at(di) ;                                    // there must be at least 1 black on the branch and it is not f, so d exists
						Item& g = _at(gi) ;                                    // .
						SWEAR( d.black() && g.black() ) ;                      // per invariant
						Idx ci = d.subs( left) ;
						Idx ei = d.subs(!left) ;
						if (_is_black(ei)) {
							if (_is_black(ci)) {
								// a b dFg -> abD f g
								_rot( root , pi , pleft ,!left ) ;             // a b dFg -> abd F g
								d.black(false) ;                               // abd F g -> abD F g
								f.black(true ) ;                               // abD F g -> abD f g
							} else {
								// a b ((.C. d .) F g) -> (aB. c .D.) f g "." stands for black subnodes of c
								_rot( root , pi ,  pleft , !left ) ;           // a b ((.C. d .) F g) -> (a b (.C. d .)) F g
								_rot( root , bi , !left  ,  left ) ;           // (a b (.C. d .)) F g -> (a b (. C .d.)) F g
								_rot( root , fi ,  left  , !left ) ;           // (a b (. C .d.)) F g -> (ab. C .d.)) F g
								b      .black(false) ;                         // (ab. C .d.)) F g    -> (aB. C .d.)) F g
								_at(ci).black(true ) ;                         // (aB. C .d.)) F g    -> (aB. c .d.)) F g
								d      .black(false) ;                         // (aB. c .d.)) F g    -> (aB. c .D.)) F g
								f      .black(true ) ;                         // (aB. c .D.)) F g    -> (aB. c .D.)) f g
							}
						} else {
							// a b (cdE F g) -> abc d eFg
							_rot( root , bi , !left  ,  left ) ;               // a b (cdE F g) -> a b (c d EFg)
							_rot( root , pi ,  pleft , !left ) ;               // a b (c d EFg) -> abc d EFg
							_at(ei).black(true) ;                              // abc d EFg     -> abc d eFg
						}
					}
				} else {
					SWEAR(f.black()) ;                                         // per invariant
					if (_is_black(di)) {
						if (_is_black(gi)) {
							// a B f -> a b F
							b.black(true ) ; // a B f -> a b f
							f.black(false) ; // a b f -> a b F
						} else {
							// a B .fg -> ab. F g
							_rot( root , pi , pleft ,!left ) ;                 // a B .fG -> aB. f G
							b      .black(true ) ;                             // aB. f G -> ab. f G
							f      .black(false) ;                             // ab. f G -> ab. F G
							_at(gi).black(true ) ;                             // ab. F G -> ab. F g
						}
					} else {
						// a B (.D. f .) -> aB. d .F.
						_rot( root , bi , !left  ,  left ) ;                   // a B (.D. f .) -> a B (. D .f.)
						_rot( root , pi ,  pleft , !left ) ;                   // a B (c D efg) -> aB. D .f.
						_at(di).black(true ) ;                                 // aB. D .f.     -> aB. d .f.
						f      .black(false) ;                                 // aB. d .f.     -> aB. d .F.
					}
				}
			}
			return res_idx ;
		}

	//
	// SingleRedBlackFile
	//

	namespace RedBlack {
		template<class Hdr,class Idx> struct SingleHdr {
			NoVoid<Hdr> hdr  ;
			Idx         root = 0 ;
		} ;
	}

	template<class Hdr_,class Idx_,class Key_,class Data_=void,bool BitIsKey_=false>
		struct SingleRedBlackFile
		:	                MultiRedBlackFile< RedBlack::SingleHdr<Hdr_,Idx_> , Idx_ , Key_ , Data_ , BitIsKey_ >
		{	using Base    = MultiRedBlackFile< RedBlack::SingleHdr<Hdr_,Idx_> , Idx_ , Key_ , Data_ , BitIsKey_ > ;
			using BaseHdr =                    RedBlack::SingleHdr<Hdr_,Idx_>                                     ;
			using Hdr     = Hdr_         ;
			using Idx     = Idx_         ;
			using Key     = Key_         ;
			using Data    = Data_        ;
			using HdrNv   = NoVoid<Hdr > ;
			using DataNv  = NoVoid<Data> ;
			static constexpr bool BitIsKey = BitIsKey_        ;
			static constexpr bool HasHdr   = !is_void_v<Hdr > ;
			static constexpr bool HasData  = !is_void_v<Data> ;
			using Base::key   ;
			using Base::bit   ;
			using Base::clear ;
			template<class... A> Idx emplace(Key const& key , bool bit , A&&... args ) requires( BitIsKey) = delete ;
			template<class... A> Idx emplace(Key const& key ,            A&&... args ) requires(!BitIsKey) = delete ;
			// cxtors
			using Base::Base ;
			// accesses
			::vector<Idx> lst() const                  { return Base::lst(_root()) ; }
			HdrNv const&  hdr() const requires(HasHdr) { return Base::hdr().hdr    ; }
			HdrNv      &  hdr()       requires(HasHdr) { return Base::hdr().hdr    ; }
		private :
			Idx  _root(       ) const { return Base::hdr().root       ; }      // cannot provide a reference to root and use it to insert (which requires !lock) w/o taking the lock
			void _root(Idx val)       {        Base::hdr().root = val ; }      // so provide a getter and a setter
			struct _Root {                                                     // and an easy to use accessor
				explicit _Root(SingleRedBlackFile* s) : self(s) , copy(s->_root()) {}
				~_Root() { self->_root(copy) ; }
				operator Idx&() { return copy ; }
				void operator=(Idx v) { copy = v ; }
				SingleRedBlackFile* self ;
				Idx                 copy ;
			} ;
			// services
		public :
			template<class... A> Idx insert( Key const& key , bool bit , A&&... args ) requires( BitIsKey) { _Root r{this} ; return Base::insert(r,key,bit,::forward<A>(args)...) ; }
			template<class... A> Idx insert( Key const& key ,            A&&... args ) requires(!BitIsKey) { _Root r{this} ; return Base::insert(r,key,    ::forward<A>(args)...) ; }
			//
			Idx           search     ( Key const& key , bool bit ) const requires(  BitIsKey            ) {                 return Base::search     (_root(),key,bit) ; }
			Idx           search     ( Key const& key            ) const requires( !BitIsKey            ) {                 return Base::search     (_root(),key    ) ; }
			DataNv const* search_data( Key const& key , bool bit ) const requires(  BitIsKey && HasData ) {                 return Base::search_data(_root(),key,bit) ; }
			DataNv const* search_data( Key const& key            ) const requires( !BitIsKey && HasData ) {                 return Base::search_data(_root(),key    ) ; }
			DataNv      * search_data( Key const& key , bool bit )       requires(  BitIsKey && HasData ) {                 return Base::search_data(_root(),key,bit) ; }
			DataNv      * search_data( Key const& key            )       requires( !BitIsKey && HasData ) {                 return Base::search_data(_root(),key    ) ; }
			void          pop        (                           )                                        {                        Base::pop        (_root()        ) ; }
			void          pop        ( Idx idx                   )       requires(  BitIsKey            ) {                        Base::pop        (_root(),idx    ) ; }
			void          pop        ( Idx idx                   )       requires( !BitIsKey            ) {                        Base::pop        (_root(),idx    ) ; }
			Idx           erase      ( Key const& key , bool bit )       requires(  BitIsKey            ) { _Root r{this} ; return Base::erase      (r      ,key,bit) ; }
			Idx           erase      ( Key const& key            )       requires( !BitIsKey            ) { _Root r{this} ; return Base::erase      (r      ,key    ) ; }
			void          clear      (                           )                                        { _root(0) ;             Base::clear      (               ) ; }
			void          chk        (                           ) const                                  {                        Base::chk        (_root()        ) ; }
		} ;
}
