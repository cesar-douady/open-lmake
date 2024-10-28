// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <netinet/ip.h> // in_addr_t, in_port_t
#include <signal.h>     // SIG*, kill
#include <sys/file.h>   // AT_*, F_*, FD_*, LOCK_*, O_*, fcntl, flock, openat

#include <cstring> // memcpy, strchr, strerror, strlen, strncmp, strnlen, strsignal

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv> // from_chars_result
#include <chrono>
#include <concepts>
#include <fstream>
#include <functional>
#include <iomanip>
#include <ios>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sys_config.h"
#include "non_portable.hh"

using namespace std ; // use std at top level so one write ::stuff instead of std::stuff
using std::getline ;  // special case getline which also has a C version that hides std::getline

//
// meta programming
//

template<class T,class... Ts> struct LargestHelper ;
template<class T1,class T2,class... Ts> struct LargestHelper<T1,T2,Ts...> {
	private : using _rest_type = typename LargestHelper<T2,Ts...>::type ;
	public  : using type       = ::conditional_t<sizeof(T1)>=sizeof(_rest_type),T1,_rest_type> ;
} ;
template<class T1> struct LargestHelper<T1> { using type = T1 ; } ;
template<class T,class... Ts> using Largest = typename LargestHelper<T,Ts...>::type ;

template<bool C,class T> using Const = ::conditional_t<C,const T,T> ;
// place holder when you need a type which is semantically void but syntactically needed
struct Void {} ;
template<class T,class D=Void> using NoVoid = ::conditional_t<::is_void_v<T>,D,T> ;
template<class T,T... X> requires(false) struct Err {} ;                            // for debug purpose, to be used as a tracing point through the diagnostic message

template<size_t NB> using Uint = ::conditional_t< NB<=8 , uint8_t , ::conditional_t< NB<=16 , uint16_t , ::conditional_t< NB<=32 , uint32_t , ::conditional_t< NB<=64 , uint64_t , void > > > > ;

template<class T,class... Ts> struct IsOneOfHelper ;
template<class T,class... Ts> concept IsOneOf = IsOneOfHelper<T,Ts...>::yes ;
template<class T                     > struct IsOneOfHelper<T         > { static constexpr bool yes = false                                 ; } ;
template<class T,class T0,class... Ts> struct IsOneOfHelper<T,T0,Ts...> { static constexpr bool yes = ::is_same_v<T,T0> || IsOneOf<T,Ts...> ; } ;

template<class T> concept IsChar = ::is_trivial_v<T> && ::is_standard_layout_v<T> ; // necessary property to make a ::basic_string
template<class T> using AsChar = ::conditional_t<IsChar<T>,T,char> ;                // provide default value if not a Char so as to make ::basic_string before knowing if it is possible

template<class D,class B> concept IsA       = ::is_same_v<remove_const_t<B>,remove_const_t<D>> || ::is_base_of_v<remove_const_t<B>,remove_const_t<D>> ;
template<class T        > concept IsNotVoid = !::is_void_v<T>                                                                                         ;

template<class T> constexpr T        copy    (T const& x) { return   x ; }
template<class T> constexpr T      & ref     (T     && x) { return *&x ; }
template<class T> constexpr T const& constify(T const& x) { return   x ; }

template<class T> static constexpr size_t NBits = sizeof(T)*8 ;

//
// std lib name simplification
//

// array
template<                size_t N> using array_s  = ::array        <       string,N> ;
template<class K,class V,size_t N> using amap     = ::array<pair   <K     ,V>    ,N> ;
template<        class V,size_t N> using amap_s   = ::amap         <string,V     ,N> ;
template<                size_t N> using amap_ss  = ::amap_s       <       string,N> ;

// pair
template<class V                 > using pair_s   = ::pair         <string,V       > ;
/**/                               using pair_ss  = ::pair_s       <       string  > ;

// map
template<        class V         > using map_s    = ::map          <string,V       > ;
/**/                               using map_ss   = ::map_s        <       string  > ;

// set
/**/                               using set_s    = ::set          <       string  > ;

// umap
template<class K,class  V         > using umap    = ::unordered_map<K     ,V       > ;
template<        class  V         > using umap_s  = ::umap         <string,V       > ;
/**/                               using umap_ss  = ::umap_s       <       string  > ;

// uset
template<class K                 > using uset     = ::unordered_set<K              > ;
/**/                               using uset_s   = ::uset         <string         > ;

// vector
/**/                               using vector_s = ::vector       <       string  > ;
template<class K,class V         > using vmap     = ::vector<pair  <K     ,V>      > ;
template<        class V         > using vmap_s   = ::vmap         <string,V       > ;
/**/                               using vmap_ss  = ::vmap_s       <       string  > ;

template<class T,size_t N> constexpr bool operator+(::array <T,N> const&  ) { return  N                   ; }
template<class T,size_t N> constexpr bool operator!(::array <T,N> const& a) { return !+a                  ; }
template<class T,class  U> constexpr bool operator+(::pair  <T,U> const& p) { return  +p.first||+p.second ; }
template<class T,class  U> constexpr bool operator!(::pair  <T,U> const& p) { return !+p                  ; }
template<class K,class  V> constexpr bool operator+(::map   <K,V> const& m) { return !m.empty()           ; }
template<class K,class  V> constexpr bool operator!(::map   <K,V> const& m) { return !+m                  ; }
template<class K,class  V> constexpr bool operator+(::umap  <K,V> const& m) { return !m.empty()           ; }
template<class K,class  V> constexpr bool operator!(::umap  <K,V> const& m) { return !+m                  ; }
template<class K         > constexpr bool operator+(::set   <K  > const& s) { return !s.empty()           ; }
template<class K         > constexpr bool operator!(::set   <K  > const& s) { return !+s                  ; }
template<class K         > constexpr bool operator+(::uset  <K  > const& s) { return !s.empty()           ; }
template<class K         > constexpr bool operator!(::uset  <K  > const& s) { return !+s                  ; }
template<class T         > constexpr bool operator+(::vector<T  > const& v) { return !v.empty()           ; }
template<class T         > constexpr bool operator!(::vector<T  > const& v) { return !+v                  ; }

#define VT(T) typename T::value_type

// easy transformation of a container into another
template<class K,        class V> ::set   <K                                                   > mk_set   (V const& v) { return { v.cbegin() , v.cend() } ; }
template<class K,        class V> ::uset  <K                                                   > mk_uset  (V const& v) { return { v.cbegin() , v.cend() } ; }
template<        class T,class V> ::vector<                                  T                 > mk_vector(V const& v) { return ::vector<T>( v.cbegin() , v.cend() ) ; }
template<class K,class T,class V> ::map   <K                                ,T                 > mk_map   (V const& v) { return { v.cbegin() , v.cend() } ; }
template<class K,class T,class V> ::umap  <K                                ,T                 > mk_umap  (V const& v) { return { v.cbegin() , v.cend() } ; }
template<class K,class T,class V> ::vmap  <K                                ,T                 > mk_vmap  (V const& v) { return { v.cbegin() , v.cend() } ; }
// with implicit key type
template<        class T,class V> ::map   <remove_const_t<VT(V)::first_type>,T                 > mk_map   (V const& v) { return { v.cbegin() , v.cend() } ; }
template<        class T,class V> ::umap  <remove_const_t<VT(V)::first_type>,T                 > mk_umap  (V const& v) { return { v.cbegin() , v.cend() } ; }
template<        class T,class V> ::vmap  <remove_const_t<VT(V)::first_type>,T                 > mk_vmap  (V const& v) { return { v.cbegin() , v.cend() } ; }
// with implicit item type
template<                class V> ::set   <remove_const_t<VT(V)            >                   > mk_set   (V const& v) { return { v.cbegin() , v.cend() } ; }
template<                class V> ::uset  <remove_const_t<VT(V)            >                   > mk_uset  (V const& v) { return { v.cbegin() , v.cend() } ; }
template<                class V> ::vector<                                  VT(V)             > mk_vector(V const& v) { return { v.cbegin() , v.cend() } ; }
template<                class V> ::map   <remove_const_t<VT(V)::first_type>,VT(V)::second_type> mk_map   (V const& v) { return { v.cbegin() , v.cend() } ; }
template<                class V> ::umap  <remove_const_t<VT(V)::first_type>,VT(V)::second_type> mk_umap  (V const& v) { return { v.cbegin() , v.cend() } ; }
template<                class V> ::vmap  <remove_const_t<VT(V)::first_type>,VT(V)::second_type> mk_vmap  (V const& v) { return { v.cbegin() , v.cend() } ; }

