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
template< IsOStream S , HasSerdes   T > void serdes( S& os , T  const& x                                   ) { const_cast<T&>(x).serdes(os) ;                }
template< IsIStream S , HasSerdes   T > void serdes( S& is , T       & x                                   ) {                x .serdes(is) ;                }
template< IsOStream S , HasSerdeser T > void serdes( S& os , T  const& x                                   ) { Serdeser<T>::s_serdes(os,const_cast<T&>(x)) ; }
template< IsIStream S , HasSerdeser T > void serdes( S& is , T       & x                                   ) { Serdeser<T>::s_serdes(is,               x ) ; }
//
template< IsOStream S , Serializable T1 , Serializable T2 , Serializable... Ts > void serdes( S& os , T1 const& x1 , T2 const& x2 , Ts const&... xs ) { serdes(os,x1) ; serdes(os,x2,xs...) ; }
template< IsIStream S , Serializable T1 , Serializable T2 , Serializable... Ts > void serdes( S& is , T1      & x1 , T2      & x2 , Ts      &... xs ) { serdes(is,x1) ; serdes(is,x2,xs...) ; }
//
template< Serializable T , IsOStream S          > void serialize  ( S& os , T const& x ) {          serdes(os ,x  ) ;              }
template< Serializable T , IsOStream S=::string > S    serialize  (         T const& x ) { S res  ; serdes(res,x  ) ; return res ; }
template< Serializable T , IsIStream S          > void deserialize( S& is , T      & x ) { x = {} ; serdes(is ,x  ) ;              }
template< Serializable T , IsIStream S          > T    deserialize( S& is              ) { T res  ; serdes(is ,res) ; return res ; }
//
template< Serializable T , IsOStream S > void serialize  ( S            && os , T const& x ) {        serialize  <T>(os              ,x) ; }
template< Serializable T , IsIStream S > void deserialize( S            && is , T      & x ) {        deserialize<T>(is              ,x) ; }
template< Serializable T , IsIStream S > T    deserialize( S            && is              ) { return deserialize<T>(is                ) ; }
template< Serializable T               > T    deserialize( ::string const& s               ) { return deserialize<T>(::string_view(s)  ) ; }
template< Serializable T               > void deserialize( ::string const& s  , T      & x ) {        deserialize<T>(::string_view(s),x) ; }

// make objects hashable as soon as they define serdes
// as soon as a class T is serializable, you can simply use ::set<T>, ::uset<T>, ::map<T,...> or ::umap<T,...>
// however we must ensure not to redefine hash for already hashable types
// /!\ : not ideal in terms of performances, but easy to use.
// suppress calls to FAIL when necessary
template<HasSerdes T> bool              operator== ( T const& a , T const& b ) { FAIL() ; return serialize(a)== serialize(b) ; } // cannot define for Serializable as it creates conflicts
template<HasSerdes T> ::strong_ordering operator<=>( T const& a , T const& b ) { FAIL() ; return serialize(a)<=>serialize(b) ; } // .
namespace std {
	template<class T> requires( HasSerdes<T> || ::is_aggregate_v<T> ) struct hash<T> {
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
		if (is.size()<sizeof(x)) throw 0 ;
		is.copy         ( ::launder(reinterpret_cast<char*>(&x)) , sizeof(x) ) ;
		is.remove_prefix(                                          sizeof(x) ) ;
	}
} ;

