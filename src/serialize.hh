// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

template<class T> struct Serdeser ;

template<class S> concept IsStream = ::is_base_of_v<::ostream,S> || ::is_base_of_v<::istream,S> ;
//
template<class T> concept HasSerdes   =                  requires( T x , ::istream& is , ::ostream& os ) { x.serdes(os)                ;  x.serdes(is)                ; } ;
template<class T> concept HasSerdeser = !HasSerdes<T> && requires( T x , ::istream& is , ::ostream& os ) { Serdeser<T>::s_serdes(os,x) ;  Serdeser<T>::s_serdes(is,x) ; } ;

template<class T> concept Serializable = HasSerdes<T> || HasSerdeser<T> ;

// serdes method should be const when serializing but is not because C++ does not let constness be template arg dependent
inline                                                             void serdes( ::ostream&                                                    ) {                                       }
inline                                                             void serdes( ::istream&                                                    ) {                                       }
template<HasSerdes     T                                         > void serdes( ::ostream& os , T  const& x                                   ) { const_cast<T&>(x).serdes(os) ;        }
template<HasSerdes     T                                         > void serdes( ::istream& is , T       & x                                   ) {                x .serdes(is) ;        }
template<HasSerdeser   T                                         > void serdes( ::ostream& os , T  const& x                                   ) { Serdeser<T>::s_serdes(os,x)  ;        }
template<HasSerdeser   T                                         > void serdes( ::istream& is , T       & x                                   ) { Serdeser<T>::s_serdes(is,x)  ;        }
template< Serializable T1 , Serializable T2 , Serializable... Ts > void serdes( ::ostream& os , T1 const& x1 , T2 const& x2 , Ts const&... xs ) { serdes(os,x1) ; serdes(os,x2,xs...) ; }
template< Serializable T1 , Serializable T2 , Serializable... Ts > void serdes( ::istream& is , T1      & x1 , T2      & x2 , Ts      &... xs ) { serdes(is,x1) ; serdes(is,x2,xs...) ; }
//
template<Serializable T> void     serialize  ( ::ostream& os , T const& x ) {                     serdes(os ,x  ) ; os.flush()               ; }
template<Serializable T> ::string serialize  (                 T const& x ) { OStringStream res ; serdes(res,x  ) ; return ::move(res).str() ; }
template<Serializable T> void     deserialize( ::istream& is , T      & x ) {                     serdes(is ,x  ) ;                            }
template<Serializable T> T        deserialize( ::istream& is              ) { T             res ; serdes(is ,res) ; return res               ; }
//
template<Serializable T> void serialize  ( ::ostream&&     os , T const& x ) {        serialize  <T>(os              ,x) ; }
template<Serializable T> void deserialize( ::istream&&     is , T      & x ) {        deserialize<T>(is              ,x) ; }
template<Serializable T> T    deserialize( ::istream&&     is              ) { return deserialize<T>(is                ) ; }
template<Serializable T> T    deserialize( ::string const& s               ) { return deserialize<T>(IStringStream(s)  ) ; }

// make objects hashable as soon as they define serdes(::ostream) &&  serdes(::istream)
// as soon as a class T is serializable, you can simply use ::set<T>, ::uset<T>, ::map<T,...> or ::umap<T,...>
// /!\ : not ideal in terms of performances, but easy to use.
// suppress calls to FAIL when necessary
template<HasSerdes T> bool              operator== ( T const& a , T const& b ) { FAIL();return serialize(a)== serialize(b) ; }        // cannot define this for Serializable as it creates conflicts
template<HasSerdes T> ::strong_ordering operator<=>( T const& a , T const& b ) { FAIL();return serialize(a)<=>serialize(b) ; }        // .
namespace std {
	template<HasSerdes T> struct hash<T> { size_t operator()(T const& x) const { FAIL();return hash<::string>()(serialize(x)) ; } } ; // .
}