// keys & vals
template<class K,class M> ::set   <K> const mk_key_set   (M const& m) { ::set   <K> res ;                         for( auto const& [k,v] : m) res.insert   (k) ; return res ; }
template<class K,class M> ::uset  <K> const mk_key_uset  (M const& m) { ::uset  <K> res ;                         for( auto const& [k,v] : m) res.insert   (k) ; return res ; }
template<class K,class M> ::vector<K> const mk_key_vector(M const& m) { ::vector<K> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m) res.push_back(k) ; return res ; }
template<class T,class M> ::set   <T>       mk_val_set   (M const& m) { ::set   <T> res ;                         for( auto const& [k,v] : m) res.insert   (v) ; return res ; }
template<class T,class M> ::uset  <T>       mk_val_uset  (M const& m) { ::uset  <T> res ;                         for( auto const& [k,v] : m) res.insert   (v) ; return res ; }
template<class T,class M> ::vector<T>       mk_val_vector(M const& m) { ::vector<T> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m) res.push_back(v) ; return res ; }
// with implicit item type
template<class M> ::set   <remove_const_t<VT(M)::first_type >> const mk_key_set   (M const& m) { return mk_key_set   <remove_const_t<VT(M)::first_type >>(m) ; }
template<class M> ::uset  <remove_const_t<VT(M)::first_type >> const mk_key_uset  (M const& m) { return mk_key_uset  <remove_const_t<VT(M)::first_type >>(m) ; }
template<class M> ::vector<remove_const_t<VT(M)::first_type >> const mk_key_vector(M const& m) { return mk_key_vector<remove_const_t<VT(M)::first_type >>(m) ; }
template<class M> ::set   <               VT(M)::second_type >       mk_val_set   (M const& m) { return mk_val_set   <               VT(M)::second_type >(m) ; }
template<class M> ::uset  <               VT(M)::second_type >       mk_val_uset  (M const& m) { return mk_val_uset  <               VT(M)::second_type >(m) ; }
template<class M> ::vector<               VT(M)::second_type >       mk_val_vector(M const& m) { return mk_val_vector<               VT(M)::second_type >(m) ; }

// support container arg to standard utility functions
using std::sort          ;                              // keep std definitions
using std::stable_sort   ;                              // .
using std::binary_search ;                              // .
using std::lower_bound   ;                              // .
using std::min           ;                              // .
using std::max           ;                              // .
#define CMP ::function<bool(VT(T) const&,VT(T) const&)>
template<class T>          void              sort         ( T      & x ,                  CMP cmp ) {                                             ::sort         ( x.begin() , x.end() ,     cmp ) ; }
template<class T>          void              stable_sort  ( T      & x ,                  CMP cmp ) {                                             ::stable_sort  ( x.begin() , x.end() ,     cmp ) ; }
template<class T>          bool              binary_search( T const& x , VT(T) const& v , CMP cmp ) {                                     return  ::binary_search( x.begin() , x.end() , v , cmp ) ; }
template<class T> typename T::const_iterator lower_bound  ( T const& x , VT(T) const& v , CMP cmp ) {                                     return  ::lower_bound  ( x.begin() , x.end() , v , cmp ) ; }
template<class T>          VT(T)             min          ( T const& x ,                  CMP cmp ) { if (x.begin()==x.end()) return {} ; return *::min_element  ( x.begin() , x.end() ,     cmp ) ; }
template<class T>          VT(T)             max          ( T const& x ,                  CMP cmp ) { if (x.begin()==x.end()) return {} ; return *::max_element  ( x.begin() , x.end() ,     cmp ) ; }
template<class T>          void              sort         ( T      & x                            ) {                                             ::sort         ( x.begin() , x.end()           ) ; }
template<class T>          void              stable_sort  ( T      & x                            ) {                                             ::stable_sort  ( x.begin() , x.end()           ) ; }
template<class T>          bool              binary_search( T const& x , VT(T) const& v           ) {                                     return  ::binary_search( x.begin() , x.end() , v       ) ; }
template<class T> typename T::const_iterator lower_bound  ( T const& x , VT(T) const& v           ) {                                     return  ::lower_bound  ( x.begin() , x.end() , v       ) ; }
template<class T>          VT(T)             min          ( T const& x                            ) { if (x.begin()==x.end()) return {} ; return *::min_element  ( x.begin() , x.end()           ) ; }
template<class T>          VT(T)             max          ( T const& x                            ) { if (x.begin()==x.end()) return {} ; return *::max_element  ( x.begin() , x.end()           ) ; }
#undef CMP

#undef TVT

template<class T> T& grow( ::vector<T>& v , uint32_t i ) {
	if(i>=v.size()) v.resize(i+1) ;
	return v[i] ;
}

//
// streams
//

template<class Stream> struct FakeStream : Stream {
	struct Buf : ::streambuf {
		int underflow(   ) { return EOF ; }
		int overflow (int) { return EOF ; }
		int sync     (   ) { return 0   ; }
	} ;
	// cxtors & casts
	FakeStream() : Stream{&_buf} {}
	// data
protected :
	Buf _buf ;
} ;
using OFakeStream = FakeStream<::ostream> ;
using IFakeStream = FakeStream<::istream> ;

inline void _set_cloexec(::filebuf* fb) {
	int fd = np_get_fd(*fb) ;
	if (fd>=0) ::fcntl(fd,F_SETFD,FD_CLOEXEC) ;
}
inline void sanitize(::ostream& os) {
	os.exceptions(~os.goodbit) ;
	os<<::left<<::boolalpha ;
}
struct OFStream : ::ofstream {
	using Base = ::ofstream ;
	// cxtors & casts
	OFStream (                                                                     ) : Base{           } { sanitize(*this) ;                         }
	OFStream ( ::string const& f , ::ios_base::openmode om=::ios::out|::ios::trunc ) : Base{f,om       } { sanitize(*this) ; _set_cloexec(rdbuf()) ; }
	OFStream ( OFStream&& ofs                                                      ) : Base{::move(ofs)} {                                           }
	~OFStream(                                                                     )                     {                                           }
	//
	OFStream& operator=(OFStream&& ofs) { Base::operator=(::move(ofs)) ; return *this ; }
	// services
	template<class... A> void open(A&&... args) { Base::open(::forward<A>(args)...) ; _set_cloexec(rdbuf()) ; }
} ;
struct IFStream : ::ifstream {
	using Base = ::ifstream ;
	// cxtors & casts
	IFStream(                                 ) : Base{    } { exceptions(~goodbit) ; _set_cloexec(rdbuf()) ; }
	IFStream( ::string const& f               ) : Base{f   } { exceptions(~goodbit) ; _set_cloexec(rdbuf()) ; }
	IFStream( ::string const& f , openmode om ) : Base{f,om} { exceptions(~goodbit) ; _set_cloexec(rdbuf()) ; }
	//
	IFStream& operator=(IFStream&& ifs) { Base::operator=(::move(ifs)) ; return *this ; }
	// services
	template<class... A> void open(A&&... args) { Base::open(::forward<A>(args)...) ; _set_cloexec(rdbuf()) ; }
} ;

struct OStringStream : ::ostringstream {
	OStringStream() : ::ostringstream{} { sanitize(*this) ; }
} ;
struct IStringStream : ::istringstream {
	IStringStream(::string const& s) : ::istringstream{s} { exceptions(~goodbit) ; }
} ;

//
// string
//

static constexpr size_t Npos = ::string::npos ;

inline bool operator+(::string      const& s) { return !s.empty() ; }
inline bool operator!(::string      const& s) { return !+s        ; }
inline bool operator+(::string_view const& s) { return !s.empty() ; }
inline bool operator!(::string_view const& s) { return !+s        ; }

namespace std {
	inline                                                ::string operator+( ::string          && a , ::string_view const& b ) { return ::move     (a)+::string   (b) ; } // XXX : suppress with c++26
	inline                                                ::string operator+( ::string      const& a , ::string_view const& b ) { return             a +::string   (b) ; } // XXX : suppress with c++26
	inline                                                ::string operator+( ::string_view const& a , ::string      const& b ) { return ::string   (a)+            b  ; } // .
	template<::integral I> requires(!::is_same_v<I,char>) ::string operator+( ::string          && a , I                    b ) { return ::move     (a)+::to_string(b) ; }
	template<::integral I> requires(!::is_same_v<I,char>) ::string operator+( ::string      const& a , I                    b ) { return             a +::to_string(b) ; }
	template<::integral I> requires(!::is_same_v<I,char>) ::string operator+( I                    a , ::string const&      b ) { return ::to_string(a)+            b  ; }
	//
	inline                                                ::string& operator+=( ::string& a , ::string_view const& b ) { a += ::string   (b) ; return a ; }                // XXX : suppress with c++26
	template<::integral I> requires(!::is_same_v<I,char>) ::string& operator+=( ::string& a , I                    b ) { a += ::to_string(b) ; return a ; }
	// work around stupid += righ associativity
	template<class T> requires requires { ::ref(::string())+=::decay_t<T>() ; } ::string& operator<<( ::string& s , T&&                         v ) { s += ::forward<T>(v) ; return s ; }
	inline                                                                      ::string& operator<<( ::string& s , ::function<void(::string&)> f ) { f(s) ;                 return s ; }
}

template<class... A> ::string fmt_string(A const&... args) {
	OStringStream res ;
	[[maybe_unused]] bool _[] = { false , (res<<args,false)... } ;
	return ::move(res).str() ;
}

