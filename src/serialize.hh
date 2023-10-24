// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

template<class T> struct Serdeser ;

template<class S> concept IsStream    =                                                                    ::is_same_v<S,ostream>      || ::is_same_v<S,istream>          ;
//
template<class T> concept HasSerdes   =                  requires( T x , ::istream& is , ::ostream& os ) { x.serdes(os)                ;  x.serdes(is)                ; } ;
template<class T> concept HasSerdeser = !HasSerdes<T> && requires( T x , ::istream& is , ::ostream& os ) { Serdeser<T>::s_serdes(os,x) ;  Serdeser<T>::s_serdes(is,x) ; } ;

template<class T> concept Serializable = HasSerdes<T> || HasSerdeser<T> ;

// serdes method should be const when serializing but is not because C++ does not let constness be template arg dependent
/**/                                                               static inline void serdes( ::ostream&                                                    ) {                                       }
/**/                                                               static inline void serdes( ::istream&                                                    ) {                                       }
template<HasSerdes     T                                         > static inline void serdes( ::ostream& os , T  const& x                                   ) { const_cast<T&>(x).serdes(os) ;        }
template<HasSerdes     T                                         > static inline void serdes( ::istream& is , T       & x                                   ) {                x .serdes(is) ;        }
template<HasSerdeser   T                                         > static inline void serdes( ::ostream& os , T  const& x                                   ) { Serdeser<T>::s_serdes(os,x)  ;        }
template<HasSerdeser   T                                         > static inline void serdes( ::istream& is , T       & x                                   ) { Serdeser<T>::s_serdes(is,x)  ;        }
template< Serializable T1 , Serializable T2 , Serializable... Ts > static inline void serdes( ::ostream& os , T1 const& x1 , T2 const& x2 , Ts const&... xs ) { serdes(os,x1) ; serdes(os,x2,xs...) ; }
template< Serializable T1 , Serializable T2 , Serializable... Ts > static inline void serdes( ::istream& is , T1      & x1 , T2      & x2 , Ts      &... xs ) { serdes(is,x1) ; serdes(is,x2,xs...) ; }
//
template<Serializable T> static inline void     serialize  ( ::ostream& os , T const& x ) {                     serdes(os ,x  ) ; os.flush()       ; }
template<Serializable T> static inline ::string serialize  (                 T const& x ) { OStringStream res ; serdes(res,x  ) ; return res.str() ; }
template<Serializable T> static inline void     deserialize( ::istream& is , T      & x ) {                     serdes(is ,x  ) ;                    }
template<Serializable T> static inline T        deserialize( ::istream& is              ) { T             res ; serdes(is ,res) ; return res       ; }
//
template<Serializable T> static inline void serialize  ( ::ostream&&     os , T const& x ) {        serialize  <T>(os              ,x) ; }
template<Serializable T> static inline void deserialize( ::istream&&     is , T      & x ) {        deserialize<T>(is              ,x) ; }
template<Serializable T> static inline T    deserialize( ::istream&&     is              ) { return deserialize<T>(is                ) ; }
template<Serializable T> static inline T    deserialize( ::string const& s               ) { return deserialize<T>(IStringStream(s)  ) ; }

// make objects hashable as soon as they define serdes(::ostream) &&  serdes(::istream)
// as soon as a class T is serializable, you can simply use ::set<T>, ::uset<T>, ::map<T,...> or ::umap<T,...>
// /!\ : not ideal in terms of performances, but easy to use.
// suppress calls to FAIL when necessary
template<HasSerdes T> bool              operator== ( T const& a , T const& b ) { FAIL();return serialize(a)== serialize(b) ; } // cannot define this for Serializable as it creates conflicts
template<HasSerdes T> ::strong_ordering operator<=>( T const& a , T const& b ) { FAIL();return serialize(a)<=>serialize(b) ; } // .
namespace std {
	template<HasSerdes T> struct hash<T> { size_t operator()(T const& x) const { FAIL();return hash<::string>()(serialize(x)) ; } } ; // .
}

