// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <netinet/ip.h>                // in_addr_t, in_port_t
#include <signal.h>                    // SIG*, kill, killpg
#include <sys/file.h>                  // AT_*, F_*, FD_*, LOCK_*, O_*, fcntl, flock, openat

#include <cstring>                     // memcpy, strchr, strerror, strlen, strncmp, strnlen, strsignal

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>                    // from_chars_result
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

using namespace std ;                                                          // use std at top level so one write ::stuff instead of std::stuff
using std::getline ;                                                           // special case getline which also has a C version that hides std::getline

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
template<class T,class D=Void> using NoVoid = ::conditional_t<is_void_v<T>,D,T> ;
template<class T,T... X> requires(false) struct Err {} ; // for debug purpose, to be used as a tracing point through the diagnostic message

template<size_t NB> using Uint = ::conditional_t< NB<=8 , uint8_t , ::conditional_t< NB<=16 , uint16_t , ::conditional_t< NB<=32 , uint32_t , ::conditional_t< NB<=64 , uint64_t , void > > > > ;

template<class T,class... Ts> struct IsOneOfHelper ;
template<class T,class... Ts> concept IsOneOf = IsOneOfHelper<T,Ts...>::yes ;
template<class T                     > struct IsOneOfHelper<T         > { static constexpr bool yes = false                                 ; } ;
template<class T,class T0,class... Ts> struct IsOneOfHelper<T,T0,Ts...> { static constexpr bool yes = ::is_same_v<T,T0> || IsOneOf<T,Ts...> ; } ;

template<class T> concept IsChar = ::is_trivial_v<T> && ::is_standard_layout_v<T> ; // necessary property to make a ::basic_string
template<class T> using AsChar = ::conditional_t<IsChar<T>,T,char> ;                // provide default value if not a Char so as to make ::basic_string before knowing if it is possible

template<class D,class B> concept IsA       = ::is_same_v<B,D> || ::is_base_of_v<B,D> ;
template<class T        > concept IsNotVoid = !::is_void_v<T>                         ;

template<class T> static constexpr size_t NBits = sizeof(T)*8 ;

template<class T> T const& mk_const(T const& x) { return x ; }

//
// std lib name simplification
//

// array
template<                size_t N            > using array_s    = ::array             <       string,N    > ;
template<class K,class V,size_t N            > using amap       = ::array<pair        <K     ,V>    ,N    > ;
template<        class V,size_t N            > using amap_s     = ::amap              <string,V     ,N    > ;
template<                size_t N            > using amap_ss    = ::amap_s            <       string,N    > ;

// pair
template<class V                             > using pair_s     = ::pair              <string,V           > ;
/**/                                           using pair_ss    = ::pair_s            <       string      > ;

// map
template<        class V         ,class... Ts> using map_s_x    = ::map               <string,V     ,Ts...> ;
template<                         class... Ts> using map_ss_x   = ::map_s_x           <       string,Ts...> ;
template<        class V                     > using map_s      = ::map               <string,V           > ;
/**/                                           using map_ss     = ::map_s             <       string      > ;

// mmap
template<        class V         ,class... Ts> using mmap_s_x   = ::multimap          <string,V     ,Ts...> ;
template<                         class... Ts> using mmap_ss_x  = ::mmap_s_x          <       string,Ts...> ;
template<        class V                     > using mmap_s     = ::multimap          <string,V           > ;
/**/                                           using mmap_ss    = ::mmap_s            <       string      > ;

// mset
template<                         class... Ts> using mset_s_x   = ::multiset          <       string,Ts...> ;
/**/                                           using mset_s     = ::multiset          <       string      > ;

// set
template<                         class... Ts> using set_s_x    = ::set               <       string,Ts...> ;
/**/                                           using set_s      = ::set               <       string      > ;

// umap
template<class K,class V         ,class... Ts> using umap_x     = ::unordered_map     <K     ,V     ,Ts...> ;
template<        class V         ,class... Ts> using umap_s_x   = ::umap_x            <string,V     ,Ts...> ;
template<                         class... Ts> using umap_ss_x  = ::umap_s_x          <       string,Ts...> ;
template<class K,class V                     > using umap       = ::unordered_map     <K     ,V           > ;
template<        class V                     > using umap_s     = ::umap              <string,V           > ;
/**/                                           using umap_ss    = ::umap_s            <       string      > ;

// ummap
template<class K,class V         ,class... Ts> using ummap_x    = ::unordered_multimap<K     ,V     ,Ts...> ;
template<        class V         ,class... Ts> using ummap_s_x  = ::ummap_x           <string,V     ,Ts...> ;
template<                         class... Ts> using ummap_ss_x = ::ummap_s_x         <       string,Ts...> ;
template<class K,class V                     > using ummap      = ::unordered_multimap<K     ,V           > ;
template<        class V                     > using ummap_s    = ::ummap             <string,V           > ;
/**/                                           using ummap_ss   = ::ummap_s           <       string      > ;

// umset
template<class K                 ,class... Ts> using umset_x    = ::unordered_multiset<K            ,Ts...> ;
template<                         class... Ts> using umset_s_x  = ::umset_x           <string       ,Ts...> ;
template<class K                             > using umset      = ::unordered_multiset<K                  > ;
/**/                                           using umset_s    = ::umset             <string             > ;

// uset
template<class K                 ,class... Ts> using uset_x     = ::unordered_set     <K            ,Ts...> ;
template<                         class... Ts> using uset_s_x   = ::uset_x            <string       ,Ts...> ;
template<class K                             > using uset       = ::unordered_set     <K                  > ;
/**/                                           using uset_s     = ::uset              <string             > ;