template<::integral I,IsOneOf<::string,::string_view> S> I from_string( S const& txt , bool empty_ok=false , bool hex=false ) {
	static constexpr bool IsBool = is_same_v<I,bool> ;
	if ( empty_ok && !txt ) return 0 ;
	::conditional_t<IsBool,size_t,I> res = 0/*garbage*/ ;
	//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::from_chars_result rc = ::from_chars( txt.data() , txt.data()+txt.size() , res , hex?16:10 ) ;
	//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( IsBool && res>1 ) throw "bool value must be 0 or 1"s       ;
	if ( rc.ec!=::errc{} ) throw ::make_error_code(rc.ec).message() ;
	else                   return res ;
}
template<::integral I> I from_string( const char* txt , bool empty_ok=false , bool hex=false ) { return from_string<I>( ::string_view(txt,strlen(txt)) , empty_ok , hex ) ; }
//
template<::floating_point F,IsOneOf<::string,::string_view> S> F from_string( S const& txt , bool empty_ok=false ) {
	if ( empty_ok && !txt ) return 0 ;
	F res = 0/*garbage*/ ;
	//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::from_chars_result rc = ::from_chars( txt.data() , txt.data()+txt.size() , res ) ;
	//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (rc.ec!=::errc{}) throw ::make_error_code(rc.ec).message() ;
	else                 return res ;
}
template<::floating_point F> F from_string( const char* txt , bool empty_ok=false ) { return from_string<F>( ::string_view(txt,strlen(txt)) , empty_ok ) ; }

/**/   ::string mk_json_str (::string_view  ) ;
/**/   ::string mk_shell_str(::string_view  ) ;
/**/   ::string mk_py_str   (::string_view  ) ;
inline ::string mk_py_str   (const char*   s) { return mk_py_str(::string_view(s,strlen(s))) ; }
inline ::string mk_py_str   (bool          b) { return b ? "True" : "False"                  ; }

// ::isspace is too high level as it accesses environment, which may not be available during static initialization
inline constexpr bool is_space(char c) {
	switch (c) {
		case '\f' :
		case '\n' :
		case '\r' :
		case '\t' :
		case '\v' :
		case ' '  : return true  ;
		default   : return false ;
	}
}

// ::isprint is too high level as it accesses environment, which may not be available during static initialization
inline constexpr bool is_printable(char c) {
	return uint8_t(c)>=0x20 && uint8_t(c)<=0x7e ;
}
inline bool is_printable(::string const& s) {
	for( char c : s ) if (!is_printable(c)) return false ;
	/**/                                    return true  ;
}

template<char Delimiter=0> ::string mk_printable(::string const&    ) ;
template<char Delimiter=0> ::string mk_printable(::string     && txt) {
	for( char c : txt ) if ( !is_printable(c) || (Delimiter&&c==Delimiter) ) return mk_printable(txt) ;
	return ::move(txt) ;                                                                                // fast path : avoid analysis & copy
}
template<char Delimiter=0> ::string parse_printable( ::string const& , size_t& pos ) ;

template<class T> requires(IsOneOf<T,::vector_s,::vmap_ss,::vmap_s<::vector_s>>) ::string mk_printable   ( T        const&               , bool empty_ok=true ) ;
template<class T> requires(IsOneOf<T,::vector_s,::vmap_ss,::vmap_s<::vector_s>>) T        parse_printable( ::string const& , size_t& pos , bool empty_ok=true ) ;

inline void     set_nl      (::string      & txt) { if ( +txt && txt.back()!='\n' ) txt += '\n'    ; }
inline void     set_no_nl   (::string      & txt) { if ( +txt && txt.back()=='\n' ) txt.pop_back() ; }
inline ::string ensure_nl   (::string     && txt) { set_nl   (txt) ; return txt ;                    }
inline ::string ensure_no_nl(::string     && txt) { set_no_nl(txt) ; return txt ;                    }
inline ::string ensure_nl   (::string const& txt) { return ensure_nl   (::copy(txt)) ;               }
inline ::string ensure_no_nl(::string const& txt) { return ensure_no_nl(::copy(txt)) ;               }

template<char C='\t',size_t N=1> ::string indent( ::string const& s , size_t i=1 ) {
	::string res ; res.reserve(s.size()+N*(s.size()>>4)) ;                           // anticipate lines of size 16, this is a reasonable pessimistic guess (as overflow is expensive)
	bool     sol = true ;
	for( char c : s ) {
		if (sol) for( size_t k=0 ; k<i*N ; k++ ) res += C ;
		res += c       ;
		sol  = c=='\n' ;
	}
	return res ;
}

inline bool is_identifier(::string const& s) {
	/**/              if (!s                               ) return false ;
	/**/              if (!( ::isalpha(s[0]) || s[0]=='_' )) return false ;
	for( char c : s ) if (!( ::isalnum(c   ) || c   =='_' )) return false ;
	/**/                                                     return true  ;
}

inline ::string strip(::string const& txt) {
	size_t start = 0          ;
	size_t end   = txt.size() ;
	while ( start<end && ::is_space(txt[start])) start++ ;
	while ( start<end && ::is_space(txt[end-1])) end  -- ;
	return txt.substr(start,end-start) ;
}

// split into space separated words
inline ::vector_s split(::string_view const& txt) {
	::vector_s res ;
	for( size_t pos=0 ;;) {
		for( ; pos<txt.size() && is_space(txt[pos]) ; pos++ ) ;
		if (pos==txt.size()) return res ;
		size_t start = pos ;
		for( ; pos<txt.size() && !is_space(txt[pos]) ; pos++ ) ;
		res.emplace_back( txt.substr(start,pos-start) ) ;
	}
	return res ;
}

// split on sep
inline ::vector_s split( ::string_view const& txt , char sep , size_t n_sep=Npos ) {
	::vector_s res ;
	size_t     pos = 0 ;
	for( size_t i=0 ; i<n_sep ; i++ ) {
		size_t   end    = txt.find(sep,pos) ;
		res.emplace_back( txt.substr(pos,end-pos) ) ;
		if (end==Npos) return res ;                   // we have exhausted all sep's
		pos = end+1 ;                                 // after the sep
	}
	res.emplace_back(txt.substr(pos)) ;               // all the remaining as last component after n_sep sep's
	return res ;
}

inline ::string_view first_lines( ::string_view const& txt , size_t n_sep , char sep='\n' ) {
	size_t pos = -1 ;
	for( size_t i=0 ; i<n_sep ; i++ ) {
		pos = txt.find(sep,pos+1) ;
		if (pos==Npos) return txt ;
	}
	return txt.substr(0,pos+1) ;
}

template<::integral I> I decode_int(const char* p) {
	I r = 0 ;
	for( uint8_t i=0 ; i<sizeof(I) ; i++ ) r |= I(uint8_t(p[i]))<<(i*8) ; // little endian, /!\ : beware of signs, casts & integer promotion
	return r ;
}

template<::integral I> void encode_int( char* p , I x ) {
	for( uint8_t i=0 ; i<sizeof(I) ; i++ ) p[i] = char(x>>(i*8)) ; // little endian
}

::string glb_subst( ::string&& txt , ::string const& sub , ::string const& repl ) ;

template<char U,::integral I=size_t> I        from_string_with_units(::string const& s) ;                                           // provide default unit in U. ...
template<char U,::integral I=size_t> ::string to_string_with_units  (I               x) ;                                           // ... If provided, return value is expressed in this unit
template<       ::integral I=size_t> I        from_string_with_units(::string const& s) { return from_string_with_units<0,I>(s) ; }
template<       ::integral I=size_t> ::string to_string_with_units  (I               x) { return to_string_with_units  <0,I>(x) ; }

//
// assert
//

extern thread_local char t_thread_key ;

void kill_self      ( int sig                        ) ;
void set_sig_handler( int sig , void (*handler)(int) ) ;
void write_backtrace( ::ostream& os , int hide_cnt   ) ;

template<class... A> [[noreturn]] void crash( int hide_cnt , int sig , A const&... args ) {
	static bool busy = false ;
	if (!busy) {                             // avoid recursive call in case syscalls are highjacked (hoping sig handler management are not)
		busy = true ;
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlink("/proc/self/exe",buf,PATH_MAX) ;
		if ( cnt>=0 || cnt<=PATH_MAX ) {
			/**/                   ::cerr << ::string_view(buf,cnt) ;
			if (t_thread_key!='?') ::cerr <<':'<< t_thread_key      ;
			/**/                   ::cerr <<" :"                    ;
		}
		[[maybe_unused]] bool _[] = {false,(::cerr<<' '<<args,false)...} ;
		::cerr << '\n' ;
		set_sig_handler(sig,SIG_DFL) ;
		write_backtrace(::cerr,hide_cnt+1) ; // rather than merely calling abort, this works even if crash_handler is not installed
		kill_self(sig) ;
	}
	set_sig_handler(SIGABRT,SIG_DFL) ;
	::abort() ;
}

#if !HAS_UNREACHABLE                         // defined in <utility> in c++23, use 202100 as g++-12 generates 202100 when -std=c++23
	[[noreturn]] inline void unreachable() {
		#ifdef __has_builtin
			#if __has_builtin(__builtin_unreachable)
				__builtin_unreachable() ;
			#else
				::abort() ;
			#endif
		#else
			::abort() ;
		#endif
	}
#endif

template<class... A> [[noreturn]] void fail( A const&... args [[maybe_unused]] ) {
	#ifndef NDEBUG
		crash( 1 , SIGABRT , "fail @" , args... ) ;
	#else
		unreachable() ;
	#endif
}

template<class... A> constexpr void throw_if    ( bool cond , A const&... args ) { if ( cond) throw fmt_string(args...) ; }
template<class... A> constexpr void throw_unless( bool cond , A const&... args ) { if (!cond) throw fmt_string(args...) ; }

