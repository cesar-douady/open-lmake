// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

template<class T> struct Serdeser ;

template<class S> concept IsOStream = requires( S& os , ::string_view sv       ) { os += sv ;                                            } ;
template<class S> concept IsIStream = requires( S& is , char* data , size_t sz ) { is.size() ; is.copy(data,sz) ; is.remove_prefix(sz) ; } ;
template<class S> concept IsStream  = IsIStream<S> || IsOStream<S> ;
//
template<class T> concept HasSerdes   =                  requires( T x , ::string& os , ::string_view& is ) { x.serdes             (os  ) ;  x.serdes             (is  ) ; } ;
template<class T> concept HasSerdeser = !HasSerdes<T> && requires( T x , ::string& os , ::string_view& is ) { Serdeser<T>::s_serdes(os,x) ;  Serdeser<T>::s_serdes(is,x) ; } ;

template<class T> concept Serializable = HasSerdes<T> || HasSerdeser<T> ;

// serdes method should be const when serializing but is not because C++ does not let constness be template arg dependent
template< IsOStream S , HasSerdes   T > void serdes( S& os , T  const& x ) { const_cast<T&>(x).serdes(os) ;                }
template< IsIStream S , HasSerdes   T > void serdes( S& is , T       & x ) {                x .serdes(is) ;                }
template< IsOStream S , HasSerdeser T > void serdes( S& os , T  const& x ) { Serdeser<T>::s_serdes(os,const_cast<T&>(x)) ; }
template< IsIStream S , HasSerdeser T > void serdes( S& is , T       & x ) { Serdeser<T>::s_serdes(is,               x ) ; }
//
template< Serializable T , IsOStream S > void     serialize  ( S& os , T const&        x ) {                serdes(os ,x  ) ;              }
template< Serializable T               > ::string serialize  (         T const&        x ) { ::string res ; serdes(res,x  ) ; return res ; }
template< Serializable T , IsIStream S > void     deserialize( S& is , T      &/*out*/ x ) { x = {} ;       serdes(is ,x  ) ;              }
template< Serializable T , IsIStream S > T        deserialize( S& is                     ) { T res ;        serdes(is ,res) ; return res ; }
//
template< Serializable T , IsOStream S > void serialize  ( S            && os , T const&        x ) {        serialize  <T>(os              ,x) ; }
template< Serializable T , IsIStream S > void deserialize( S            && is , T      &/*out*/ x ) {        deserialize<T>(is              ,x) ; }
template< Serializable T , IsIStream S > T    deserialize( S            && is                     ) { return deserialize<T>(is                ) ; }
template< Serializable T               > void deserialize( ::string const& s  , T      &/*out*/ x ) {        deserialize<T>(::string_view(s),x) ; }
template< Serializable T               > T    deserialize( ::string const& s                      ) { return deserialize<T>(::string_view(s)  ) ; }

// make objects hashable as soon as they define serdes
// as soon as a class T is serializable, you can simply use ::set<T>, ::uset<T>, ::map<T,...> or ::umap<T,...>
// however we must ensure not to redefine hash for already hashable types
// /!\ : not ideal in terms of performances, but easy to use.
// uncomment if necessary
//template<HasSerdes T> bool              operator== ( T const& a , T const& b ) { return serialize(a)== serialize(b) ; } // NO_COV cannot define for Serializable as it creates conflicts
//template<HasSerdes T> ::strong_ordering operator<=>( T const& a , T const& b ) { return serialize(a)<=>serialize(b) ; } // NO_COV .

namespace std {                                                                                                              // cannot specialize std::hash from global namespace with gcc-11
	template<class T> requires( !requires(T t){t.hash();} && ( HasSerdes<T> || ::is_aggregate_v<T> ) ) struct hash<T> {
		size_t operator()(T const& x) const {
			return hash<::string>()(serialize(x)) ;
		}
	} ;
}

template<class T> requires(::is_empty_v<T>) struct Serdeser<T> {
	template<IsOStream S> static void s_serdes( S& , T const& ) {}
	template<IsIStream S> static void s_serdes( S& , T      & ) {}
} ;

template<class T> requires( ::is_trivially_copyable_v<T> && !::is_empty_v<T> ) struct Serdeser<T> {
	template<IsOStream S> static void s_serdes( S& os , T const& x ) {
		static_assert(!IsIStream<S>) ;                                                    // there is an ambiguity about the direction
		os += ::string_view( ::launder(reinterpret_cast<char const*>(&x)) , sizeof(x) ) ;
	}
	template<IsIStream S> static void s_serdes( S& is , T& x ) {
		static_assert(!IsOStream<S>) ;                                                    // there is an ambiguity about the direction
		if (is.size()<sizeof(x)) throw "truncated stream"s ;
		is.copy         ( ::launder(reinterpret_cast<char*>(&x)) , sizeof(x) ) ;
		is.remove_prefix(                                          sizeof(x) ) ;
	}
} ;

