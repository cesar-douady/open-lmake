// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

template<class T> struct Serdeser ;

template<class S> concept IsOStream = ::is_base_of_v<::string     ,S> ;
template<class S> concept IsIStream = ::is_base_of_v<::string_view,S> ;
template<class S> concept IsStream  = IsIStream<S> || IsOStream<S>    ;
//
template<class T> concept HasSerdes   =                  requires( T x , ::string& os , ::string_view& is ) { x.serdes             (os  ) ;  x.serdes             (is  ) ; } ;
template<class T> concept HasSerdeser = !HasSerdes<T> && requires( T x , ::string& os , ::string_view& is ) { Serdeser<T>::s_serdes(os,x) ;  Serdeser<T>::s_serdes(is,x) ; } ;

template<class T> concept Serializable = HasSerdes<T> || HasSerdeser<T> ;

// serdes method should be const when serializing but is not because C++ does not let constness be template arg dependent
template< IsOStream S , HasSerdes   T > void serdes( S& os , T  const& x                                   ) { const_cast<T&>(x).serdes(os) ;                }
template< IsIStream S , HasSerdes   T > void serdes( S& is , T       & x                                   ) {                x .serdes(is) ;                }
template< IsOStream S , HasSerdeser T > void serdes( S& os , T  const& x                                   ) { Serdeser<T>::s_serdes(os,const_cast<T&>(x)) ; }
template< IsIStream S , HasSerdeser T > void serdes( S& is , T       & x                                   ) { Serdeser<T>::s_serdes(is,               x ) ; }
//
template< IsOStream S , Serializable T1 , Serializable T2 , Serializable... Ts > void serdes( S& os , T1 const& x1 , T2 const& x2 , Ts const&... xs ) { serdes(os,x1) ; serdes(os,x2,xs...) ; }
template< IsIStream S , Serializable T1 , Serializable T2 , Serializable... Ts > void serdes( S& is , T1      & x1 , T2      & x2 , Ts      &... xs ) { serdes(is,x1) ; serdes(is,x2,xs...) ; }
//
template< Serializable T , IsOStream S > void     serialize  ( S& os , T const& x ) {                serdes(os ,x  ) ;              }
template< Serializable T               > ::string serialize  (         T const& x ) { ::string res ; serdes(res,x  ) ; return res ; }
template< Serializable T , IsIStream S > void     deserialize( S& is , T      & x ) { x = {}       ; serdes(is ,x  ) ;              }
template< Serializable T , IsIStream S > T        deserialize( S& is              ) { T        res ; serdes(is ,res) ; return res ; }
//
template< Serializable T , IsOStream S > void serialize  ( S            && os , T const& x ) {        serialize  <T>(os              ,x) ; }
template< Serializable T , IsIStream S > void deserialize( S            && is , T      & x ) {        deserialize<T>(is              ,x) ; }
template< Serializable T , IsIStream S > T    deserialize( S            && is              ) { return deserialize<T>(is                ) ; }
template< Serializable T               > T    deserialize( ::string const& s               ) { return deserialize<T>(::string_view(s)  ) ; }
template< Serializable T               > void deserialize( ::string const& s  , T      & x ) {        deserialize<T>(::string_view(s),x) ; }

// make objects hashable as soon as they define serdes
// as soon as a class T is serializable, you can simply use ::set<T>, ::uset<T>, ::map<T,...> or ::umap<T,...>
// /!\ : not ideal in terms of performances, but easy to use.
// suppress calls to FAIL when necessary
template<HasSerdes T> bool              operator== ( T const& a , T const& b ) { FAIL();return serialize(a)== serialize(b) ; }        // cannot define for Serializable as it creates conflicts
template<HasSerdes T> ::strong_ordering operator<=>( T const& a , T const& b ) { FAIL();return serialize(a)<=>serialize(b) ; }        // .
namespace std {
	template<HasSerdes T> struct hash<T> { size_t operator()(T const& x) const { FAIL();return hash<::string>()(serialize(x)) ; } } ; // .
}