template<class... A> constexpr void swear( bool cond , A const&... args [[maybe_unused]] ) {
	#ifndef NDEBUG
		if (!cond) crash( 1 , SIGABRT , "assertion violation @" , args... ) ;
	#else
		if (!cond) unreachable() ;
	#endif
}

template<class... A> [[noreturn]] void fail_prod( A const&... args ) {
	crash( 1 , SIGABRT , "fail @ " , args... ) ;
}

template<class... A> constexpr void swear_prod( bool cond , A const&... args ) {
	if (!cond) crash( 1 , SIGABRT , "assertion violation @" , args... ) ;
}

#define _FAIL_STR2(x) #x
#define _FAIL_STR(x) _FAIL_STR2(x)
#define FAIL(           ...) fail      (       __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__            __VA_OPT__(,": " #__VA_ARGS__ " =",)__VA_ARGS__)
#define FAIL_PROD(      ...) fail_prod (       __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__            __VA_OPT__(,": " #__VA_ARGS__ " =",)__VA_ARGS__)
#define SWEAR(     cond,...) swear     ((cond),__FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__,": " #cond __VA_OPT__(" : " #__VA_ARGS__ " =",)__VA_ARGS__)
#define SWEAR_PROD(cond,...) swear_prod((cond),__FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__,": " #cond __VA_OPT__(" : " #__VA_ARGS__ " =",)__VA_ARGS__)

#define DF default : FAIL() ; // for use at end of switch statements
#define DN default :        ; // .

inline bool/*done*/ kill_process( pid_t pid , int sig , bool as_group=false ) {
	swear_prod(pid>1,"killing process",pid) ;                                   // /!\ ::kill(-1) sends signal to all possible processes, ensure no system wide catastrophe
	//
	if (!as_group          ) return ::kill(pid,sig)==0 ;
	if (::kill(-pid,sig)==0) return true               ;                        // fast path : group exists, nothing else to do
	bool proc_killed  = ::kill( pid,sig)==0 ;                                   // else, there may be another possibility : the process to kill might not have had enough time to call setpgid(0,0) ...
	bool group_killed = ::kill(-pid,sig)==0 ;                                   // ... that makes it be a group, so kill it as a process, and kill the group again in case it was created inbetween
	return proc_killed || group_killed ;
}

inline void kill_self(int sig) {      // raise kills the thread, not the process
	int rc = ::kill(::getpid(),sig) ; // dont use kill_process as we call kill ourselves even if we are process 1 (in a namespace)
	SWEAR(rc==0) ;                    // killing outselves should always be ok
}

//
// vector_view
// mimic string_view with ::vector's instead of ::string's
//

template<class T> struct vector_view {
	static constexpr bool IsConst = ::is_const_v<T> ;
	using TNC        = ::remove_const_t<T>              ;
	using View       = vector_view                      ;
	using ViewC      = vector_view<const T>             ;
	using Vector     = ::vector<TNC>                    ;
	using String     = ::basic_string_view<AsChar<TNC>> ;
	using value_type = TNC                              ;
	using Iter       = ::conditional_t<IsConst,typename Vector::const_iterator,typename Vector::iterator> ;
	// cxtors & casts
	vector_view(                         ) = default ;
	vector_view( T*   begin , size_t sz  ) : _data{             begin } , _sz{sz       } {}
	vector_view( Iter begin , size_t sz  ) : _data{::to_address(begin)} , _sz{sz       } {}
	vector_view( T*   begin , T*     end ) : _data{             begin } , _sz{end-begin} {}
	vector_view( Iter begin , Iter   end ) : _data{::to_address(begin)} , _sz{end-begin} {}
	//
	vector_view& operator=(vector_view const&) = default ;
	//
	vector_view( vector_view const& v , size_t start=0 , size_t sz=-1 )                                   : _data(v.data()+start) , _sz(::min(sz,v.size()-start)) { SWEAR(start<=v.size()) ; }
	vector_view( Vector      const& v , size_t start=0 , size_t sz=-1 ) requires(  IsConst              ) : _data(v.data()+start) , _sz(::min(sz,v.size()-start)) { SWEAR(start<=v.size()) ; }
	vector_view( Vector           & v , size_t start=0 , size_t sz=-1 ) requires( !IsConst              ) : _data(v.data()+start) , _sz(::min(sz,v.size()-start)) { SWEAR(start<=v.size()) ; }
	vector_view( String      const& s                                 ) requires(  IsConst && IsChar<T> ) : _data(s.data()      ) , _sz(         s.size()       ) {                          }
	vector_view( String           & s                                 ) requires( !IsConst && IsChar<T> ) : _data(s.data()      ) , _sz(         s.size()       ) {                          }
	//
	explicit operator Vector() const {
		Vector res ;
		res.reserve(size()) ;
		for( T const& x : *this ) res.push_back(x) ;
		return res ;
	}
	// accesses
	bool operator+() const { return _sz     ; }
	bool operator!() const { return !+*this ; }
	//
	T      * data      (        ) const { return _data        ; }
	T      * begin     (        ) const { return _data        ; }
	T const* cbegin    (        ) const { return _data        ; }
	T      * end       (        ) const { return _data+_sz    ; }
	T const* cend      (        ) const { return _data+_sz    ; }
	T      & front     (        ) const { return _data[0    ] ; }
	T      & back      (        ) const { return _data[_sz-1] ; }
	T      & operator[](size_t i) const { return _data[i    ] ; }
	size_t   size      (        ) const { return  _sz         ; }
	// services
	View  subvec( size_t start , size_t sz=Npos ) const requires( IsConst) { return View ( begin()+start , ::min(sz,_sz-start) ) ; }
	ViewC subvec( size_t start , size_t sz=Npos ) const requires(!IsConst) { return ViewC( begin()+start , ::min(sz,_sz-start) ) ; }
	View  subvec( size_t start , size_t sz=Npos )       requires(!IsConst) { return View ( begin()+start , ::min(sz,_sz-start) ) ; }
	//
	void clear() { *this = {} ; }
	// data
protected :
	T*     _data = nullptr ;
	size_t _sz   = 0       ;
} ;
template<class T> struct c_vector_view : vector_view<T const> {
	using Base = vector_view<T const> ;
	using Base::begin ;
	using Base::_sz   ;
	// cxtors & casts
	using Base::Base ;
	c_vector_view( T const* begin                        , size_t sz    ) : Base{begin  ,sz} {}
	c_vector_view( ::vector<T> const& v , size_t start=0 , size_t sz=-1 ) : Base{v,start,sz} {}
	// services
	c_vector_view subvec( size_t start , size_t sz=Npos ) const { return c_vector_view( begin()+start , ::min(sz,_sz-start) ) ; }
} ;

using vector_view_s = vector_view<::string> ;
template<class K,class V> using vmap_view    = vector_view<::pair<K,V>> ;
template<        class V> using vmap_view_s  = vmap_view  <::string,V > ;
/**/                      using vmap_view_ss = vmap_view_s<::string   > ;
using c_vector_view_s = c_vector_view<::string> ;
template<class K,class V> using vmap_view_c    = c_vector_view<::pair<K,V>> ;
template<        class V> using vmap_view_c_s  = vmap_view_c  <::string,V > ;
/**/                      using vmap_view_c_ss = vmap_view_c_s<::string   > ;

//
// math
//

constexpr inline uint8_t n_bits(size_t n) { return NBits<size_t>-::countl_zero(n-1) ; } // number of bits to store n states

#define SCI static constexpr inline
template<::integral T=size_t> SCI T    bit_msk ( bool x ,             uint8_t b            ) {                           return T(x)<<b                                     ; }
template<::integral T=size_t> SCI T    bit_msk (                      uint8_t b            ) {                           return bit_msk<T>(true,b)                          ; }
template<::integral T=size_t> SCI T    lsb_msk ( bool x ,             uint8_t b            ) {                           return (bit_msk<T>(b)-1) & -T(x)                   ; }
template<::integral T=size_t> SCI T    lsb_msk (                      uint8_t b            ) {                           return lsb_msk<T>(true,b)                          ; }
template<::integral T=size_t> SCI T    msb_msk ( bool x ,             uint8_t b            ) {                           return (-bit_msk<T>(b)) & -T(x)                    ; }
template<::integral T=size_t> SCI T    msb_msk (                      uint8_t b            ) {                           return msb_msk<T>(true,b)                          ; }
template<::integral T       > SCI bool bit     ( T    x ,             uint8_t b            ) {                           return x&(1<<b)                                    ; } // get bit
template<::integral T       > SCI T    bit     ( T    x ,             uint8_t b   , bool v ) {                           return (x&~bit_msk<T>(b)) | bit_msk(v,b)           ; } // set bit
template<::integral T       > SCI T    bits_msk( T    x , uint8_t w , uint8_t lsb          ) { SWEAR(!(x&~lsb_msk(w))) ; return x<<lsb                                      ; }
template<::integral T=size_t> SCI T    bits_msk(          uint8_t w , uint8_t lsb          ) {                           return bits_msk<T>(lsb_msk(w),w,lsb)               ; }
template<::integral T       > SCI T    bits    ( T    x , uint8_t w , uint8_t lsb          ) {                           return (x>>lsb)&lsb_msk<T>(w)                      ; } // get bits
template<::integral T       > SCI T    bits    ( T    x , uint8_t w , uint8_t lsb , T    v ) {                           return (x&~bits_msk<T>(w,lsb)) | bits_msk(v,w,lsb) ; } // set bits
#undef SCI