// vector
template<                         class... Ts> using vector_s_x = ::vector            <       string,Ts...> ;
template<class K,class V         ,class... Ts> using vmap_x     = ::vector<pair<       K     ,V>    ,Ts...> ;
template<        class V         ,class... Ts> using vmap_s_x   = ::vmap_x            <string,V     ,Ts...> ;
template<                         class... Ts> using vmap_ss_x  = ::vmap_s_x          <       string,Ts...> ;
/**/                                           using vector_s   = ::vector            <       string      > ;
template<class K,class V                     > using vmap       = ::vector<pair<       K     ,V>          > ;
template<        class V                     > using vmap_s     = ::vmap              <string,V           > ;
/**/                                           using vmap_ss    = ::vmap_s            <       string      > ;

#define VT(T) typename T::value_type

// easy transformation of a container into another
template<class K,        class V> ::set   <K                                                   > mk_set   (V const& v) { return { v.cbegin() , v.cend() } ; }
template<class K,        class V> ::uset  <K                                                   > mk_uset  (V const& v) { return { v.cbegin() , v.cend() } ; }
template<        class T,class V> ::vector<                                  T                 > mk_vector(V const& v) { return { v.cbegin() , v.cend() } ; }
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
using std::sort          ; // keep std definitions
using std::stable_sort   ; // .
using std::binary_search ; // .
using std::min           ; // .
using std::max           ; // .
#define CMP ::function<bool(VT(T) const&,VT(T) const&)>
template<class T> void  sort         ( T      & x ,                  CMP cmp ) {                                             ::sort         ( x.begin() , x.end() ,     cmp ) ; }
template<class T> void  stable_sort  ( T      & x ,                  CMP cmp ) {                                             ::stable_sort  ( x.begin() , x.end() ,     cmp ) ; }
template<class T> bool  binary_search( T const& x , VT(T) const& v , CMP cmp ) {                                     return  ::binary_search( x.begin() , x.end() , v , cmp ) ; }
template<class T> VT(T) min          ( T const& x ,                  CMP cmp ) { if (x.begin()==x.end()) return {} ; return *::min_element  ( x.begin() , x.end() ,     cmp ) ; }
template<class T> VT(T) max          ( T const& x ,                  CMP cmp ) { if (x.begin()==x.end()) return {} ; return *::max_element  ( x.begin() , x.end() ,     cmp ) ; }
template<class T> void  sort         ( T      & x                            ) {                                             ::sort         ( x.begin() , x.end()           ) ; }
template<class T> void  stable_sort  ( T      & x                            ) {                                             ::stable_sort  ( x.begin() , x.end()           ) ; }
template<class T> bool  binary_search( T const& x , VT(T) const& v           ) {                                     return  ::binary_search( x.begin() , x.end() , v       ) ; }
template<class T> VT(T) min          ( T const& x                            ) { if (x.begin()==x.end()) return {} ; return *::min_element  ( x.begin() , x.end()           ) ; }
template<class T> VT(T) max          ( T const& x                            ) { if (x.begin()==x.end()) return {} ; return *::max_element  ( x.begin() , x.end()           ) ; }
#undef CMP

#undef TVT