template<class T> requires( ::is_aggregate_v<T> && !::is_trivially_copyable_v<T> ) struct Serdeser<T> {
	struct Universal { template<class X> operator X() const ; } ;                                               // a universal class that can be cast to anything
	template<IsStream S> static void s_serdes( S& s , T& x ) {

		#define U1  Universal()
		#define U2  U1  , U1
		#define U4  U2  , U2
		#define U8  U4  , U4
		#define U16 U8  , U8
		#define U32 U16 , U16
		#
		#define A1( pfx) a##pfx
		#define A2( pfx) A1 (pfx##0) , A1 (pfx##1)
		#define A4( pfx) A2 (pfx##0) , A2 (pfx##1)
		#define A8( pfx) A4 (pfx##0) , A4 (pfx##1)
		#define A16(pfx) A8 (pfx##0) , A8 (pfx##1)
		#define A32(pfx) A16(pfx##0) , A16(pfx##1)
		#
		#define S1( pfx) serdes(s,a##pfx) ;
		#define S2( pfx) S1 (pfx##0) S1 (pfx##1)
		#define S4( pfx) S2 (pfx##0) S2 (pfx##1)
		#define S8( pfx) S4 (pfx##0) S4 (pfx##1)
		#define S16(pfx) S8 (pfx##0) S8 (pfx##1)
		#define S32(pfx) S16(pfx##0) S16(pfx##1)

		// force compilation error to ensure no partial serialization of large classes
		if      constexpr (requires{T{ U32 , U32                     };}) { Universal(0) ; } // cannot use static_assert(false) with gcc-11
		else if constexpr (requires{T{ U32 , U16 , U8 , U4 , U2 , U1 };}) { auto& [ A32() , A16() , A8() , A4() , A2() , A1() ] = x ; S32() S16() S8() S4() S2() S1() }
		else if constexpr (requires{T{ U32 , U16 , U8 , U4 , U2      };}) { auto& [ A32() , A16() , A8() , A4() , A2()        ] = x ; S32() S16() S8() S4() S2()      }
		else if constexpr (requires{T{ U32 , U16 , U8 , U4 ,      U1 };}) { auto& [ A32() , A16() , A8() , A4() ,        A1() ] = x ; S32() S16() S8() S4()      S1() }
		else if constexpr (requires{T{ U32 , U16 , U8 , U4           };}) { auto& [ A32() , A16() , A8() , A4()               ] = x ; S32() S16() S8() S4()           }
		else if constexpr (requires{T{ U32 , U16 , U8 ,      U2 , U1 };}) { auto& [ A32() , A16() , A8() ,        A2() , A1() ] = x ; S32() S16() S8()      S2() S1() }
		else if constexpr (requires{T{ U32 , U16 , U8 ,      U2      };}) { auto& [ A32() , A16() , A8() ,        A2()        ] = x ; S32() S16() S8()      S2()      }
		else if constexpr (requires{T{ U32 , U16 , U8 ,           U1 };}) { auto& [ A32() , A16() , A8() ,               A1() ] = x ; S32() S16() S8()           S1() }
		else if constexpr (requires{T{ U32 , U16 , U8                };}) { auto& [ A32() , A16() , A8()                      ] = x ; S32() S16() S8()                }
		else if constexpr (requires{T{ U32 , U16 ,      U4 , U2 , U1 };}) { auto& [ A32() , A16() ,        A4() , A2() , A1() ] = x ; S32() S16()      S4() S2() S1() }
		else if constexpr (requires{T{ U32 , U16 ,      U4 , U2      };}) { auto& [ A32() , A16() ,        A4() , A2()        ] = x ; S32() S16()      S4() S2()      }
		else if constexpr (requires{T{ U32 , U16 ,      U4 ,      U1 };}) { auto& [ A32() , A16() ,        A4() ,        A1() ] = x ; S32() S16()      S4()      S1() }
		else if constexpr (requires{T{ U32 , U16 ,      U4           };}) { auto& [ A32() , A16() ,        A4()               ] = x ; S32() S16()      S4()           }
		else if constexpr (requires{T{ U32 , U16 ,           U2 , U1 };}) { auto& [ A32() , A16() ,               A2() , A1() ] = x ; S32() S16()           S2() S1() }
		else if constexpr (requires{T{ U32 , U16 ,           U2      };}) { auto& [ A32() , A16() ,               A2()        ] = x ; S32() S16()           S2()      }
		else if constexpr (requires{T{ U32 , U16 ,                U1 };}) { auto& [ A32() , A16() ,                      A1() ] = x ; S32() S16()                S1() }
		else if constexpr (requires{T{ U32 , U16                     };}) { auto& [ A32() , A16()                             ] = x ; S32() S16()                     }
		else if constexpr (requires{T{ U32 ,       U8 , U4 , U2 , U1 };}) { auto& [ A32() ,         A8() , A4() , A2() , A1() ] = x ; S32()       S8() S4() S2() S1() }
		else if constexpr (requires{T{ U32 ,       U8 , U4 , U2      };}) { auto& [ A32() ,         A8() , A4() , A2()        ] = x ; S32()       S8() S4() S2()      }
		else if constexpr (requires{T{ U32 ,       U8 , U4 ,      U1 };}) { auto& [ A32() ,         A8() , A4() ,        A1() ] = x ; S32()       S8() S4()      S1() }
		else if constexpr (requires{T{ U32 ,       U8 , U4           };}) { auto& [ A32() ,         A8() , A4()               ] = x ; S32()       S8() S4()           }
		else if constexpr (requires{T{ U32 ,       U8 ,      U2 , U1 };}) { auto& [ A32() ,         A8() ,        A2() , A1() ] = x ; S32()       S8()      S2() S1() }
		else if constexpr (requires{T{ U32 ,       U8 ,      U2      };}) { auto& [ A32() ,         A8() ,        A2()        ] = x ; S32()       S8()      S2()      }
		else if constexpr (requires{T{ U32 ,       U8 ,           U1 };}) { auto& [ A32() ,         A8() ,               A1() ] = x ; S32()       S8()           S1() }
		else if constexpr (requires{T{ U32 ,       U8                };}) { auto& [ A32() ,         A8()                      ] = x ; S32()       S8()                }
		else if constexpr (requires{T{ U32 ,            U4 , U2 , U1 };}) { auto& [ A32() ,                A4() , A2() , A1() ] = x ; S32()            S4() S2() S1() }
		else if constexpr (requires{T{ U32 ,            U4 , U2      };}) { auto& [ A32() ,                A4() , A2()        ] = x ; S32()            S4() S2()      }
		else if constexpr (requires{T{ U32 ,            U4 ,      U1 };}) { auto& [ A32() ,                A4() ,        A1() ] = x ; S32()            S4()      S1() }
		else if constexpr (requires{T{ U32 ,            U4           };}) { auto& [ A32() ,                A4()               ] = x ; S32()            S4()           }
		else if constexpr (requires{T{ U32 ,                 U2 , U1 };}) { auto& [ A32() ,                       A2() , A1() ] = x ; S32()                 S2() S1() }
		else if constexpr (requires{T{ U32 ,                 U2      };}) { auto& [ A32() ,                       A2()        ] = x ; S32()                 S2()      }
		else if constexpr (requires{T{ U32 ,                      U1 };}) { auto& [ A32() ,                              A1() ] = x ; S32()                      S1() }
		else if constexpr (requires{T{ U32                           };}) { auto& [ A32()                                     ] = x ; S32()                           }
		else if constexpr (requires{T{       U16 , U8 , U4 , U2 , U1 };}) { auto& [         A16() , A8() , A4() , A2() , A1() ] = x ;       S16() S8() S4() S2() S1() }
		else if constexpr (requires{T{       U16 , U8 , U4 , U2      };}) { auto& [         A16() , A8() , A4() , A2()        ] = x ;       S16() S8() S4() S2()      }
		else if constexpr (requires{T{       U16 , U8 , U4 ,      U1 };}) { auto& [         A16() , A8() , A4() ,        A1() ] = x ;       S16() S8() S4()      S1() }
		else if constexpr (requires{T{       U16 , U8 , U4           };}) { auto& [         A16() , A8() , A4()               ] = x ;       S16() S8() S4()           }
		else if constexpr (requires{T{       U16 , U8 ,      U2 , U1 };}) { auto& [         A16() , A8() ,        A2() , A1() ] = x ;       S16() S8()      S2() S1() }
		else if constexpr (requires{T{       U16 , U8 ,      U2      };}) { auto& [         A16() , A8() ,        A2()        ] = x ;       S16() S8()      S2()      }
		else if constexpr (requires{T{       U16 , U8 ,           U1 };}) { auto& [         A16() , A8() ,               A1() ] = x ;       S16() S8()           S1() }
		else if constexpr (requires{T{       U16 , U8                };}) { auto& [         A16() , A8()                      ] = x ;       S16() S8()                }
		else if constexpr (requires{T{       U16 ,      U4 , U2 , U1 };}) { auto& [         A16() ,        A4() , A2() , A1() ] = x ;       S16()      S4() S2() S1() }
		else if constexpr (requires{T{       U16 ,      U4 , U2      };}) { auto& [         A16() ,        A4() , A2()        ] = x ;       S16()      S4() S2()      }
		else if constexpr (requires{T{       U16 ,      U4 ,      U1 };}) { auto& [         A16() ,        A4() ,        A1() ] = x ;       S16()      S4()      S1() }
		else if constexpr (requires{T{       U16 ,      U4           };}) { auto& [         A16() ,        A4()               ] = x ;       S16()      S4()           }
		else if constexpr (requires{T{       U16 ,           U2 , U1 };}) { auto& [         A16() ,               A2() , A1() ] = x ;       S16()           S2() S1() }
		else if constexpr (requires{T{       U16 ,           U2      };}) { auto& [         A16() ,               A2()        ] = x ;       S16()           S2()      }
		else if constexpr (requires{T{       U16 ,                U1 };}) { auto& [         A16() ,                      A1() ] = x ;       S16()                S1() }
		else if constexpr (requires{T{       U16                     };}) { auto& [         A16()                             ] = x ;       S16()                     }
		else if constexpr (requires{T{             U8 , U4 , U2 , U1 };}) { auto& [                 A8() , A4() , A2() , A1() ] = x ;             S8() S4() S2() S1() }
		else if constexpr (requires{T{             U8 , U4 , U2      };}) { auto& [                 A8() , A4() , A2()        ] = x ;             S8() S4() S2()      }
		else if constexpr (requires{T{             U8 , U4 ,      U1 };}) { auto& [                 A8() , A4() ,        A1() ] = x ;             S8() S4()      S1() }
		else if constexpr (requires{T{             U8 , U4           };}) { auto& [                 A8() , A4()               ] = x ;             S8() S4()           }
		else if constexpr (requires{T{             U8 ,      U2 , U1 };}) { auto& [                 A8() ,        A2() , A1() ] = x ;             S8()      S2() S1() }
		else if constexpr (requires{T{             U8 ,      U2      };}) { auto& [                 A8() ,        A2()        ] = x ;             S8()      S2()      }
		else if constexpr (requires{T{             U8 ,           U1 };}) { auto& [                 A8() ,               A1() ] = x ;             S8()           S1() }
		else if constexpr (requires{T{             U8                };}) { auto& [                 A8()                      ] = x ;             S8()                }
		else if constexpr (requires{T{                  U4 , U2 , U1 };}) { auto& [                        A4() , A2() , A1() ] = x ;                  S4() S2() S1() }
		else if constexpr (requires{T{                  U4 , U2      };}) { auto& [                        A4() , A2()        ] = x ;                  S4() S2()      }
		else if constexpr (requires{T{                  U4 ,      U1 };}) { auto& [                        A4() ,        A1() ] = x ;                  S4()      S1() }
		else if constexpr (requires{T{                  U4           };}) { auto& [                        A4()               ] = x ;                  S4()           }
		else if constexpr (requires{T{                       U2 , U1 };}) { auto& [                               A2() , A1() ] = x ;                       S2() S1() }
		else if constexpr (requires{T{                       U2      };}) { auto& [                               A2()        ] = x ;                       S2()      }
		else if constexpr (requires{T{                            U1 };}) { auto& [                                      A1() ] = x ;                            S1() } // XXX! : find a working way
		else if constexpr (requires{T{                               };}) {                                                                                           }
		else                                                              { Universal(0) ; } // cannot use static_assert(false) with gcc-11

		#undef U1
		#undef U2
		#undef U4
		#undef U8
		#undef U16
		#undef U32
		#
		#undef A1
		#undef A2
		#undef A4
		#undef A8
		#undef A16
		#undef A32
		#
		#undef S1
		#undef S2
		#undef S4
		#undef S8
		#undef S16
		#undef S32

	}
} ;