template<class N,class D> constexpr N round_down(N n,D d) { return n - n%d             ; }
template<class N,class D> constexpr N div_down  (N n,D d) { return n/d                 ; }
template<class N,class D> constexpr N round_up  (N n,D d) { return round_down(n+d-1,d) ; }
template<class N,class D> constexpr N div_up    (N n,D d) { return div_down  (n+d-1,d) ; }

static constexpr double Infinity = ::numeric_limits<double>::infinity () ;
static constexpr double Nan      = ::numeric_limits<double>::quiet_NaN() ;

//
// stream formatting
//

namespace std {

	#define OP(...) inline ::ostream& operator<<( ::ostream& os , __VA_ARGS__ )
	template<class T,size_t N> OP(             T    const  a[N] ) { os <<'[' ; const char* sep="" ; for( T    const&  x    : a ) { os<<sep<<x         ; sep="," ; } return os <<']' ; }
	template<class T,size_t N> OP( array      <T,N> const& a    ) { os <<'[' ; const char* sep="" ; for( T    const&  x    : a ) { os<<sep<<x         ; sep="," ; } return os <<']' ; }
	template<class T         > OP( vector     <T  > const& v    ) { os <<'[' ; const char* sep="" ; for( T    const&  x    : v ) { os<<sep<<x         ; sep="," ; } return os <<']' ; }
	template<class T         > OP( vector_view<T  > const& v    ) { os <<'[' ; const char* sep="" ; for( T    const&  x    : v ) { os<<sep<<x         ; sep="," ; } return os <<']' ; }
	template<class K         > OP( uset       <K  > const& s    ) { os <<'{' ; const char* sep="" ; for( K    const&  k    : s ) { os<<sep<<k         ; sep="," ; } return os <<'}' ; }
	template<class K         > OP( set        <K  > const& s    ) { os <<'{' ; const char* sep="" ; for( K    const&  k    : s ) { os<<sep<<k         ; sep="," ; } return os <<'}' ; }
	template<class K,class V > OP( umap       <K,V> const& m    ) { os <<'{' ; const char* sep="" ; for( auto const& [k,v] : m ) { os<<sep<<k<<':'<<v ; sep="," ; } return os <<'}' ; }
	template<class K,class V > OP( map        <K,V> const& m    ) { os <<'{' ; const char* sep="" ; for( auto const& [k,v] : m ) { os<<sep<<k<<':'<<v ; sep="," ; } return os <<'}' ; }
	template<class A,class B > OP( pair       <A,B> const& p    ) { return os <<'('<< p.first <<','<< p.second <<')' ;                                                                }
	#undef OP

	inline ::ostream& operator<<( ::ostream& os , uint8_t const i ) { return os<<uint32_t(i) ; } // avoid output a char when actually a int
	inline ::ostream& operator<<( ::ostream& os , int8_t  const i ) { return os<<int32_t (i) ; } // .

}

//
// enum management
//

// fine trick to count arguments with cpp
#define _ENUM_N(...) _ENUM_N_(__VA_ARGS__, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define _ENUM_N_(                         _39,_38,_37,_36,_35,_34,_33,_32,_31,_30,_29,_28,_27,_26,_25,_24,_23,_22,_21,_20,_19,_18,_17,_16,_15,_14,_13,_12,_11,_10,_9,_8,_7,_6,_5,_4,_3,_2,_1, n,...) n

template<size_t Sz> constexpr ::array<char,Sz> _enum_split0(const char* comma_sep) {
	::array<char,Sz> res {} ;
	char* q   = res.data() ;
	bool  sep = true       ;
	for( const char* p=comma_sep ; *p ; p++ )
		switch (*p) {
			case ',' :
			case ' ' : if (!sep) { *q++ = 0  ; sep = true  ; } break ; // one separator is enough
			default  :             *q++ = *p ; sep = false ;
		}
	*q = 0 ;
	return res ;
}

template<size_t Sz> constexpr ::array<char,Sz*2> _enum_snake0(::array<char,Sz> const& camel0) { // at worst, snake inserts a _ before all chars, doubling the size
	::array<char,Sz*2> res   {}/*constexpr*/ ;
	char*              q     = res.data()    ;
	bool               first = true          ;
	for( char c : camel0 ) {
		if ( 'A'<=c && c<='Z' ) { { if (!first) *q++ = '_' ; } *q++ = 'a'+(c-'A') ; }
		else                                                   *q++ =      c      ;
		first = !c ;
	}
	return res ;
}

template<size_t Sz,size_t VSz> constexpr ::array<string_view,Sz> _enum_mk_tab(::array<char,VSz> const& vals) {
	::array<string_view,Sz> res  {}/*constexpr*/ ;
	const char*             item = vals.data()   ;
	for( size_t i=0 ; i<Sz ; i++ ) {
		size_t len = 0 ; while (item[len]) len++ ;
		res[i]  = {item,len} ;
		item   += len+1      ; // point to the start of the next one (it will not be dereferenced at the end)
	}
	return res ;
}

// IsStdEnum, N, EnumName and _EnumCamelsComma templates could be defined static instead of within an anonymous namespace
// but then, clang requires the specialization to be static as well while gcc forbids it
// using an anonymous namespace is ok with both and provides the same functionality
namespace {
	template<class T> constexpr bool IsStdEnum = false ;                          // unless specialized
}
template<class T> concept StdEnum = IsStdEnum<T> ;
namespace {
	template<StdEnum E> constexpr uint8_t    N                  = 0 /*garbage*/ ; // specialized for each enum
	template<StdEnum E> constexpr const char EnumName        [] = ""/*garbage*/ ; // .
	template<StdEnum E> constexpr const char _EnumCamelsComma[] = ""/*garbage*/ ; // .
}

template<StdEnum E> static constexpr E                                           All          = E(N<E>)                                                             ;
template<StdEnum E> static constexpr ::array<char,sizeof(_EnumCamelsComma<E>)  > _EnumCamels0 = _enum_split0<     sizeof(_EnumCamelsComma<E>)>(_EnumCamelsComma<E>) ;
template<StdEnum E> static constexpr ::array<char,sizeof(_EnumCamelsComma<E>)*2> _EnumSnakes0 = _enum_snake0                                  (_EnumCamels0    <E>) ;
template<StdEnum E> static constexpr ::array<::string_view,N<E>                > EnumCamels   = _enum_mk_tab<N<E>,sizeof(_EnumCamels0    <E>)>(_EnumCamels0    <E>) ;
template<StdEnum E> static constexpr ::array<::string_view,N<E>                > EnumSnakes   = _enum_mk_tab<N<E>,sizeof(_EnumSnakes0    <E>)>(_EnumSnakes0    <E>) ;

template<StdEnum E> static constexpr size_t NBits<E> = n_bits(N<E>) ;

template<StdEnum E> ::ostream& operator<<( ::ostream& os , E e ) {
	if (e<All<E>) return os << camel(e)        ;
	else          return os << "N+"<<(+e-N<E>) ;
}

template<StdEnum E> ::string_view camel     (E e) { return          EnumCamels<E>[+e]        ; }
template<StdEnum E> ::string_view snake     (E e) { return          EnumSnakes<E>[+e]        ; }
template<StdEnum E> ::string      camel_str (E e) { return ::string(EnumCamels<E>[+e])       ; }
template<StdEnum E> ::string      snake_str (E e) { return ::string(EnumSnakes<E>[+e])       ; }
template<StdEnum E> const char*   camel_cstr(E e) { return          EnumCamels<E>[+e].data() ; } // string_view's in this table have a terminating null
template<StdEnum E> const char*   snake_cstr(E e) { return          EnumSnakes<E>[+e].data() ; } // .

template<StdEnum E> ::umap_s<E> _mk_enum_tab() {
	::umap_s<E> res ;
	for( E e : All<E> ) {
		res[camel_str(e)] = e ;
		res[snake_str(e)] = e ;
	}
	return res ;
}
template<StdEnum E> ::pair<E,bool/*ok*/> _mk_enum(::string const& x) {
	static ::umap_s<E> const s_tab = _mk_enum_tab<E>() ;
	auto it = s_tab.find(x) ;
	if (it==s_tab.end()) return {{}        ,false/*ok*/} ;
	else                 return {it->second,true /*ok*/} ;
}

template<StdEnum E> bool can_mk_enum(::string const& x) {
	return _mk_enum<E>(x).second ;
}

template<StdEnum E> E mk_enum(::string const& x) {
	::pair<E,bool/*ok*/> res = _mk_enum<E>(x) ;
	if (!res.second) throw "cannot make enum "s+EnumName<E>+" from "+x ;
	return res.first ;
}