template<class T> static inline T& grow( ::vector<T>& v , uint32_t i ) {
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

static inline void _set_cloexec(::filebuf* fb) {
	int fd = np_get_fd(*fb) ;
	if (fd>=0) ::fcntl(fd,F_SETFD,FD_CLOEXEC) ;
}
static inline void sanitize(::ostream& os) {
	os.exceptions(~os.goodbit) ;
	os<<::left<<::boolalpha ;
}
struct OFStream : ::ofstream {
	using Base = ::ofstream ;
	// cxtors & casts
	OFStream(                                 ) : Base{           } { sanitize(*this) ;                         }
	OFStream( ::string const& f               ) : Base{f          } { sanitize(*this) ; _set_cloexec(rdbuf()) ; }
	OFStream( ::string const& f , openmode om ) : Base{f,om       } { sanitize(*this) ; _set_cloexec(rdbuf()) ; }
	OFStream( OFStream&& ofs                  ) : Base{::move(ofs)} {                                           }
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

template<class... A> static inline ::string to_string(A const&... args) {
	OStringStream res ;
	[[maybe_unused]] bool _[] = { false , (res<<args,false)... } ;
	return res.str() ;
}
//
static inline ::string to_string(::string const& s) { return  s  ; }           // fast path
static inline ::string to_string(const char*     s) { return  s  ; }           // .
static inline ::string to_string(char            c) { return {c} ; }           // .
static inline ::string to_string(                 ) { return {}  ; }           // .

using ::std::from_chars ;
template<::integral I,IsOneOf<::string,::string_view> S> static inline I from_chars( S const& txt , bool empty_ok=false , bool hex=false ) {
	static constexpr bool IsBool = is_same_v<I,bool> ;
	if ( empty_ok && txt.empty() ) return 0 ;
	::conditional_t<IsBool,size_t,I> res = 0/*garbage*/ ;
	//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::from_chars_result rc = ::from_chars( txt.data() , txt.data()+txt.size() , res , hex?16:10 ) ;
	//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( IsBool && res>1 ) throw "bool value must be 0 or 1"s       ;
	if ( rc.ec!=::errc{} ) throw ::make_error_code(rc.ec).message() ;
	else                   return res ;
}
template<::integral I> static inline I from_chars( const char* txt , bool empty_ok=false , bool hex=false ) { return from_chars<I>( ::string_view(txt,strlen(txt)) , empty_ok , hex ) ; }
//
template<::floating_point F,IsOneOf<::string,::string_view> S> static inline F from_chars( S const& txt , bool empty_ok=false ) {
	if ( empty_ok && txt.empty() ) return 0 ;
	F res = 0/*garbage*/ ;
	//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::from_chars_result rc = ::from_chars( txt.data() , txt.data()+txt.size() , res ) ;
	//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if (rc.ec!=::errc{}) throw ::make_error_code(rc.ec).message() ;
	else                 return res ;
}
template<::floating_point F> static inline F from_chars( const char* txt , bool empty_ok=false ) { return from_chars<F>( ::string_view(txt,strlen(txt)) , empty_ok ) ; }

::string mk_py_str   ( ::string const&) ;
::string mk_shell_str( ::string const&) ;

// ::isspace is too high level as it accesses environment, which may not be available during static initialization
static inline constexpr bool is_space(char c) {
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
static inline constexpr bool is_print(char c) {
	return uint8_t(c)>=0x20 && uint8_t(c)<=0x7e ;
}

template<char Delimiter=0> ::string         mk_printable   ( ::string const&                ) ;
template<char Delimiter=0> ::pair_s<size_t> parse_printable( ::string const& , size_t pos=0 ) ;

static inline ::string ensure_nl(::string     && txt) { if ( !txt.empty() && txt.back()!='\n' ) txt += '\n' ; return txt ; }
static inline ::string ensure_nl(::string const& txt) { return ensure_nl(::string(txt)) ;                                  }

template<class T> static inline void _append_to_string( ::string& dst , T&&             x ) { dst += to_string(::forward<T>(x)) ; }
/**/              static inline void _append_to_string( ::string& dst , ::string const& s ) { dst +=                        s   ; } // fast path
/**/              static inline void _append_to_string( ::string& dst , const char*     s ) { dst +=                        s   ; } // .
/**/              static inline void _append_to_string( ::string& dst , char            c ) { dst +=                        c   ; } // .
template<class... A> void append_to_string( ::string& dst , A const&... args ) {
	[[maybe_unused]] bool _[] = { false , (_append_to_string(dst,args),false)... } ;
}

template<char C='\t',size_t N=1> static inline ::string indent( ::string const& s , size_t i=1 ) {
	::string res ; res.reserve(s.size()+N*(s.size()>>4)) ;                     // anticipate lines of size 16, this is a reasonable pessimistic guess (as overflow is expensive)
	bool     sol = true ;
	for( char c : s ) {
		if (sol) for( size_t k=0 ; k<i*N ; k++ ) res += C ;
		res += c       ;
		sol  = c=='\n' ;
	}
	return res ;
}

static inline bool is_identifier(::string const& s) {
	/**/              if (s.empty()                        ) return false ;
	/**/              if (!( ::isalpha(s[0]) || s[0]=='_' )) return false ;
	for( char c : s ) if (!( ::isalnum(c   ) || c   =='_' )) return false ;
	/**/                                                     return true  ;
}

// split into space separated words
static inline ::vector_s split(::string_view const& txt) {
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
static inline ::vector_s split( ::string_view const& txt , char sep , size_t n_sep=Npos ) {
	::vector_s res ;
	size_t     pos = 0 ;
	for( size_t i=0 ; i<n_sep ; i++ ) {
		size_t   end    = txt.find(sep,pos) ;
		res.emplace_back( txt.substr(pos,end-pos) ) ;
		if (end==Npos) return res ;                                            // we have exhausted all sep's
		pos = end+1 ;                                                          // after the sep
	}
	res.emplace_back(txt.substr(pos)) ;                                        // all the remaining as last component after n_sep sep's
	return res ;
}

template<class... A> static inline ::string mk_snake(A const&... args) {
	return mk_snake(to_string(args...)) ;
}
template<> inline ::string mk_snake<::string>(::string const& s) {
	::string res           ; res.reserve(s.size()+2) ;                         // 3 words is a reaonable pessimistic guess
	bool     start_of_word = true ;
	for( char c : s ) {
		if (::isupper(c)) {
			if (!start_of_word) res.push_back('_'         ) ;                  // convert CamelCase to snake_case
			/**/                res.push_back(::tolower(c)) ;
		} else {
			res.push_back(c) ;
		}
		start_of_word = !::isalnum(c) ;
	}
	return res ;
}

static inline ::string_view first_lines( ::string_view const& txt , size_t n_sep , char sep='\n' ) {
	size_t pos = -1 ;
	for( size_t i=0 ; i<n_sep ; i++ ) {
		pos = txt.find(sep,pos+1) ;
		if (pos==Npos) return txt ;
	}
	return txt.substr(0,pos+1) ;
}

template<::integral I> static inline I decode_int(const char* p) {
	I r = 0 ;
	for( uint8_t i=0 ; i<sizeof(I) ; i++ ) r |= I(uint8_t(p[i]))<<(i*8) ;      // little endian, /!\ : beware of signs, casts & integer promotion
	return r ;
}

template<::integral I> static inline void encode_int( char* p , I x ) {
	for( uint8_t i=0 ; i<sizeof(I) ; i++ ) p[i] = char(x>>(i*8)) ;             // little endian
}

::string glb_subst( ::string&& txt , ::string const& sub , ::string const& repl ) ;

template<char U,::integral I> I        from_string_with_units(::string const& s) ; // provide default unit in U. If provided, return value is expressed in this unit
template<char U,::integral I> ::string to_string_with_units  (I               x) ; // .
template<       ::integral I> I        from_string_with_units(::string const& s) { return from_string_with_units<0,I>(s) ; }
template<       ::integral I> ::string to_string_with_units  (I               x) { return to_string_with_units  <0,I>(x) ; }

//
// assert
//

static bool/*done*/ kill_self      ( int sig                        ) ;
/**/   void         set_sig_handler( int sig , void (*handler)(int) ) ;
/**/   void         write_backtrace( ::ostream& os , int hide_cnt   ) ;

template<class... A> [[noreturn]] void exit( int rc , A const&... args ) {
	::cerr << ensure_nl(to_string(args...)) ;
	::std::exit(rc) ;
}

template<class... A> [[noreturn]] void crash( int hide_cnt , int sig , A const&... args ) {
	static bool busy = false ;
	if (!busy) {                                                               // avoid recursive call in case syscalls are highjacked (hoping sig handler management are not)
		busy = true ;
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlink("/proc/self/exe",buf,PATH_MAX) ;
		if ( cnt>=0 || cnt<=PATH_MAX ) ::cerr << ::string_view(buf,cnt) <<" :" ;
		[[maybe_unused]] bool _[] ={false,(::cerr<<' '<<args,false)...} ;
		::cerr << '\n' ;
		set_sig_handler(sig,SIG_DFL) ;
		write_backtrace(::cerr,hide_cnt+1) ;                                   // rather than merely calling abort, this works even if crash_handler is not installed
		kill_self(sig) ;
	}
	set_sig_handler(SIGABRT,SIG_DFL) ;
	::abort() ;
}

[[noreturn]] static inline void unreachable() {
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

template<class... A> [[noreturn]] static inline void fail( A const&... args [[maybe_unused]] ) {
	#ifndef NDEBUG
		crash( 1 , SIGABRT , "fail : " , args... ) ;
	#else
		unreachable() ;
	#endif
}

template<class... A> static inline constexpr void swear( bool cond , A const&... args [[maybe_unused]] ) {
	#ifndef NDEBUG
		if (!cond) crash( 1 , SIGABRT , "assertion violation : " , args... ) ;
	#else
		if (!cond) unreachable() ;
	#endif
}

template<class... A> [[noreturn]] static inline void fail_prod( A const&... args ) {
	crash( 1 , SIGABRT , "fail : " , args... ) ;
}

template<class... A> static inline constexpr void swear_prod( bool cond , A const&... args ) {
	if (!cond) crash( 1 , SIGABRT , "assertion violation : " , args... ) ;
}

#define _FAIL_STR2(x) #x
#define _FAIL_STR(x) _FAIL_STR2(x)
#define FAIL(           ...) fail      (       __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__            __VA_OPT__(,": " #__VA_ARGS__ " =",)__VA_ARGS__                 )
#define FAIL_PROD(      ...) fail_prod (       __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__            __VA_OPT__(,": " #__VA_ARGS__ " =",)__VA_ARGS__                 )
#define SWEAR(     cond,...) swear     ((cond),__FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__,": " #cond __VA_OPT__(" ( " #__VA_ARGS__ " =",)__VA_ARGS__ __VA_OPT__(,')'))
#define SWEAR_PROD(cond,...) swear_prod((cond),__FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__,": " #cond __VA_OPT__(" ( " #__VA_ARGS__ " =",)__VA_ARGS__ __VA_OPT__(,')'))

static inline bool/*done*/ kill_process(pid_t pid,int sig) { swear_prod(pid>1,"killing process ",pid," !") ; return ::kill  (pid,sig)==0 ;         } // ensure no system wide catastrophe !
static inline bool/*done*/ kill_group  (pid_t pid,int sig) { swear_prod(pid>1,"killing process ",pid," !") ; return ::killpg(pid,sig)==0 ;         } // .
static inline bool/*done*/ kill_self   (          int sig) {                                                 return kill_process(::getpid(),sig) ; } // raise kills the thread, not the process

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
	T      * data      (        ) const { return _data        ; }
	T      * begin     (        ) const { return _data        ; }
	T const* cbegin    (        ) const { return _data        ; }
	T      * end       (        ) const { return _data+_sz    ; }
	T const* cend      (        ) const { return _data+_sz    ; }
	T      & front     (        ) const { return _data[0    ] ; }
	T      & back      (        ) const { return _data[_sz-1] ; }
	T      & operator[](size_t i) const { return _data[i    ] ; }
	size_t   size      (        ) const { return  _sz         ; }
	bool     empty     (        ) const { return !_sz         ; }
	// services
	View  subvec( size_t start , size_t sz=Npos ) const requires( IsConst) { return View ( begin()+start , ::min(sz,_sz-start) ) ; }
	ViewC subvec( size_t start , size_t sz=Npos ) const requires(!IsConst) { return ViewC( begin()+start , ::min(sz,_sz-start) ) ; }
	View  subvec( size_t start , size_t sz=Npos )       requires(!IsConst) { return View ( begin()+start , ::min(sz,_sz-start) ) ; }
	//
	void clear() { *this = vector_view() ; }
	// data
protected :
	T*     _data = nullptr ;
	size_t _sz   = 0       ;
} ;
template<class T> struct vector_view_c : vector_view<T const> {
	using Base = vector_view<T const> ;
	using Base::begin ;
	using Base::_sz   ;
	// cxtors & casts
	using Base::Base ;
	vector_view_c( T const* begin                        , size_t sz    ) : Base{begin  ,sz} {}
	vector_view_c( ::vector<T> const& v , size_t start=0 , size_t sz=-1 ) : Base{v,start,sz} {}
	// services
	vector_view_c subvec( size_t start , size_t sz=Npos ) const { return vector_view_c( begin()+start , ::min(sz,_sz-start) ) ; }
} ;

using vector_view_s = vector_view<::string> ;
template<class K,class V> using vmap_view    = vector_view<::pair<K,V>> ;
template<        class V> using vmap_view_s  = vmap_view  <::string,V > ;
/**/                      using vmap_view_ss = vmap_view_s<::string   > ;
using vector_view_c_s = vector_view_c<::string> ;
template<class K,class V> using vmap_view_c    = vector_view_c<::pair<K,V>> ;
template<        class V> using vmap_view_c_s  = vmap_view_c  <::string,V > ;
/**/                      using vmap_view_c_ss = vmap_view_c_s<::string   > ;

//
// math
//

static constexpr inline uint8_t n_bits(size_t n) { return NBits<size_t>-::countl_zero(n-1) ; } // number of bits to store n states

#define SCI static constexpr inline
template<::integral T=size_t> SCI T    bit_msk ( bool x ,             uint8_t b            ) {                           return T(x)<<b                                     ; }
template<::integral T=size_t> SCI T    bit_msk (                      uint8_t b            ) {                           return bit_msk<T>(true,b)                          ; }
template<::integral T=size_t> SCI T    lsb_msk ( bool x ,             uint8_t b            ) {                           return (bit_msk<T>(b)-1) & -T(x)                   ; }
template<::integral T=size_t> SCI T    lsb_msk (                      uint8_t b            ) {                           return lsb_msk<T>(true,b)                          ; }
template<::integral T=size_t> SCI T    msb_msk ( bool x ,             uint8_t b            ) {                           return (-bit_msk<T>(b)) & -T(x)                    ; }
template<::integral T=size_t> SCI T    msb_msk (                      uint8_t b            ) {                           return msb_msk<T>(true,b)                          ; }
template<::integral T       > SCI bool bit     ( T    x ,             uint8_t b            ) {                           return x&(1<<b)                                    ; } // get bit
template<::integral T       > SCI T    bit     ( T    x ,             uint8_t b   , bool v ) {                           return x&~bit_msk<T>(b) | bit_msk(v,b)             ; } // set bit
template<::integral T       > SCI T    bits_msk( T    x , uint8_t w , uint8_t lsb          ) { SWEAR(!(x&~lsb_msk(w))) ; return x<<lsb                                      ; }
template<::integral T=size_t> SCI T    bits_msk(          uint8_t w , uint8_t lsb          ) {                           return bits_msk<T>(lsb_msk(w),w,lsb)               ; }
template<::integral T       > SCI T    bits    ( T    x , uint8_t w , uint8_t lsb          ) {                           return (x>>lsb)&lsb_msk<T>(w)                      ; } // get bits
template<::integral T       > SCI T    bits    ( T    x , uint8_t w , uint8_t lsb , T    v ) {                           return (x&~bits_msk<T>(w,lsb)) | bits_msk(v,w,lsb) ; } // set bits
#undef SCI

template<class N,class D> static constexpr inline N round_down(N n,D d) { return n - n%d             ; }
template<class N,class D> static constexpr inline N div_down  (N n,D d) { return n/d                 ; }
template<class N,class D> static constexpr inline N round_up  (N n,D d) { return round_down(n+d-1,d) ; }
template<class N,class D> static constexpr inline N div_up    (N n,D d) { return div_down  (n+d-1,d) ; }

static constexpr double Infinity = ::numeric_limits<double>::infinity() ;

//
// stream formatting
//

namespace std {

	#define OP(...) static inline ::ostream& operator<<( ::ostream& os , __VA_ARGS__ )
	template<class T,size_t N> OP(             T    const  a[N] ) { char sep='[' ; if (!N       ) os<<sep ; else for( T    const&  x    : a ) { os<<sep<<x         ; sep=',' ; } return os << ']' ; }
	template<class T,size_t N> OP( array      <T,N> const& a    ) { char sep='[' ; if (!N       ) os<<sep ; else for( T    const&  x    : a ) { os<<sep<<x         ; sep=',' ; } return os << ']' ; }
	template<class T         > OP( vector     <T  > const& v    ) { char sep='[' ; if (v.empty()) os<<sep ; else for( T    const&  x    : v ) { os<<sep<<x         ; sep=',' ; } return os << ']' ; }
	template<class T         > OP( vector_view<T  > const& v    ) { char sep='[' ; if (v.empty()) os<<sep ; else for( T    const&  x    : v ) { os<<sep<<x         ; sep=',' ; } return os << ']' ; }
	template<class K         > OP( uset       <K  > const& s    ) { char sep='{' ; if (s.empty()) os<<sep ; else for( K    const&  k    : s ) { os<<sep<<k         ; sep=',' ; } return os << '}' ; }
	template<class K         > OP( set        <K  > const& s    ) { char sep='{' ; if (s.empty()) os<<sep ; else for( K    const&  k    : s ) { os<<sep<<k         ; sep=',' ; } return os << '}' ; }
	template<class K,class V > OP( umap       <K,V> const& m    ) { char sep='{' ; if (m.empty()) os<<sep ; else for( auto const& [k,v] : m ) { os<<sep<<k<<':'<<v ; sep=',' ; } return os << '}' ; }
	template<class K,class V > OP( map        <K,V> const& m    ) { char sep='{' ; if (m.empty()) os<<sep ; else for( auto const& [k,v] : m ) { os<<sep<<k<<':'<<v ; sep=',' ; } return os << '}' ; }
	template<class A,class B > OP( pair       <A,B> const& p    ) { return os <<'('<< p.first <<','<< p.second <<')' ;                                                                              }
	#undef OP

	static inline ::ostream& operator<<( ::ostream& os , uint8_t const i ) { return os<<uint32_t(i) ; } // avoid output a char when actually a int
	static inline ::ostream& operator<<( ::ostream& os , int8_t  const i ) { return os<<int32_t (i) ; } // .

}

//
// enum management
//

// use N as unknown val, efficient and practical
// usage of iterator over E : for( E e : E::N ) {...}
#define ENUM(   E ,                               ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N                    } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_1( E , eq1 ,                         ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1                } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_2( E , eq1 , eq2 ,                   ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2            } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_3( E , eq1 , eq2 , eq3 ,             ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2,eq3        } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_4( E , eq1 , eq2 , eq3 , eq4 ,       ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2,eq3,eq4    } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_5( E , eq1 , eq2 , eq3 , eq4 , eq5 , ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2,eq3,eq4,eq5} ; _ENUM(E,#__VA_ARGS__)
#define _ENUM( E , vals_str ) \
	[[maybe_unused]] static inline ::string const& _s_enum_name(E) { static ::string name_str = #E ; return name_str ; } \
	[[maybe_unused]] static inline const char* _enum_name(E e) { \
		static char        cvals[     ]            = vals_str                            ; \
		static const char* tab  [+E::N]            = {}                                  ; \
		static bool        inited [[maybe_unused]] = (_enum_split(tab,cvals,+E::N),true) ; \
		return tab[+e] ;                                                                   \
	} \
	[[maybe_unused]] static inline ::ostream&      operator<<( ::ostream& os , E e ) { if (e<E::N) os << _enum_name(e) ; else os << "N+" << (e-E::N) ; return os ; } \
	\
	[[maybe_unused]] static inline EnumIterator<E> begin(E  ) { return EnumIterator<E>(E(0)) ; } \
	[[maybe_unused]] static inline EnumIterator<E> end  (E e) { return EnumIterator<E>(e   ) ; }

template<class E> concept StdEnum  = ::is_enum_v<E> && ::is_unsigned_v<underlying_type_t<E>> && requires() { E::N ; } ;

template<StdEnum E> using EnumUint = underlying_type_t<E>         ;
template<StdEnum E> using EnumInt  = ::make_signed_t<EnumUint<E>> ;

template<StdEnum E> static inline constexpr EnumUint<E> operator+(E e) { return EnumUint<E>(e) ; }
template<StdEnum E> static inline constexpr bool        operator!(E e) { return !+e            ; }
//
template<StdEnum E> static inline constexpr E          operator+ (E  e,EnumInt<E> i) {                e = E(+e+ i) ; return e  ; }
template<StdEnum E> static inline constexpr E&         operator+=(E& e,EnumInt<E> i) {                e = E(+e+ i) ; return e  ; }
template<StdEnum E> static inline constexpr E          operator- (E  e,EnumInt<E> i) {                e = E(+e- i) ; return e  ; }
template<StdEnum E> static inline constexpr EnumInt<E> operator- (E  e,E          o) { EnumInt<E> d ; d =   +e-+o  ; return d  ; }
template<StdEnum E> static inline constexpr E&         operator-=(E& e,EnumInt<E> i) {                e = E(+e- i) ; return e  ; }
template<StdEnum E> static inline constexpr E          operator++(E& e             ) {                e = E(+e+ 1) ; return e  ; }
template<StdEnum E> static inline constexpr E          operator++(E& e,int         ) { E e_ = e ;     e = E(+e+ 1) ; return e_ ; }
template<StdEnum E> static inline constexpr E          operator--(E& e             ) {                e = E(+e- 1) ; return e  ; }
template<StdEnum E> static inline constexpr E          operator--(E& e,int         ) { E e_ = e ;     e = E(+e- 1) ; return e_ ; }
//
template<StdEnum E> static inline constexpr E  operator& (E  e,E o) { SWEAR(e!=E::Unknown) ; SWEAR(o!=E::Unknown) ; return ::min(e,o) ; }
template<StdEnum E> static inline constexpr E  operator| (E  e,E o) { SWEAR(e!=E::Unknown) ; SWEAR(o!=E::Unknown) ; return ::max(e,o) ; }
template<StdEnum E> static inline constexpr E& operator&=(E& e,E o) { e = e&o                                     ; return e          ; }
template<StdEnum E> static inline constexpr E& operator|=(E& e,E o) { e = e|o                                     ; return e          ; }
//
template<StdEnum E> static inline E    decode_enum( const char* p ) { return E(decode_int<EnumUint<E>>(p)) ; }
template<StdEnum E> static inline void encode_enum( char* p , E e ) { encode_int(p,+e) ;                     }

template<StdEnum E> struct BitMap {
	template<StdEnum> friend ::ostream& operator<<( ::ostream& , BitMap const ) ;
	using Elem =       E     ;
	using Val  = Uint<+E::N> ;
	static const BitMap None ;
	static const BitMap All  ;
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
	constexpr bool    operator==(BitMap const&) const = default ;
	constexpr bool    operator<=(BitMap other ) const { return !(  _val & ~other._val )      ; }
	constexpr bool    operator>=(BitMap other ) const { return !( ~_val &  other._val )      ; }
	constexpr BitMap  operator~ (             ) const { return BitMap(lsb_msk(+E::N)&~_val)  ; }
	constexpr BitMap  operator& (BitMap other ) const { return BitMap(_val&other._val)       ; }
	constexpr BitMap  operator| (BitMap other ) const { return BitMap(_val|other._val)       ; }
	constexpr BitMap& operator&=(BitMap other )       { *this = *this & other ; return *this ; }
	constexpr BitMap& operator|=(BitMap other )       { *this = *this | other ; return *this ; }
	constexpr bool    operator[](E      bit_  ) const { return bit(_val,+bit_)               ; }
	constexpr uint8_t popcount  (             ) const { return ::popcount(_val)              ; }
	// data
private :
	Val _val = 0 ;
} ;
template<StdEnum E> constexpr BitMap<E> BitMap<E>::None =  BitMap<E>() ;
template<StdEnum E> constexpr BitMap<E> BitMap<E>::All  = ~BitMap<E>() ;
//
template<StdEnum E> static inline constexpr BitMap<E>   operator~(E e) { return ~BitMap<E>(e)  ; }

static inline void _enum_split( const char** tab , char* str , size_t n ) {
	const char* start = nullptr ;
	size_t      i     = 0       ;
	for( char* p=str ; *p ; p++ )
		if ( is_space(*p) || *p==',' ) { if ( start) { tab[i++]=start ; *p=0 ; start = nullptr ; } }
		else                           { if (!start) {                         start = p       ; } }
	if (start) tab[i++] = start ;
	SWEAR( i==n , i , n ) ;
}

template<StdEnum E> ::map_s<E> _mk_enum_tab() {
	::map_s<E> res ;
	for( E e : E::N ) {
		::string s = _enum_name(e) ;
		res[         s ] = e ;                                                 // CamelCase
		res[mk_snake(s)] = e ;                                                 // snake_case
	}
	return res ;
}
template<StdEnum E> static inline E mk_enum_no_throw(::string const& x) {
	static map_s<E> const* s_tab = new ::map_s<E>{_mk_enum_tab<E>()} ;         // ensure table is never destroyed as we have no control of the order
	auto it = s_tab->find(x) ;
	if (it==s_tab->end()) return E::Unknown ;
	else                  return it->second ;
}

template<StdEnum E> static inline bool can_mk_enum(::string const& x) {
	return mk_enum_no_throw<E>(x)!=E::Unknown ;
}

template<StdEnum E> static inline E mk_enum(::string const& x) {
	E res = mk_enum_no_throw<E>(x) ;
	if (res==E::Unknown) throw to_string("cannot make enum ",_s_enum_name(E(0))," from ",x) ;
	return res ;
}

template<StdEnum E> static inline BitMap<E> mk_bitmap( ::string const& x , char sep=',' ) {
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

template<StdEnum E> ::ostream& operator<<( ::ostream& os , BitMap<E> const bm ) {
	os <<'(' ;
	bool first = true ;
	for( E e : E::N )
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
static inline Bool3  operator~  ( Bool3  b             ) {                return Bool3(+Bool3::N-1-+b)                                               ; }
static inline Bool3  operator!  ( Bool3  b             ) {                return ~b                                                                  ; }
static inline Bool3  operator|  ( Bool3  b1 , bool  b2 ) {                return  b2 ? Yes : b1                                                      ; }
static inline Bool3  operator&  ( Bool3  b1 , bool  b2 ) {                return !b2 ? No  : b1                                                      ; }
static inline Bool3  operator|  ( bool   b1 , Bool3 b2 ) {                return  b1 ? Yes : b2                                                      ; }
static inline Bool3  operator&  ( bool   b1 , Bool3 b2 ) {                return !b1 ? No  : b2                                                      ; }
static inline Bool3& operator|= ( Bool3& b1 , bool  b2 ) { b1 = b1 | b2 ; return b1                                                                  ; }
static inline Bool3& operator&= ( Bool3& b1 , bool  b2 ) { b1 = b1 & b2 ; return b1                                                                  ; }
static inline Bool3  common     ( Bool3  b1 , Bool3 b2 ) {                return b1==Yes ? (b2==Yes?Yes:Maybe) : b1==No ? ( b2==No?No:Maybe) : Maybe ; }
static inline Bool3  common     ( Bool3  b1 , bool  b2 ) {                return b2      ? (b1==Yes?Yes:Maybe) :          ( b1==No?No:Maybe)         ; }
static inline Bool3  common     ( bool   b1 , Bool3 b2 ) {                return b1      ? (b2==Yes?Yes:Maybe) :          ( b2==No?No:Maybe)         ; }
static inline Bool3  common     ( bool   b1 , bool  b2 ) {                return b1      ? (b2     ?Yes:Maybe) :          (!b2    ?No:Maybe)         ; }

//
// miscellaneous
//

static inline bool has_env(::string const& name) {
	return ::getenv(name.c_str()) ;
}
static inline ::string get_env( ::string const& name , ::string const& dflt={} ) {
	if ( const char* c_path = ::getenv(name.c_str()) ) return c_path ;
	else                                               return dflt   ;
}
static inline void set_env( ::string const& name , ::string const& val ) {
	int rc = ::setenv( name.c_str() , val.c_str() , true ) ;
	swear_prod(rc==0,"cannot setenv ",name," to ",val) ;
}
static inline void del_env(::string const& name) {
	int rc = ::unsetenv(name.c_str()) ;
	swear_prod(rc==0,"cannot unsetenv ",name) ;
}

::string beautify_filename(::string const&) ;

template<::unsigned_integral T> struct SmallIds {
	T acquire() {
		T res ;
		if (free_ids.empty()) {
			res = n_allocated ;
			n_allocated++ ;
			SWEAR(n_allocated) ;                                               // ensure no overflow
		} else {
			res = *free_ids.begin() ;
			free_ids.erase(res) ;
		}
		return res ;
	}
	void release(T id) {
		if (!id) return ;                                                      // id 0 has not been acquired
		SWEAR(!free_ids.contains(id)) ;                                        // else, double release
		free_ids.insert(id) ;
	}
	// data
	T n_acquired() const {
		return n_allocated - 1 - free_ids.size() ;
	}
	set<T> free_ids    ;
	T      n_allocated = 1 ;                                                   // dont use id 0 so that it is free to mean "no id"
} ;

static inline void fence() { ::atomic_signal_fence(::memory_order_acq_rel) ; } // ensure execution order in case of crash to guaranty disk integrity

template<class T> static inline T clone(T const& x) { return x ; }             // simply clone a value

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

//
// Implementation
//

//
// string
//

template<char Delimiter> ::string mk_printable(::string const& s) {
	static_assert( (Delimiter<'a'||Delimiter>'z') && Delimiter!='\\' && (!Delimiter||is_print(Delimiter)) ) ; // ensure delimiter does not clash with encoding
	::string res ; res.reserve(s.size()) ;                                                                    // typically, all characters are printable and nothing to add
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
			case Delimiter :
				if (Delimiter) { res += '\\'    ; res += Delimiter ; }         // protect delimiter
				else           { res += "\\x00" ;                    }         // no delimiter, use default encoding
			break ;
			default   :
				if (is_print(c)) res +=                                                            c   ;
				else             res += to_string("\\x",::right,::setfill('0'),::hex,::setw(2),int(c)) ;
		}
	}
	return res ;
}

template<char Delimiter> ::pair_s<size_t> parse_printable( ::string const& s , size_t pos ) {
	static_assert( (Delimiter<'a'||Delimiter>'z') && Delimiter!='\\' && (!Delimiter||is_print(Delimiter)) ) ; // ensure delimiter does not clash with encoding
	SWEAR(pos<=s.size(),s,pos) ;
	::string res ; res.reserve(s.size()) ;                                     // string can only shrink and typically does not
	const char* start = s.c_str()+pos ;
	for( const char* p=start ; *p ; p++ )
		if      (*p==Delimiter) return {res,p-start} ;
		else if (*p!='\\'     ) res += *p ;
		else {
			p++ ;
			switch (*p) {
				case 'a'  : res += '\a' ; break ;
				case 'b'  : res += '\b' ; break ;
				case 'e'  : res += 0x1b ; break ;
				case 'f'  : res += '\f' ; break ;
				case 'n'  : res += '\n' ; break ;
				case 'r'  : res += '\r' ; break ;
				case 't'  : res += '\t' ; break ;
				case 'v'  : res += '\v' ; break ;
				case '\\' : res += '\\' ; break ;
				case 'x' :
					p++ ;
					res += char(from_chars<uint8_t>(::string_view(p,2),false/*empty_ok*/,true/*hex*/)) ;
					p += 2 ;
				break ;
				default : throw "illegal \\ code"s ;
			}
		}
	if (Delimiter) throw "cannot find delimiter"s ;
	else           return {res,s.size()-pos}      ;
}

static constexpr inline int _unit_val(char u) {
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
		default  : throw to_string("unrecognized suffix ",u) ;
	}
}
template<char U,::integral I> I from_string_with_units(::string const& s) {
	using I64 = ::conditional_t<is_signed_v<I>,int64_t,uint64_t> ;
	I64                 val     = 0 /*garbage*/                   ;
	const char*         s_start = s.c_str()                       ;
	const char*         s_end   = s.c_str()+s.size()              ;
	::from_chars_result fcr     = ::from_chars(s_start,s_end,val) ;
	//
	if (fcr.ec!=::errc()) throw to_string("unrecognized value "        ,s) ;
	if (fcr.ptr<s_end-1 ) throw to_string("partially recognized value ",s) ;
	//
	static constexpr int B = _unit_val(U       ) ;
	/**/             int b = _unit_val(*fcr.ptr) ;
	//
	if (B>=b) {
		val >>= B-b ;
		if ( val > ::numeric_limits<I>::max() ) throw "overflow"s  ;
		if ( val < ::numeric_limits<I>::min() ) throw "underflow"s ;
		return I(val) ;
	} else {
		if ( val > ::numeric_limits<I>::max()>>(b-B) ) throw "overflow"s  ;
		if ( val < ::numeric_limits<I>::min()>>(b-B) ) throw "underflow"s ;
		return I(val<<(b-B)) ;
	}
}

template<char U,::integral I> ::string to_string_with_units(I x) {
	if (!x) return to_string(0,U) ;
	switch (U) {
		case 'a' : if (x&0x3ff) return to_string(x,'a') ; x >>= 10 ; [[fallthrough]] ;
		case 'f' : if (x&0x3ff) return to_string(x,'f') ; x >>= 10 ; [[fallthrough]] ;
		case 'p' : if (x&0x3ff) return to_string(x,'p') ; x >>= 10 ; [[fallthrough]] ;
		case 'n' : if (x&0x3ff) return to_string(x,'n') ; x >>= 10 ; [[fallthrough]] ;
		case 'u' : if (x&0x3ff) return to_string(x,'u') ; x >>= 10 ; [[fallthrough]] ;
		case 'm' : if (x&0x3ff) return to_string(x,'m') ; x >>= 10 ; [[fallthrough]] ;
		case 0   : if (x&0x3ff) return to_string(x    ) ; x >>= 10 ; [[fallthrough]] ;
		case 'k' : if (x&0x3ff) return to_string(x,'k') ; x >>= 10 ; [[fallthrough]] ;
		case 'M' : if (x&0x3ff) return to_string(x,'M') ; x >>= 10 ; [[fallthrough]] ;
		case 'G' : if (x&0x3ff) return to_string(x,'G') ; x >>= 10 ; [[fallthrough]] ;
		case 'T' : if (x&0x3ff) return to_string(x,'T') ; x >>= 10 ; [[fallthrough]] ;
		case 'P' : if (x&0x3ff) return to_string(x,'P') ; x >>= 10 ; [[fallthrough]] ;
		case 'E' :              return to_string(x,'E') ;
		default  : FAIL(U) ;
	}
}