template<class T> requires( ::is_aggregate_v<T> && !::is_trivially_copyable_v<T> ) struct Serdeser<T> {
	struct U { template<class X> operator X() const ; } ;                                               // a universal class that can be cast to anything
	template<IsStream S> static void s_serdes( S& s_ , T& x_ ) {
		#define U10     U(),U(),U(),U(),U(),U(),U(),U(),U(),U()
		#define U20 U10,U(),U(),U(),U(),U(),U(),U(),U(),U(),U()
		#define U30 U20,U(),U(),U(),U(),U(),U(),U(),U(),U(),U()
		#define a10     a,b,c,d,e,f,g ,h ,i ,j
		#define a20 a10,k,l,m,n,o,p,q ,r ,s ,t
		#define a30 a20,u,v,w,x,y,z,aa,ab,ac,ad
		if      constexpr (requires{T{U30,U(),U(),U(),U(),U(),U(),U(),U(),U(),U()};}) { U(0) ; }        // force compilation error to ensure no partial serialization of large classes
		else if constexpr (requires{T{U30,U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [a30,ae,af,ag,ah,ai,aj,ak,al,am] = x_ ; serdes(s_,a30,ae,af,ag,ah,ai,aj,ak,al,am) ; }
		else if constexpr (requires{T{U30,U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [a30,ae,af,ag,ah,ai,aj,ak,al   ] = x_ ; serdes(s_,a30,ae,af,ag,ah,ai,aj,ak,al   ) ; }
		else if constexpr (requires{T{U30,U(),U(),U(),U(),U(),U(),U()            };}) { auto& [a30,ae,af,ag,ah,ai,aj,ak      ] = x_ ; serdes(s_,a30,ae,af,ag,ah,ai,aj,ak      ) ; }
		else if constexpr (requires{T{U30,U(),U(),U(),U(),U(),U()                };}) { auto& [a30,ae,af,ag,ah,ai,aj         ] = x_ ; serdes(s_,a30,ae,af,ag,ah,ai,aj         ) ; }
		else if constexpr (requires{T{U30,U(),U(),U(),U(),U()                    };}) { auto& [a30,ae,af,ag,ah,ai            ] = x_ ; serdes(s_,a30,ae,af,ag,ah,ai            ) ; }
		else if constexpr (requires{T{U30,U(),U(),U(),U()                        };}) { auto& [a30,ae,af,ag,ah               ] = x_ ; serdes(s_,a30,ae,af,ag,ah               ) ; }
		else if constexpr (requires{T{U30,U(),U(),U()                            };}) { auto& [a30,ae,af,ag                  ] = x_ ; serdes(s_,a30,ae,af,ag                  ) ; }
		else if constexpr (requires{T{U30,U(),U()                                };}) { auto& [a30,ae,af                     ] = x_ ; serdes(s_,a30,ae,af                     ) ; }
		else if constexpr (requires{T{U30,U()                                    };}) { auto& [a30,ae                        ] = x_ ; serdes(s_,a30,ae                        ) ; }
		else if constexpr (requires{T{U30                                        };}) { auto& [a30                           ] = x_ ; serdes(s_,a30                           ) ; }
		else if constexpr (requires{T{U20,U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [a20,u ,v ,w ,x ,y ,z ,aa,ab,ac] = x_ ; serdes(s_,a20,u ,v ,w ,x ,y ,z ,aa,ab,ac) ; }
		else if constexpr (requires{T{U20,U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [a20,u ,v ,w ,x ,y ,z ,aa,ab   ] = x_ ; serdes(s_,a20,u ,v ,w ,x ,y ,z ,aa,ab   ) ; }
		else if constexpr (requires{T{U20,U(),U(),U(),U(),U(),U(),U()            };}) { auto& [a20,u ,v ,w ,x ,y ,z ,aa      ] = x_ ; serdes(s_,a20,u ,v ,w ,x ,y ,z ,aa      ) ; }
		else if constexpr (requires{T{U20,U(),U(),U(),U(),U(),U()                };}) { auto& [a20,u ,v ,w ,x ,y ,z          ] = x_ ; serdes(s_,a20,u ,v ,w ,x ,y ,z          ) ; }
		else if constexpr (requires{T{U20,U(),U(),U(),U(),U()                    };}) { auto& [a20,u ,v ,w ,x ,y             ] = x_ ; serdes(s_,a20,u ,v ,w ,x ,y             ) ; }
		else if constexpr (requires{T{U20,U(),U(),U(),U()                        };}) { auto& [a20,u ,v ,w ,x                ] = x_ ; serdes(s_,a20,u ,v ,w ,x                ) ; }
		else if constexpr (requires{T{U20,U(),U(),U()                            };}) { auto& [a20,u ,v ,w                   ] = x_ ; serdes(s_,a20,u ,v ,w                   ) ; }
		else if constexpr (requires{T{U20,U(),U()                                };}) { auto& [a20,u ,v                      ] = x_ ; serdes(s_,a20,u ,v                      ) ; }
		else if constexpr (requires{T{U20,U()                                    };}) { auto& [a20,u                         ] = x_ ; serdes(s_,a20,u                         ) ; }
		else if constexpr (requires{T{U20                                        };}) { auto& [a20                           ] = x_ ; serdes(s_,a20                           ) ; }
		else if constexpr (requires{T{U10,U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [a10,k ,l ,m ,n ,o ,p ,q ,r ,s ] = x_ ; serdes(s_,a10,k ,l ,m ,n ,o ,p ,q ,r ,s ) ; }
		else if constexpr (requires{T{U10,U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [a10,k ,l ,m ,n ,o ,p ,q ,r    ] = x_ ; serdes(s_,a10,k ,l ,m ,n ,o ,p ,q ,r    ) ; }
		else if constexpr (requires{T{U10,U(),U(),U(),U(),U(),U(),U()            };}) { auto& [a10,k ,l ,m ,n ,o ,p ,q       ] = x_ ; serdes(s_,a10,k ,l ,m ,n ,o ,p ,q       ) ; }
		else if constexpr (requires{T{U10,U(),U(),U(),U(),U(),U()                };}) { auto& [a10,k ,l ,m ,n ,o ,p          ] = x_ ; serdes(s_,a10,k ,l ,m ,n ,o ,p          ) ; }
		else if constexpr (requires{T{U10,U(),U(),U(),U(),U()                    };}) { auto& [a10,k ,l ,m ,n ,o             ] = x_ ; serdes(s_,a10,k ,l ,m ,n ,o             ) ; }
		else if constexpr (requires{T{U10,U(),U(),U(),U()                        };}) { auto& [a10,k ,l ,m ,n                ] = x_ ; serdes(s_,a10,k ,l ,m ,n                ) ; }
		else if constexpr (requires{T{U10,U(),U(),U()                            };}) { auto& [a10,k ,l ,m                   ] = x_ ; serdes(s_,a10,k ,l ,m                   ) ; }
		else if constexpr (requires{T{U10,U(),U()                                };}) { auto& [a10,k ,l                      ] = x_ ; serdes(s_,a10,k ,l                      ) ; }
		else if constexpr (requires{T{U10,U()                                    };}) { auto& [a10,k                         ] = x_ ; serdes(s_,a10,k                         ) ; }
		else if constexpr (requires{T{U10                                        };}) { auto& [a10                           ] = x_ ; serdes(s_,a10                           ) ; }
		else if constexpr (requires{T{    U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [    a ,b ,c ,d ,e ,f ,g ,h ,i ] = x_ ; serdes(s_    ,a ,b ,c ,d ,e ,f ,g ,h ,i ) ; }
		else if constexpr (requires{T{    U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [    a ,b ,c ,d ,e ,f ,g ,h    ] = x_ ; serdes(s_    ,a ,b ,c ,d ,e ,f ,g ,h    ) ; }
		else if constexpr (requires{T{    U(),U(),U(),U(),U(),U(),U()            };}) { auto& [    a ,b ,c ,d ,e ,f ,g       ] = x_ ; serdes(s_    ,a ,b ,c ,d ,e ,f ,g       ) ; }
		else if constexpr (requires{T{    U(),U(),U(),U(),U(),U()                };}) { auto& [    a ,b ,c ,d ,e ,f          ] = x_ ; serdes(s_    ,a ,b ,c ,d ,e ,f          ) ; }
		else if constexpr (requires{T{    U(),U(),U(),U(),U()                    };}) { auto& [    a ,b ,c ,d ,e             ] = x_ ; serdes(s_    ,a ,b ,c ,d ,e             ) ; }
		else if constexpr (requires{T{    U(),U(),U(),U()                        };}) { auto& [    a ,b ,c ,d                ] = x_ ; serdes(s_    ,a ,b ,c ,d                ) ; }
		else if constexpr (requires{T{    U(),U(),U()                            };}) { auto& [    a ,b ,c                   ] = x_ ; serdes(s_    ,a ,b ,c                   ) ; }
		else if constexpr (requires{T{    U(),U()                                };}) { auto& [    a ,b                      ] = x_ ; serdes(s_    ,a ,b                      ) ; }
		else if constexpr (requires{T{    U()                                    };}) { auto& [    a                         ] = x_ ; serdes(s_    ,a                         ) ; }
		#undef a30
		#undef a20
		#undef a10
		#undef U30
		#undef U20
		#undef U10
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
		if (is.size()<sz) throw 0 ;
		s.resize(sz) ;
		is.copy         ( s.data() , sz ) ;
		is.remove_prefix(            sz ) ;
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

template<class T,class U> struct Serdeser<::pair<T,U>> {
	template<IsOStream S> static void s_serdes( S& s , ::pair<T,U> const& p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
	template<IsIStream S> static void s_serdes( S& s , ::pair<T,U>      & p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
} ;