template<class T> requires( ::is_aggregate_v<T> && !::is_trivially_copyable_v<T> ) struct Serdeser<T> {
	struct U { template<class X> operator X() const ; } ;                                               // a universal class that can be cast to anything
	static void s_serdes( ::ostream& os , T const& x ) {
		if      constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()};}) { U(0) ; } // force compilation error to ensure we do not partially serialize a large class
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto const& [a,b,c,d,e,f,g,h,i,j] = x ; serdes(os,a,b,c,d,e,f,g,h,i,j) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto const& [a,b,c,d,e,f,g,h,i  ] = x ; serdes(os,a,b,c,d,e,f,g,h,i  ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U()            };}) { auto const& [a,b,c,d,e,f,g,h    ] = x ; serdes(os,a,b,c,d,e,f,g,h    ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U()                };}) { auto const& [a,b,c,d,e,f,g      ] = x ; serdes(os,a,b,c,d,e,f,g      ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U()                    };}) { auto const& [a,b,c,d,e,f        ] = x ; serdes(os,a,b,c,d,e,f        ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U()                        };}) { auto const& [a,b,c,d,e          ] = x ; serdes(os,a,b,c,d,e          ) ; }
		else if constexpr (requires{T{U(),U(),U(),U()                            };}) { auto const& [a,b,c,d            ] = x ; serdes(os,a,b,c,d            ) ; }
		else if constexpr (requires{T{U(),U(),U()                                };}) { auto const& [a,b,c              ] = x ; serdes(os,a,b,c              ) ; }
		else if constexpr (requires{T{U(),U()                                    };}) { auto const& [a,b                ] = x ; serdes(os,a,b                ) ; }
		else if constexpr (requires{T{U()                                        };}) { auto const& [a                  ] = x ; serdes(os,a                  ) ; }
	}
	static void s_serdes( ::istream& is , T& x ) {
		if      constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U(),U()};}) { U(0) ; } // force compilation error to ensure we do not partially serialize a large class
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U(),U()    };}) { auto& [a,b,c,d,e,f,g,h,i,j] = x ; serdes(is,a,b,c,d,e,f,g,h,i,j) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U(),U()        };}) { auto& [a,b,c,d,e,f,g,h,i  ] = x ; serdes(is,a,b,c,d,e,f,g,h,i  ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U(),U()            };}) { auto& [a,b,c,d,e,f,g,h    ] = x ; serdes(is,a,b,c,d,e,f,g,h    ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U(),U()                };}) { auto& [a,b,c,d,e,f,g      ] = x ; serdes(is,a,b,c,d,e,f,g      ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U(),U()                    };}) { auto& [a,b,c,d,e,f        ] = x ; serdes(is,a,b,c,d,e,f        ) ; }
		else if constexpr (requires{T{U(),U(),U(),U(),U()                        };}) { auto& [a,b,c,d,e          ] = x ; serdes(is,a,b,c,d,e          ) ; }
		else if constexpr (requires{T{U(),U(),U(),U()                            };}) { auto& [a,b,c,d            ] = x ; serdes(is,a,b,c,d            ) ; }
		else if constexpr (requires{T{U(),U(),U()                                };}) { auto& [a,b,c              ] = x ; serdes(is,a,b,c              ) ; }
		else if constexpr (requires{T{U(),U()                                    };}) { auto& [a,b                ] = x ; serdes(is,a,b                ) ; }
		else if constexpr (requires{T{U()                                        };}) { auto& [a                  ] = x ; serdes(is,a                  ) ; }
	}
} ;

template<class T> requires(::is_trivially_copyable_v<T>) struct Serdeser<T> {
	// compiler should be able to avoid copy when using bit_cast, even with an intermediate buf
	static void s_serdes( ::ostream& os , T const& x ) {                       os.write( bit_cast<::array<char,sizeof(x)>>(x).data() , sizeof(x) ) ;                        }
	static void s_serdes( ::istream& is , T      & x ) { char buf[sizeof(x)] ; is.read ( buf                                         , sizeof(x) ) ; x = bit_cast<T>(buf) ; }
} ;

template<> struct Serdeser<::string> {
	static void s_serdes( ::ostream& os , ::string const& s ) { size_t sz=s.size() ; serdes(os,sz) ;                os.write( s.data() , sz ) ; }
	static void s_serdes( ::istream& is , ::string      & s ) { size_t sz          ; serdes(is,sz) ; s.resize(sz) ; is.read ( s.data() , sz ) ; }
} ;