// usage of iterator over E : for( E e : All<E> ) {...}
#define ENUM(   E ,                               ... ) enum class E : uint8_t {__VA_ARGS__                    } ; _ENUM(E,__VA_ARGS__)
#define ENUM_1( E , eq1 ,                         ... ) enum class E : uint8_t {__VA_ARGS__,eq1                } ; _ENUM(E,__VA_ARGS__)
#define ENUM_2( E , eq1 , eq2 ,                   ... ) enum class E : uint8_t {__VA_ARGS__,eq1,eq2            } ; _ENUM(E,__VA_ARGS__)
#define ENUM_3( E , eq1 , eq2 , eq3 ,             ... ) enum class E : uint8_t {__VA_ARGS__,eq1,eq2,eq3        } ; _ENUM(E,__VA_ARGS__)
#define ENUM_4( E , eq1 , eq2 , eq3 , eq4 ,       ... ) enum class E : uint8_t {__VA_ARGS__,eq1,eq2,eq3,eq4    } ; _ENUM(E,__VA_ARGS__)
#define ENUM_5( E , eq1 , eq2 , eq3 , eq4 , eq5 , ... ) enum class E : uint8_t {__VA_ARGS__,eq1,eq2,eq3,eq4,eq5} ; _ENUM(E,__VA_ARGS__)

#define _ENUM(E,...) \
	namespace { \
		template<> [[maybe_unused]] constexpr bool       IsStdEnum       <E>   = true                 ; \
		template<> [[maybe_unused]] constexpr uint8_t    N               <E>   = _ENUM_N(__VA_ARGS__) ; \
		template<> [[maybe_unused]] constexpr const char EnumName        <E>[] = #E                   ; \
		template<> [[maybe_unused]] constexpr const char _EnumCamelsComma<E>[] = #__VA_ARGS__         ; \
	}

template<StdEnum E> using EnumUint = underlying_type_t<E>         ;
template<StdEnum E> using EnumInt  = ::make_signed_t<EnumUint<E>> ;

template<StdEnum E> constexpr EnumUint<E> operator+(E e) { return EnumUint<E>(e) ; }
template<StdEnum E> constexpr bool        operator!(E e) { return !+e            ; }
//
template<StdEnum E> constexpr E          operator+ (E  e,EnumInt<E> i) {                e = E(+e+ i) ; return e  ; }
template<StdEnum E> constexpr E&         operator+=(E& e,EnumInt<E> i) {                e = E(+e+ i) ; return e  ; }
template<StdEnum E> constexpr E          operator- (E  e,EnumInt<E> i) {                e = E(+e- i) ; return e  ; }
template<StdEnum E> constexpr EnumInt<E> operator- (E  e,E          o) { EnumInt<E> d ; d =   +e-+o  ; return d  ; }
template<StdEnum E> constexpr E&         operator-=(E& e,EnumInt<E> i) {                e = E(+e- i) ; return e  ; }
template<StdEnum E> constexpr E          operator++(E& e             ) {                e = E(+e+ 1) ; return e  ; }
template<StdEnum E> constexpr E          operator++(E& e,int         ) { E e_ = e ;     e = E(+e+ 1) ; return e_ ; }
template<StdEnum E> constexpr E          operator--(E& e             ) {                e = E(+e- 1) ; return e  ; }
template<StdEnum E> constexpr E          operator--(E& e,int         ) { E e_ = e ;     e = E(+e- 1) ; return e_ ; }
//
template<StdEnum E> constexpr E  operator& (E  e,E o) {           return ::min(e,o) ; }
template<StdEnum E> constexpr E  operator| (E  e,E o) {           return ::max(e,o) ; }
template<StdEnum E> constexpr E& operator&=(E& e,E o) { e = e&o ; return e          ; }
template<StdEnum E> constexpr E& operator|=(E& e,E o) { e = e|o ; return e          ; }
//
template<StdEnum E> E    decode_enum( const char* p ) { return E(decode_int<EnumUint<E>>(p)) ; }
template<StdEnum E> void encode_enum( char* p , E e ) { encode_int(p,+e) ;                     }

template<StdEnum E> struct BitMap {
	template<StdEnum> friend ::ostream& operator<<( ::ostream& , BitMap const ) ;
	using Elem =       E    ;
	using Val  = Uint<N<E>> ;
	// cxtors & casts
	BitMap() = default ;
	//
	constexpr explicit BitMap(Val v) : _val{v} {}
	//
	constexpr BitMap(E e1                                              ) : _val{Val((1<<+e1)                                                                                  )} {}
	constexpr BitMap(E e1,E e2                                         ) : _val{Val((1<<+e1)|(1<<+e2)                                                                         )} {}
	constexpr BitMap(E e1,E e2,E e3                                    ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)                                                                )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4                               ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)                                                       )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4,E e5                          ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)|(1<<+e5)                                              )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4,E e5,E e6                     ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)|(1<<+e5)|(1<<+e6)                                     )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4,E e5,E e6,E e7                ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)|(1<<+e5)|(1<<+e6)|(1<<+e7)                            )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4,E e5,E e6,E e7,E e8           ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)|(1<<+e5)|(1<<+e6)|(1<<+e7)|(1<<+e8)                   )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4,E e5,E e6,E e7,E e8,E e9      ) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)|(1<<+e5)|(1<<+e6)|(1<<+e7)|(1<<+e8)|(1<<+e9)          )} {}
	constexpr BitMap(E e1,E e2,E e3,E e4,E e5,E e6,E e7,E e8,E e9,E e10) : _val{Val((1<<+e1)|(1<<+e2)|(1<<+e3)|(1<<+e4)|(1<<+e5)|(1<<+e6)|(1<<+e7)|(1<<+e8)|(1<<+e9)|(1<<+e10))} {}
	//
	constexpr Val  operator+() const { return  _val ; }
	constexpr bool operator!() const { return !_val ; }
	// services
	constexpr bool    operator==( BitMap const&     ) const = default ;
	constexpr bool    operator<=( BitMap other      ) const { return !(  _val & ~other._val )    ;                   }
	constexpr bool    operator>=( BitMap other      ) const { return !( ~_val &  other._val )    ;                   }
	constexpr BitMap  operator~ (                   ) const { return BitMap(lsb_msk(N<E>)&~_val) ;                   }
	constexpr BitMap  operator& ( BitMap other      ) const { return BitMap(_val&other._val)     ;                   }
	constexpr BitMap  operator| ( BitMap other      ) const { return BitMap(_val|other._val)     ;                   }
	constexpr BitMap& operator&=( BitMap other      )       { *this = *this&other ; return *this ;                   }
	constexpr BitMap& operator|=( BitMap other      )       { *this = *this|other ; return *this ;                   }
	constexpr bool    operator[]( E      bit_       ) const { return bit(_val,+bit_)             ;                   }
	constexpr uint8_t popcount  (                   ) const { return ::popcount(_val)            ;                   }
	constexpr void    set       ( E flag , bool val )       { if (val) *this |= flag ; else *this &= ~BitMap(flag) ; } // operator~(E) is not always recognized because of namespace's
	// data
private :
	Val _val = 0 ;
} ;
//
template<StdEnum E> constexpr BitMap<E> operator~(E e) { return ~BitMap<E>(e)  ; }

template<StdEnum E> BitMap<E> mk_bitmap( ::string const& x , char sep=',' ) {
	BitMap<E> res ;
	for( ::string const& s : split(x,sep) ) res |= mk_enum<E>(s) ;
	return res ;
}

// enumerate
template<StdEnum E> struct EnumIterator {
	// cxtors & casts
	EnumIterator(E e) : val(e) {}
	// services
	bool operator==(EnumIterator const&) const = default ;
	void operator++()       { val++ ;      }
	E    operator* () const { return val ; }
	// data
	E val ;
} ;

template<StdEnum E> constexpr EnumIterator<E> begin(E  ) { return EnumIterator<E>(E(0)) ; }
template<StdEnum E> constexpr EnumIterator<E> end  (E e) { return EnumIterator<E>(e   ) ; }

template<StdEnum E> ::ostream& operator<<( ::ostream& os , BitMap<E> const bm ) {
	os <<'(' ;
	bool first = true ;
	for( E e : All<E> )
		if (bm[e]) {
			if (first) { os <<      e ; first = false ; }
			else       { os <<'|'<< e ;                 }
		}
	return os <<')' ;
}

// used to disambiguate some calls
ENUM( NewType , New )
static constexpr NewType New = NewType::New ;
ENUM( DfltType , Dflt )
static constexpr DfltType Dflt = DfltType::Dflt ;