template<class T> requires( ::is_aggregate_v<T> && !::is_trivially_copyable_v<T> ) struct Serdeser<T> {
	struct U { template<class X> operator X() const ; } ;                                               // a universal class that can be cast to anything
	static void s_serdes( ::ostream& os , T const& x ) {
		if      constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()};}) { U(0) ; }    // force compilation error to ensure we do not partially serialize a large class
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto const& [a,b,c,d,e,f,g,h,i,j,k] = x ; serdes(os,a,b,c,d,e,f,g,h,i,j,k) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto const& [a,b,c,d,e,f,g,h,i,j  ] = x ; serdes(os,a,b,c,d,e,f,g,h,i,j  ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U()            };}) { auto const& [a,b,c,d,e,f,g,h,i    ] = x ; serdes(os,a,b,c,d,e,f,g,h,i    ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U()                };}) { auto const& [a,b,c,d,e,f,g,h      ] = x ; serdes(os,a,b,c,d,e,f,g,h      ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U()                    };}) { auto const& [a,b,c,d,e,f,g        ] = x ; serdes(os,a,b,c,d,e,f,g        ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U()                        };}) { auto const& [a,b,c,d,e,f          ] = x ; serdes(os,a,b,c,d,e,f          ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U()                            };}) { auto const& [a,b,c,d,e            ] = x ; serdes(os,a,b,c,d,e            ) ; }
		else if constexpr (requires{T{U(),U(),U(),U()                                };}) { auto const& [a,b,c,d              ] = x ; serdes(os,a,b,c,d              ) ; }
		else if constexpr (requires{T{U(),U(),U()                                    };}) { auto const& [a,b,c                ] = x ; serdes(os,a,b,c                ) ; }
		else if constexpr (requires{T{U(),U()                                        };}) { auto const& [a,b                  ] = x ; serdes(os,a,b                  ) ; }
		else if constexpr (requires{T{U()                                            };}) { auto const& [a                    ] = x ; serdes(os,a                    ) ; }
	}
	static void s_serdes( ::istream& is , T& x ) {
		if      constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()};}) { U(0) ; }    // force compilation error to ensure we do not partially serialize a large class
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [a,b,c,d,e,f,g,h,i,j,k] = x ; serdes(is,a,b,c,d,e,f,g,h,i,j,k) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [a,b,c,d,e,f,g,h,i,j  ] = x ; serdes(is,a,b,c,d,e,f,g,h,i,j  ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U()            };}) { auto& [a,b,c,d,e,f,g,h,i    ] = x ; serdes(is,a,b,c,d,e,f,g,h,i    ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U()                };}) { auto& [a,b,c,d,e,f,g,h      ] = x ; serdes(is,a,b,c,d,e,f,g,h      ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U()                    };}) { auto& [a,b,c,d,e,f,g        ] = x ; serdes(is,a,b,c,d,e,f,g        ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U()                        };}) { auto& [a,b,c,d,e,f          ] = x ; serdes(is,a,b,c,d,e,f          ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U()                            };}) { auto& [a,b,c,d,e            ] = x ; serdes(is,a,b,c,d,e            ) ; }
		else if constexpr (requires{T{U(),U(),U(),U()                                };}) { auto& [a,b,c,d              ] = x ; serdes(is,a,b,c,d              ) ; }
		else if constexpr (requires{T{U(),U(),U()                                    };}) { auto& [a,b,c                ] = x ; serdes(is,a,b,c                ) ; }
		else if constexpr (requires{T{U(),U()                                        };}) { auto& [a,b                  ] = x ; serdes(is,a,b                  ) ; }
		else if constexpr (requires{T{U()                                            };}) { auto& [a                    ] = x ; serdes(is,a                    ) ; }
	}
} ;

template<class T> requires(::is_trivially_copyable_v<T>) struct Serdeser<T> {
	// compiler should be able to avoid copy when using bit_cast, even with an intermediate buf
	static void s_serdes( ::ostream& os , T const& x ) {                       os.write( bit_cast<::array<char,sizeof(x)>>(x).data() , sizeof(x) ) ;                        }
	static void s_serdes( ::istream& is , T      & x ) { char buf[sizeof(x)] ; is.read ( buf                                         , sizeof(x) ) ; x = bit_cast<T>(buf) ; }
} ;