// /!\ dont use size_t in serialized stream to make serialization interoperable between 32 bits and 64 bits
template<class T> static uint32_t _sz32(T const& v) {
	uint32_t res = v.size() ; SWEAR(res==v.size()) ;  // uint32_t is already very comfortable, no need to use 64 bits for lengths
	return res ;
}

template<> struct Serdeser<::string> {
	template<IsOStream S> static void s_serdes( S& os , ::string const& s ) { uint32_t sz=_sz32(s) ; serdes(os,sz) ; os += ::string_view(s) ; }
	template<IsIStream S> static void s_serdes( S& is , ::string      & s ) {
		uint32_t sz ; serdes(is,sz) ;
		if (is.size()<sz) throw "truncated stream"s ;
		s.resize(sz) ;
		is.copy         ( s.data() , sz ) ;
		is.remove_prefix(            sz ) ;
	}
} ;

template<class T,class U> struct Serdeser<::pair<T,U>> {
	template<IsOStream S> static void s_serdes( S& s , ::pair<T,U> const& p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
	template<IsIStream S> static void s_serdes( S& s , ::pair<T,U>      & p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
} ;

template<class T,size_t N> struct Serdeser<T[N]> {
	template<IsOStream S> static void s_serdes( S& os , T const a[N] ) { for( size_t i : iota(N) ) serdes(os,a[i]) ; }
	template<IsIStream S> static void s_serdes( S& is , T       a[N] ) { for( size_t i : iota(N) ) serdes(is,a[i]) ; }
} ;

template<class T,size_t N> struct Serdeser<::array<T,N>> {
	template<IsOStream S> static void s_serdes( S& os , ::array<T,N> const& a ) { for( T const& x : a ) serdes(os,x) ; }
	template<IsIStream S> static void s_serdes( S& is , ::array<T,N>      & a ) { for( T      & x : a ) serdes(is,x) ; }
} ;

template<class T> struct Serdeser<::vector<T>> {
	template<IsOStream S> static void s_serdes( S& os , ::vector<T> const& v ) { serdes(os,_sz32(v)) ; for( T const& x : v ) serdes(os,x) ;                                           }
	template<IsIStream S> static void s_serdes( S& is , ::vector<T>      & v ) { for( [[maybe_unused]] size_t _ : iota(deserialize<uint32_t>(is)) ) v.push_back(deserialize<T>(is)) ; }
} ;

// special case vector<bool> as it is special cased in std (operator[] returns a fancy type)
template<> struct Serdeser<::vector<bool>> {
	template<IsOStream S> static void s_serdes( S& os , ::vector<bool> const& v ) { serdes(os,_sz32(v)) ; for( bool x : v ) serdes(os,x) ;                                                  }
	template<IsIStream S> static void s_serdes( S& is , ::vector<bool>      & v ) { for( [[maybe_unused]] size_t _ : iota(deserialize<uint32_t>(is)) ) v.push_back(deserialize<bool>(is)) ; }
} ;

template<class T> struct Serdeser<::set<T>> {
	template<IsOStream S> static void s_serdes( S& os , ::set<T> const& s ) { serdes(os,_sz32(s)) ; for( T const& x : s ) serdes(os,x)  ;                                         }
	template<IsIStream S> static void s_serdes( S& is , ::set<T>      & s ) { for( [[maybe_unused]] size_t _ : iota(deserialize<uint32_t>(is)) ) s.insert(deserialize<T>(is  )) ; }
} ;

template<class T> struct Serdeser<::uset<T>> {
	template<IsOStream S> static void s_serdes( S& os , ::uset<T> const& s ) { serdes(os,_sz32(s)) ; for( T const& x : s ) serdes(os,x)  ;                                         }
	template<IsIStream S> static void s_serdes( S& is , ::uset<T>      & s ) { for( [[maybe_unused]] size_t _ : iota(deserialize<uint32_t>(is)) ) s.insert(deserialize<T>(is  )) ; }
} ;

template<class K,class V> struct Serdeser<::map<K,V>> {
	template<IsOStream S> static void s_serdes( S& os , ::map<K,V> const& m ) { serdes(os,_sz32(m)) ; for( ::pair<K const,V> const& p : m ) serdes(os,p)  ;                                   }
	template<IsIStream S> static void s_serdes( S& is , ::map<K,V>      & m ) { for( [[maybe_unused]] size_t _ : iota(deserialize<uint32_t>(is)) ) m.insert(deserialize<::pair<K,V>>(is  )) ; }
} ;

template<class K,class V> struct Serdeser<::umap<K,V>> {
	template<IsOStream S> static void s_serdes( S& os , ::umap<K,V> const& m ) { serdes(os,_sz32(m)) ; for( ::pair<K const,V> const& p : m ) serdes(os,p)  ;                                   }
	template<IsIStream S> static void s_serdes( S& is , ::umap<K,V>      & m ) { for( [[maybe_unused]] size_t _ : iota(deserialize<uint32_t>(is)) ) m.insert(deserialize<::pair<K,V>>(is  )) ; }
} ;