// Bool3 is both generally useful and provides an example of use the previous helpers
ENUM(Bool3
,	No
,	Maybe
,	Yes
)
static constexpr Bool3 No    = Bool3::No    ;
static constexpr Bool3 Maybe = Bool3::Maybe ;
static constexpr Bool3 Yes   = Bool3::Yes   ;
inline Bool3  operator~  ( Bool3  b             ) {                return Bool3(+Yes-+b)                                                      ; }
inline Bool3  operator!  ( Bool3  b             ) {                return ~b                                                                  ; }
inline Bool3  operator|  ( Bool3  b1 , bool  b2 ) {                return  b2 ? Yes : b1                                                      ; }
inline Bool3  operator&  ( Bool3  b1 , bool  b2 ) {                return !b2 ? No  : b1                                                      ; }
inline Bool3  operator|  ( bool   b1 , Bool3 b2 ) {                return  b1 ? Yes : b2                                                      ; }
inline Bool3  operator&  ( bool   b1 , Bool3 b2 ) {                return !b1 ? No  : b2                                                      ; }
inline Bool3& operator|= ( Bool3& b1 , bool  b2 ) { b1 = b1 | b2 ; return b1                                                                  ; }
inline Bool3& operator&= ( Bool3& b1 , bool  b2 ) { b1 = b1 & b2 ; return b1                                                                  ; }
inline Bool3  common     ( Bool3  b1 , Bool3 b2 ) {                return b1==Yes ? (b2==Yes?Yes:Maybe) : b1==No ? ( b2==No?No:Maybe) : Maybe ; }
inline Bool3  common     ( Bool3  b1 , bool  b2 ) {                return b2      ? (b1==Yes?Yes:Maybe) :          ( b1==No?No:Maybe)         ; }
inline Bool3  common     ( bool   b1 , Bool3 b2 ) {                return b1      ? (b2==Yes?Yes:Maybe) :          ( b2==No?No:Maybe)         ; }
inline Bool3  common     ( bool   b1 , bool  b2 ) {                return b1      ? (b2     ?Yes:Maybe) :          (!b2    ?No:Maybe)         ; }

//
// mutexes
//

// prevent dead locks by associating a level to each mutex, so we can verify the absence of dead-locks even in absence of race
// use of identifiers (in the form of an enum) allows easy identification of the origin of misorder
ENUM( MutexLvl  // identify who is owning the current level to ease debugging
,	None
// level 1
,	Audit
,	JobExec
,	Rule
,	StartJob
// level 2
,	Backend     // must follow StartJob
// level 3
,	BackendId   // must follow Backend
,	Gil         // must follow Backend
,	NodeCrcDate // must follow Backend
,	Req         // must follow Backend
,	TargetDir   // must follow Backend
// level 4
,	Autodep1    // must follow Gil
,	Gather      // must follow Gil
,	Node        // must follow NodeCrcDate
,	Time        // must follow BackendId
// level 5
,	Autodep2    // must follow Autodep1
// inner (locks that take no other locks)
,	File
,	Hash
,	Sge
,	Slurm
,	SmallId
,	Thread
// very inner
,	Trace       // allow tracing anywhere (but tracing may call some syscall)
,	SyscallTab  // any syscall may need this mutex
)

extern thread_local MutexLvl t_mutex_lvl ;
template<MutexLvl Lvl_,bool S=false/*shared*/> struct _Mutex : ::conditional_t<S,::shared_timed_mutex,::timed_mutex> {
	using Base =                                               ::conditional_t<S,::shared_timed_mutex,::timed_mutex> ;
	static constexpr MutexLvl           Lvl     = Lvl_ ;
	static constexpr ::chrono::duration Timeout = 30s  ; // crash on dead-lock by setting a comfortable timeout on locks (regression passes with 35ms, so 30s should be very comfortable)
	// services
	void lock         (MutexLvl& lvl)             { SWEAR(t_mutex_lvl< Lvl,t_mutex_lvl) ; lvl = t_mutex_lvl ; t_mutex_lvl = Lvl ; swear_prod(Base::try_lock_for       (Timeout),"dead-lock") ; }
	void lock_shared  (MutexLvl& lvl) requires(S) { SWEAR(t_mutex_lvl< Lvl,t_mutex_lvl) ; lvl = t_mutex_lvl ; t_mutex_lvl = Lvl ; swear_prod(Base::try_lock_shared_for(Timeout),"dead-lock") ; }
	void unlock       (MutexLvl  lvl)             { SWEAR(t_mutex_lvl==Lvl,t_mutex_lvl) ; t_mutex_lvl = lvl ;                                Base::unlock             (       )              ; }
	void unlock_shared(MutexLvl  lvl) requires(S) { SWEAR(t_mutex_lvl==Lvl,t_mutex_lvl) ; t_mutex_lvl = lvl ;                                Base::unlock_shared      (       )              ; }
	#ifndef NDEBUG
		void swear_locked       ()             { SWEAR(t_mutex_lvl>=Lvl,t_mutex_lvl) ; SWEAR(!Base::try_lock       ()) ; }
		void swear_locked_shared() requires(S) { SWEAR(t_mutex_lvl>=Lvl,t_mutex_lvl) ; SWEAR(!Base::try_lock_shared()) ; }
	#else
		void swear_locked       ()             {}
		void swear_locked_shared() requires(S) {}
	#endif
} ;
template<MutexLvl Lvl> using Mutex       = _Mutex<Lvl,false/*shared*/> ;
template<MutexLvl Lvl> using SharedMutex = _Mutex<Lvl,true /*shared*/> ;

template<class M,bool S=false/*shared*/> struct Lock {
	// cxtors & casts
	Lock (                          ) = default ;
	Lock ( Lock&& l                 )              { *this = ::move(l) ;     }
	Lock ( M& m , bool do_lock=true ) : _mutex{&m} { if (do_lock) lock  () ; }
	~Lock(                          )              { if (_locked) unlock() ; }
	Lock& operator=(Lock&& l) {
		if (_locked) unlock() ;
		_mutex    = l._mutex  ;
		_lvl      = l._lvl    ;
		_locked   = l._locked ;
		l._locked = false     ;
		return *this ;
	}
	// services
	void swear_locked() requires(!S) { _mutex->swear_locked       () ;                                   }
	void swear_locked() requires( S) { _mutex->swear_locked_shared() ;                                   }
	void lock        () requires(!S) { SWEAR(!_locked) ; _locked = true  ; _mutex->lock         (_lvl) ; }
	void lock        () requires( S) { SWEAR(!_locked) ; _locked = true  ; _mutex->lock_shared  (_lvl) ; }
	void unlock      () requires(!S) { SWEAR( _locked) ; _locked = false ; _mutex->unlock       (_lvl) ; }
	void unlock      () requires( S) { SWEAR( _locked) ; _locked = false ; _mutex->unlock_shared(_lvl) ; }
	// data
	M*       _mutex = nullptr                   ; // must be !=nullptr when _locked
	MutexLvl _lvl   = MutexLvl::None/*garbage*/ ; // valid when _locked
	bool    _locked = false                     ;
} ;
template<class M,bool S=true/*shared*/> using SharedLock = Lock<M,S/*shared*/> ;

//
// miscellaneous
//

inline bool has_env(::string const& name) {
	return ::getenv(name.c_str()) ;
}
inline ::string get_env( ::string const& name , ::string const& dflt={} ) {
	if ( const char* c_path = ::getenv(name.c_str()) ) return c_path ;
	else                                               return dflt   ;
}
inline void set_env( ::string const& name , ::string const& val ) {
	int rc = ::setenv( name.c_str() , val.c_str() , true ) ;
	swear_prod(rc==0,"cannot setenv",name,"to",val) ;
}
inline void del_env(::string const& name) {
	int rc = ::unsetenv(name.c_str()) ;
	swear_prod(rc==0,"cannot unsetenv",name) ;
}

template<::unsigned_integral T,bool ThreadSafe=false> struct SmallIds {
	struct NoMutex {
		void lock  () {}
		void unlock() {}
	} ;
	struct NoLock {
		NoLock(NoMutex) {}
	} ;
private :
	using _Mutex   = ::conditional_t< ThreadSafe , Mutex<MutexLvl::SmallId> , NoMutex > ;
	using _Lock    = ::conditional_t< ThreadSafe , Lock<_Mutex>             , NoLock  > ;
	using _AtomicT = ::conditional_t< ThreadSafe , ::atomic<T>              , T       > ;
	// services
public :
	T acquire() {
		 T    res  ;
		_Lock lock { _mutex } ;
		if (!free_ids) {
			res = n_allocated ;
			if (n_allocated==::numeric_limits<T>::max()) throw "cannot allocate id"s ;
			n_allocated++ ;
		} else {
			res = *free_ids.begin() ;
			free_ids.erase(res) ;
		}
		n_acquired++ ;                                  // protected by _mutex
		SWEAR(n_acquired!=::numeric_limits<T>::min()) ; // ensure no overflow
		return res ;
	}
	void release(T id) {
		if (!id) return ;                               // id 0 has not been acquired
		_Lock lock { _mutex } ;
		SWEAR(!free_ids.contains(id)) ;                 // else, double release
		free_ids.insert(id) ;
		n_acquired-- ;                                  // protected by _mutex
		SWEAR(n_acquired!=::numeric_limits<T>::max()) ; // ensure no underflow
	}
	// data
	set<T>   free_ids    ;
	T        n_allocated = 1 ;                          // dont use id 0 so that it is free to mean "no id"
	_AtomicT n_acquired  = 0 ;                          // can be freely read by any thread if ThreadSafe
private :
	_Mutex _mutex ;
} ;

inline void fence() { ::atomic_signal_fence(::memory_order_acq_rel) ; } // ensure execution order in case of crash to guaranty disk integrity

template<class T,bool Fence=false> struct Save {
	 Save( T& ref , T const& val ) : saved{ref},_ref{ref} { _ref = val ; if (Fence) fence() ;                } // save and init, ensure sequentiality if asked to do so
	 Save( T& ref                ) : saved{ref},_ref{ref} {                                                  } // in some cases, we do not care about the value, just saving and restoring
	~Save(                       )                        {              if (Fence) fence() ; _ref = saved ; } // restore      , ensure sequentiality if asked to do so
	T saved ;
private :
	T& _ref ;
} ;
template<class T> using FenceSave = Save<T,true> ;