template<class T,size_t N> struct Serdeser<T[N]> {
	static void s_serdes( ::ostream& os , T const a[N] ) { for( size_t i=0 ; i<N ; i++ ) serdes(os,a[i]) ; }
	static void s_serdes( ::istream& is , T       a[N] ) { for( size_t i=0 ; i<N ; i++ ) serdes(is,a[i]) ; }
} ;

template<class T,size_t N> struct Serdeser<::array<T,N>> {
	static void s_serdes( ::ostream& os , ::array<T,N> const& a ) { for( T const& x : a ) serdes(os,x) ; }
	static void s_serdes( ::istream& is , ::array<T,N>      & a ) { for( T      & x : a ) serdes(is,x) ; }
} ;

template<class T> struct Serdeser<::vector<T>> {
	static void s_serdes( ::ostream& os , ::vector<T> const& v ) {             serdes(os,v.size()) ;                for( T const& x : v ) serdes(os,x) ; }
	static void s_serdes( ::istream& is , ::vector<T>      & v ) { size_t sz ; serdes(is,sz      ) ; v.resize(sz) ; for( T      & x : v ) serdes(is,x) ; }
} ;

template<class T> struct Serdeser<::set<T>> {
	static void s_serdes( ::ostream& os , ::set<T> const& s ) {             serdes(os,s.size()) ;             for( T const& x : s          )         serdes(os,x) ;                 }
	static void s_serdes( ::istream& is , ::set<T>      & s ) { size_t sz ; serdes(is,sz      ) ; s.clear() ; for( size_t i=0 ; i<sz ; i++ ) { T x ; serdes(is,x) ; s.insert(x) ; } }
} ;

template<class T> struct Serdeser<::uset<T>> {
	static void s_serdes( ::ostream& os , ::uset<T> const& s ) {             serdes(os,s.size()) ;             for( T const& x : s          )         serdes(os,x) ;                 }
	static void s_serdes( ::istream& is , ::uset<T>      & s ) { size_t sz ; serdes(is,sz      ) ; s.clear() ; for( size_t i=0 ; i<sz ; i++ ) { T x ; serdes(is,x) ; s.insert(x) ; } }
} ;

template<class K,class V> struct Serdeser<::map<K,V>> {
	static void s_serdes( ::ostream& os , ::map<K,V> const& m ) {             serdes(os,m.size()) ;             for( auto const& p : m       )                   serdes(os,p) ;                 }
	static void s_serdes( ::istream& is , ::map<K,V>      & m ) { size_t sz ; serdes(is,sz      ) ; m.clear() ; for( size_t i=0 ; i<sz ; i++ ) { ::pair<K,V> p ; serdes(is,p) ; m.insert(p) ; } }
} ;

template<class K,class V> struct Serdeser<::umap<K,V>> {
	static void s_serdes( ::ostream& os , ::umap<K,V> const& m ) {             serdes(os,m.size()) ;             for( auto const& p : m       )                   serdes(os,p) ;                 }
	static void s_serdes( ::istream& is , ::umap<K,V>      & m ) { size_t sz ; serdes(is,sz      ) ; m.clear() ; for( size_t i=0 ; i<sz ; i++ ) { ::pair<K,V> p ; serdes(is,p) ; m.insert(p) ; } }
} ;