// /!\ dont use size_t in serialized stream to make serialization interoperable between 32 bits and 64 bits
template<class T> static uint32_t _sz32(T const& v) {
	uint32_t res = v.size() ; SWEAR(res==v.size()) ;  // uint32_t is already very comfortable, no need to use 64 bits for lengths
	return res ;
}

template<> struct Serdeser<::string> {
	static void s_serdes( ::ostream& os , ::string const& s ) { uint32_t sz=_sz32(s) ; serdes(os,sz) ;                os.write(s.data(),sz) ; }
	static void s_serdes( ::istream& is , ::string      & s ) { uint32_t sz          ; serdes(is,sz) ; s.resize(sz) ; is.read (s.data(),sz) ; }
} ;

template<class T,size_t N> struct Serdeser<T[N]> {
	static void s_serdes( ::ostream& os , T const a[N] ) { for( size_t i : iota(N) ) serdes(os,a[i]) ; }
	static void s_serdes( ::istream& is , T       a[N] ) { for( size_t i : iota(N) ) serdes(is,a[i]) ; }
} ;

template<class T,size_t N> struct Serdeser<::array<T,N>> {
	static void s_serdes( ::ostream& os , ::array<T,N> const& a ) { for( T const& x : a ) serdes(os,x) ; }
	static void s_serdes( ::istream& is , ::array<T,N>      & a ) { for( T      & x : a ) serdes(is,x) ; }
} ;

template<class T> struct Serdeser<::vector<T>> {
	static void s_serdes( ::ostream& os , ::vector<T> const& v ) { serdes(os,_sz32(v)) ;                 for( T const& x : v ) serdes(os,x) ; }
	static void s_serdes( ::istream& is , ::vector<T>      & v ) { v.resize(deserialize<uint32_t>(is)) ; for( T      & x : v ) serdes(is,x) ; }
} ;

template<class T> struct Serdeser<::set<T>> {
	static void s_serdes( ::ostream& os , ::set<T> const& s ) { serdes(os,_sz32(s)) ; for( T const& x : s                                              )          serdes        (os,x)  ; }
	static void s_serdes( ::istream& is , ::set<T>      & s ) { s.clear() ;           for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) s.insert(deserialize<T>(is  )) ; }
} ;

template<class T> struct Serdeser<::uset<T>> {
	static void s_serdes( ::ostream& os , ::uset<T> const& s ) { serdes(os,_sz32(s)) ; for( T const& x : s                                              )          serdes        (os,x)  ; }
	static void s_serdes( ::istream& is , ::uset<T>      & s ) { s.clear() ;           for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) s.insert(deserialize<T>(is  )) ; }
} ;

template<class K,class V> struct Serdeser<::map<K,V>> {
	static void s_serdes( ::ostream& os , ::map<K,V> const& m ) { serdes(os,_sz32(m)) ; for( ::pair<K const,V> const& p : m                              )          serdes                  (os,p)  ; }
	static void s_serdes( ::istream& is , ::map<K,V>      & m ) { m.clear() ;           for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) m.insert(deserialize<::pair<K,V>>(is  )) ; }
} ;

template<class K,class V> struct Serdeser<::umap<K,V>> {
	static void s_serdes( ::ostream& os , ::umap<K,V> const& m ) { serdes(os,_sz32(m)) ; for( ::pair<K const,V> const& p : m                              )          serdes                  (os,p)  ; }
	static void s_serdes( ::istream& is , ::umap<K,V>      & m ) { m.clear() ;           for( [[maybe_unused]] size_t i : iota(deserialize<uint32_t>(is)) ) m.insert(deserialize<::pair<K,V>>(is  )) ; }
} ;

template<class T,class U> struct Serdeser<::pair<T,U>> {
	static void s_serdes( ::ostream& s , ::pair<T,U> const& p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
	static void s_serdes( ::istream& s , ::pair<T,U>      & p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
} ;