template<class T> struct SaveInc {
	 SaveInc(T& ref) : _ref{ref} { SWEAR(_ref<::numeric_limits<T>::max()) ; _ref++ ; } // increment
	~SaveInc(      )             { SWEAR(_ref>::numeric_limits<T>::min()) ; _ref-- ; } // restore
private :
	T& _ref ;
} ;

ENUM( Rc
,	Ok
,	Fail
,	Perm
,	Usage
,	Format
,	System
)

template<class... A> [[noreturn]] void exit( Rc rc , A const&... args ) {
	::cerr << ensure_nl(fmt_string(args...)) ;
	::std::exit(+rc) ;
}

struct First {
	bool operator()() { uint8_t v = _val ; _val = ::min(_val+1,2) ; return v==0 ; }
	//
	template<class T> T operator()( T&& first , T&& other=T() ) { return (*this)() ? ::forward<T>(first) : ::forward<T>(other) ; }
	//
	template<class T> T operator()( T&& first , T&& second , T&& other ) {
		uint8_t v = _val ;
		(*this)() ;
		switch (v) {
			case 0 : return ::forward<T>(first ) ;
			case 1 : return ::forward<T>(second) ;
			case 2 : return ::forward<T>(other ) ;
		DF}
	}
	//
	const char* operator()( const char* first ,                      const char* other="" ) { return operator()<const char*&>(first,       other) ; }
	const char* operator()( const char* first , const char* second , const char* other    ) { return operator()<const char*&>(first,second,other) ; }
private :
	uint8_t _val=0 ;
} ;

//
// Implementation
//

//
// string
//

inline constexpr bool _can_be_delimiter(char c) {                   // ensure delimiter does not clash with encoding
	if ( c=='\\'          ) return false ;
	if ( 'a'<=c && c<='z' ) return false ;
	if ( '0'<=c && c<='9' ) return false ;
	/**/                    return true  ;
}
// guarantees that result contains only printable characters and no Delimiter
template<char Delimiter> ::string mk_printable(::string const& s) { // encode s so that it is printable and contains no delimiter
	static_assert(_can_be_delimiter(Delimiter)) ;
	::string res ; res.reserve(s.size()) ;                          // typically, all characters are printable and nothing to add
	for( char c : s ) {
		switch (c) {
			case '\a' : res += "\\a"  ; break ;
			case '\b' : res += "\\b"  ; break ;
			case 0x1b : res += "\\e"  ; break ;
			case '\f' : res += "\\f"  ; break ;
			case '\n' : res += "\\n"  ; break ;
			case '\r' : res += "\\r"  ; break ;
			case '\t' : res += "\\t"  ; break ;
			case '\v' : res += "\\v"  ; break ;
			case '\\' : res += "\\\\" ; break ;
			default   :
				if ( is_printable(c) && c!=Delimiter ) {
					res += c ;
				} else {
					res += "\\x"                                                ;
					res += char( (c>>4 )>=10 ? 'a'+((c>>4 )-10) : '0'+(c>>4 ) ) ;
					res += char( (c&0xf)>=10 ? 'a'+((c&0xf)-10) : '0'+(c&0xf) ) ;
				}
		}
	}
	return res ;
}
// stop at Delimiter or any non printable char
template<char Delimiter> ::string parse_printable( ::string const& s , size_t& pos ) {
	static_assert(_can_be_delimiter(Delimiter)) ;
	SWEAR(pos<=s.size(),s,pos) ;
	::string res ;
	const char* s_c = s.c_str() ;
	for( char c ; (c=s_c[pos]) ; pos++ )
		if      (c==Delimiter    ) break/*for*/ ;
		else if (!is_printable(c)) break/*for*/ ;
		else if (c!='\\'         ) res += c ;
		else
			switch (s_c[++pos]) {
				case 'a'  : res += '\a'       ; break/*switch*/ ;
				case 'b'  : res += '\b'       ; break/*switch*/ ;
				case 'e'  : res += char(0x1b) ; break/*switch*/ ;
				case 'f'  : res += '\f'       ; break/*switch*/ ;
				case 'n'  : res += '\n'       ; break/*switch*/ ;
				case 'r'  : res += '\r'       ; break/*switch*/ ;
				case 't'  : res += '\t'       ; break/*switch*/ ;
				case 'v'  : res += '\v'       ; break/*switch*/ ;
				case '\\' : res += '\\'       ; break/*switch*/ ;
				//
				case 'x' : {
					char x = 0 ; if ( char d=s_c[++pos] ; d>='0' && d<='9' ) x += d-'0' ; else if ( d>='a' && d<='f' ) x += 10+d-'a' ; else throw "illegal hex digit "s+d ;
					x <<= 4    ; if ( char d=s_c[++pos] ; d>='0' && d<='9' ) x += d-'0' ; else if ( d>='a' && d<='f' ) x += 10+d-'a' ; else throw "illegal hex digit "s+d ;
					res += x ;
				} break/*switch*/ ;
				//
				default : throw "illegal code \\"s+s_c[pos] ;
			}
	return res ;
}

constexpr inline int8_t _unit_val(char u) {
	switch (u) {
		case 'E' : return  60 ; break ;
		case 'P' : return  50 ; break ;
		case 'T' : return  40 ; break ;
		case 'G' : return  30 ; break ;
		case 'M' : return  20 ; break ;
		case 'k' : return  10 ; break ;
		case 0   : return   0 ; break ;
		case 'm' : return -10 ; break ;
		case 'u' : return -20 ; break ;
		case 'n' : return -30 ; break ;
		case 'p' : return -40 ; break ;
		case 'f' : return -50 ; break ;
		case 'a' : return -60 ; break ;
		default  : throw "unrecognized suffix "s+u ;
	}
}
template<char U,::integral I> I from_string_with_units(::string const& s) {
	using I64 = ::conditional_t<is_signed_v<I>,int64_t,uint64_t> ;
	I64                 val     = 0 /*garbage*/                   ;
	const char*         s_start = s.c_str()                       ;
	const char*         s_end   = s_start+s.size()                ;
	::from_chars_result fcr     = ::from_chars(s_start,s_end,val) ;
	//
	if (fcr.ec!=::errc()) throw "unrecognized value "        +s ;
	if (fcr.ptr<s_end-1 ) throw "partially recognized value "+s ;
	//
	static constexpr int8_t B = _unit_val(U       ) ;
	/**/             int8_t b = _unit_val(*fcr.ptr) ;
	//
	if (b<=B) {
		if (uint8_t(B-b)>=NBits<I>) {
			val = 0 ;
		} else {
			val >>= uint8_t(B-b) ;
			if ( val > ::numeric_limits<I>::max() ) throw "overflow"s  ;
			if ( val < ::numeric_limits<I>::min() ) throw "underflow"s ;
		}
	} else {
		if (uint8_t(b-B)>=NBits<I>) {
			if ( val > 0 ) throw "overflow"s  ;
			if ( val < 0 ) throw "underflow"s ;
		} else {
			if ( val > I(::numeric_limits<I>::max()>>uint8_t(b-B)) ) throw "overflow"s  ;
			if ( val < I(::numeric_limits<I>::min()>>uint8_t(b-B)) ) throw "underflow"s ;
			val <<= uint8_t(b-B) ;
		}
	}
	return I(val) ;
}

template<char U,::integral I> ::string to_string_with_units(I x) {
	if (!x) {
		if (U) return "0"s+U ;
		else   return "0"    ;
	}
	//
	switch (U) {
		case 'a' : if (x&0x3ff) return ::to_string(x)+'a' ; x >>= 10 ; [[fallthrough]] ;
		case 'f' : if (x&0x3ff) return ::to_string(x)+'f' ; x >>= 10 ; [[fallthrough]] ;
		case 'p' : if (x&0x3ff) return ::to_string(x)+'p' ; x >>= 10 ; [[fallthrough]] ;
		case 'n' : if (x&0x3ff) return ::to_string(x)+'n' ; x >>= 10 ; [[fallthrough]] ;
		case 'u' : if (x&0x3ff) return ::to_string(x)+'u' ; x >>= 10 ; [[fallthrough]] ;
		case 'm' : if (x&0x3ff) return ::to_string(x)+'m' ; x >>= 10 ; [[fallthrough]] ;
		case 0   : if (x&0x3ff) return ::to_string(x)     ; x >>= 10 ; [[fallthrough]] ;
		case 'k' : if (x&0x3ff) return ::to_string(x)+'k' ; x >>= 10 ; [[fallthrough]] ;
		case 'M' : if (x&0x3ff) return ::to_string(x)+'M' ; x >>= 10 ; [[fallthrough]] ;
		case 'G' : if (x&0x3ff) return ::to_string(x)+'G' ; x >>= 10 ; [[fallthrough]] ;
		case 'T' : if (x&0x3ff) return ::to_string(x)+'T' ; x >>= 10 ; [[fallthrough]] ;
		case 'P' : if (x&0x3ff) return ::to_string(x)+'P' ; x >>= 10 ; [[fallthrough]] ;
		case 'E' :              return ::to_string(x)+'E' ;
	DF}
}