template<class T,class U> struct Serdeser<::pair<T,U>> {
	static void s_serdes( ::ostream& s , ::pair<T,U> const& p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
	static void s_serdes( ::istream& s , ::pair<T,U>      & p ) { serdes(s,p.first ) ; serdes(s,p.second) ; }
} ;

// need to go through a helper to manage recursion
template<size_t I,class... T> struct SerdeserTupleHelper {           // recursive case
	static void s_serdes( ::ostream& s , ::tuple<T...> const& t ) { serdes(s,::get<sizeof...(T)-I>(t)) ; SerdeserTupleHelper<I-1,T...>::s_serdes(s,t) ; }
	static void s_serdes( ::istream& s , ::tuple<T...>      & t ) { serdes(s,::get<sizeof...(T)-I>(t)) ; SerdeserTupleHelper<I-1,T...>::s_serdes(s,t) ; }
} ;
template<class... T> struct SerdeserTupleHelper<0,T...> {            // terminal case
	static void s_serdes( ::ostream& , ::tuple<T...> const& ) {}
	static void s_serdes( ::istream& , ::tuple<T...>      & ) {}
} ;
template<class... T> struct Serdeser<::tuple<T...>> {
	static void s_serdes( ::ostream& s , ::tuple<T...> const& t ) { SerdeserTupleHelper<sizeof...(T),T...>::s_serdes(s,t) ; }
	static void s_serdes( ::istream& s , ::tuple<T...>      & t ) { SerdeserTupleHelper<sizeof...(T),T...>::s_serdes(s,t) ; }
} ;

//
// MsgBuf
//

struct MsgBuf {
	using Len = size_t ;
	// statics
	static Len s_sz(const char* str) {
		Len len = 0 ; ::memcpy( &len , str , sizeof(Len) ) ;
		return len ;
	}
protected :
	void _clear() {                                                            // functionally equivalent to : *this = InEntry() ;
		_buf.resize(sizeof(Len)) ;                                             // keep buf allocation
		_len       = 0     ;
		_data_pass = false ;
	}
	// data
	Len      _len       = 0     ;      // data sent/received so far, reading : may also apply to len accumulated in buf
	::string _buf       ;              // reading : sized after expected size, but actuall filled up only with len char's    // writing : contains len+data to be sent
	bool     _data_pass = false ;      // reading : if true <=> buf contains partial data, else it contains partial data len // writing : if true <=> buf contains data
} ;

struct IMsgBuf : MsgBuf {
	IMsgBuf() { _buf.resize(sizeof(Len)) ; }                                   // prepare to receive len
	// statics
	template<class T> static T s_receive(const char* str) {
		Len len = s_sz(str) ;
		//     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		return deserialize<T>(IStringStream(::string( str+sizeof(Len) , len ))) ; // XXX : avoid copy (use string_view in C++26)
		//     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
	// services
	template<class T> T receive(Fd fd) {
		T res ;
		while (!receive_step(fd,res)) ;
		return res ;
	}
	template<class T> bool/*complete*/ receive_step( Fd fd , T& res ) {
		ssize_t cnt = ::read( fd , &_buf[_len] , _buf.size()-_len ) ;
		if (cnt<=0) throw to_string("cannot receive over fd ",fd) ;
		_len += cnt ;
		if (_len<_buf.size()) return false/*complete*/ ;                       // _buf is still partial
		if (_data_pass) {
			//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res = deserialize<T>(IStringStream(::move(_buf))) ;
			//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			_clear() ;
			return true/*complete*/ ;
		} else {
			SWEAR( _buf.size()==sizeof(Len) , _buf.size() ) ;
			Len len = s_sz(_buf.data()) ;
			// we now expect the data
			_buf.resize(len) ;
			_data_pass = true ;
			_len       = 0    ;
			return false/*complete*/ ;
		}
	}
} ;

struct OMsgBuf : MsgBuf {
	// statics
	template<class T> static ::string s_send(T const& x) {
		::string res = serialize(::pair<decltype(_len),T>(0,x)) ;              // directly serialize in res to avoid copy : serialize a pair with length+data
		Len      len = res.size()-sizeof(Len) ;
		::memcpy( res.data() , &len , sizeof(Len) ) ;                          // overwrite len
		return res ;
	}
	// services
	template<class T> void send( Fd fd , T const& x ) {
		if (send_step(fd,x)) return ;
		while (!send_step(fd)) ;
	}
	template<class T> bool/*complete*/ send_step( Fd fd , T const& x ) {
		SWEAR(!_data_pass) ;
		_buf       = s_send(x) ;
		_data_pass = true      ;
		return send_step(fd)   ;
	}
	bool/*complete*/ send_step(Fd fd) {
		SWEAR(_data_pass) ;
		ssize_t cnt = ::write( fd , &_buf[_len] , _buf.size()-_len ) ;
		if (cnt<=0) throw to_string("cannot send over ",fd) ;
		_len += cnt ;
		if (_len<_buf.size()) {            return false/*complete*/ ; }        // _buf is still partial
		else                  { _clear() ; return true /*complete*/ ; }
	}
} ;