template<class T> requires( ::is_aggregate_v<T> && !::is_trivially_copyable_v<T> ) struct Serdeser<T> {
	struct U { template<class X> operator X() const ; } ;                                               // a universal class that can be cast to anything
	template<IsStream S> static void s_serdes( S& s , T& x ) {
		if      constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()};}) { U(0) ; }    // force compilation error to ensure we do not partially serialize a large class
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [a,b,c,d,e,f,g,h,i,j,k] = x ; serdes(s,a,b,c,d,e,f,g,h,i,j,k) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [a,b,c,d,e,f,g,h,i,j  ] = x ; serdes(s,a,b,c,d,e,f,g,h,i,j  ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U()            };}) { auto& [a,b,c,d,e,f,g,h,i    ] = x ; serdes(s,a,b,c,d,e,f,g,h,i    ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U()                };}) { auto& [a,b,c,d,e,f,g,h      ] = x ; serdes(s,a,b,c,d,e,f,g,h      ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U()                    };}) { auto& [a,b,c,d,e,f,g        ] = x ; serdes(s,a,b,c,d,e,f,g        ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U()                        };}) { auto& [a,b,c,d,e,f          ] = x ; serdes(s,a,b,c,d,e,f          ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U()                            };}) { auto& [a,b,c,d,e            ] = x ; serdes(s,a,b,c,d,e            ) ; }
		else if constexpr (requires{T{U(),U(),U(),U()                                };}) { auto& [a,b,c,d              ] = x ; serdes(s,a,b,c,d              ) ; }
		else if constexpr (requires{T{U(),U(),U()                                    };}) { auto& [a,b,c                ] = x ; serdes(s,a,b,c                ) ; }
		else if constexpr (requires{T{U(),U()                                        };}) { auto& [a,b                  ] = x ; serdes(s,a,b                  ) ; }
		else if constexpr (requires{T{U()                                            };}) { auto& [a                    ] = x ; serdes(s,a                    ) ; }
	}
} ;

template<class T> requires(::is_trivially_copyable_v<T>) struct Serdeser<T> {
	static void s_serdes( ::string     & os , T const& x ) { os += ::string_view( reinterpret_cast<char const*>(&x) , sizeof(x) ) ; }
	static void s_serdes( ::string_view& is , T      & x ) {
		SWEAR(is.size()>=sizeof(x),is.size(),sizeof(x)) ;
		::memcpy( reinterpret_cast<char*>(&x) , is.data() , sizeof(x) ) ;
		is = is.substr(sizeof(x)) ;
	}
} ;

// /!\ dont use size_t in serialized stream to make serialization interoperable between 32 bits and 64 bits
template<class T> static uint32_t _sz32(T const& v) {
	uint32_t res = v.size() ; SWEAR(res==v.size()) ;  // uint32_t is already very comfortable, no need to use 64 bits for lengths
	return res ;
}

template<> struct Serdeser<::string> {
	static void s_serdes( ::string     & os , ::string const& s ) { uint32_t sz=_sz32(s) ; serdes(os,sz) ; os += s ; }
	static void s_serdes( ::string_view& is , ::string      & s ) {
		uint32_t sz ; serdes(is,sz) ;
		SWEAR(is.size()>=sz,is.size(),sz) ;
		s  = is.substr(0 ,sz) ;
		is = is.substr(sz   ) ;
	}
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
	template<IsOStream S> static void s_serdes( S& os , ::vector<T> const& v ) { serdes(os,_sz32(v)) ;                 for( T const& x : v ) serdes(os,x) ; }
	template<IsIStream S> static void s_serdes( S& is , ::vector<T>      & v ) { v.resize(deserialize<uint32_t>(is)) ; for( T      & x : v ) serdes(is,x) ; }
} ;

template<class T> struct Serdeser<::set<T>> {
	template<IsOStream S> static void s_serdes( S& os , ::set<T> const& s ) { serdes(os,_sz32(s)) ; for( T const& x : s ) serdes(os,x)  ;                                         }
	template<IsIStream S> static void s_serdes( S& is , ::set<T>      & s ) { for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) s.insert(deserialize<T>(is  )) ; }
} ;

template<class T> struct Serdeser<::uset<T>> {
	template<IsOStream S> static void s_serdes( S& os , ::uset<T> const& s ) { serdes(os,_sz32(s)) ; for( T const& x : s ) serdes(os,x)  ;                                         }
	template<IsIStream S> static void s_serdes( S& is , ::uset<T>      & s ) { for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) s.insert(deserialize<T>(is  )) ; }
} ;

template<class K,class V> struct Serdeser<::map<K,V>> {
	template<IsOStream S> static void s_serdes( S& os , ::map<K,V> const& m ) { serdes(os,_sz32(m)) ; for( ::pair<K const,V> const& p : m ) serdes(os,p)  ;                                   }
	template<IsIStream S> static void s_serdes( S& is , ::map<K,V>      & m ) { for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) m.insert(deserialize<::pair<K,V>>(is  )) ; }
} ;

template<class K,class V> struct Serdeser<::umap<K,V>> {
	template<IsOStream S> static void s_serdes( S& os , ::umap<K,V> const& m ) { serdes(os,_sz32(m)) ; for( ::pair<K const,V> const& p : m ) serdes(os,p)  ;                                   }
	template<IsIStream S> static void s_serdes( S& is , ::umap<K,V>      & m ) { for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) m.insert(deserialize<::pair<K,V>>(is  )) ; }
} ;

template<class T,class U> struct Serdeser<::pair<T,U>> {
	template<IsOStream S> static void s_serdes( S& s , ::pair<T,U> const& p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
	template<IsIStream S> static void s_serdes( S& s , ::pair<T,U>      & p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
} ;
