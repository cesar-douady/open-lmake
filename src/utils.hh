// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <features.h>                                                          // must be first as this impacts defs in system headers

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <link.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wait.h>

#include <cctype>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <ios>
#include <iostream>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sys_config.h"
#include "non_portable.hh"

using namespace std ; // use std at top level so one write ::stuff instead of std::stuff

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
using std::binary_search ; // .
using std::min           ; // .
using std::max           ; // .
#define CMP ::function<bool(VT(T) const&,VT(T) const&)>
template<class T> void  sort         ( T      & x ,                  CMP cmp ) {                                             ::sort         ( x.begin() , x.end() ,     cmp ) ; }
template<class T> bool  binary_search( T const& x , VT(T) const& v , CMP cmp ) {                                     return  ::binary_search( x.begin() , x.end() , v , cmp ) ; }
template<class T> VT(T) min          ( T const& x ,                  CMP cmp ) { if (x.begin()==x.end()) return {} ; return *::min_element  ( x.begin() , x.end() ,     cmp ) ; }
template<class T> VT(T) max          ( T const& x ,                  CMP cmp ) { if (x.begin()==x.end()) return {} ; return *::max_element  ( x.begin() , x.end() ,     cmp ) ; }
template<class T> void  sort         ( T      & x                            ) {                                             ::sort         ( x.begin() , x.end()           ) ; }
template<class T> bool  binary_search( T const& x , VT(T) const& v           ) {                                     return  ::binary_search( x.begin() , x.end() , v       ) ; }
template<class T> VT(T) min          ( T const& x                            ) { if (x.begin()==x.end()) return {} ; return *::min_element  ( x.begin() , x.end()           ) ; }
template<class T> VT(T) max          ( T const& x                            ) { if (x.begin()==x.end()) return {} ; return *::max_element  ( x.begin() , x.end()           ) ; }
#undef CMP

#undef TVT

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

template<class... A> ::string to_string(A const&... args) {
	OStringStream res ;
	[[maybe_unused]] bool _[] = { false , (res<<args,false)... } ;
	return res.str() ;
}
static inline ::string to_string(::string const& s) { return  s  ; }           // fast path
static inline ::string to_string(const char*     s) { return  s  ; }           // .
static inline ::string to_string(char            c) { return {c} ; }           // .

template<class T> static inline void _append_to_string( ::string& dst , T               x ) { dst += to_string(x) ; }
/**/              static inline void _append_to_string( ::string& dst , ::string const& s ) { dst +=           s  ; } // fast path
/**/              static inline void _append_to_string( ::string& dst , const char*     s ) { dst +=           s  ; } // .
/**/              static inline void _append_to_string( ::string& dst , char            c ) { dst +=           c  ; } // .
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

::string mk_printable(::string const&) ;
::string mk_py_str   (::string const&) ;
::string mk_shell_str(::string const&) ;

// split into space separated words
static inline ::vector_s split(::string_view const& path) {
	::vector_s res ;
	for( size_t pos=0 ;;) {
		for( ; pos<path.size() && ::isspace(path[pos]) ; pos++ ) ;
		if (pos==path.size()) return res ;
		size_t start = pos ;
		for( ; pos<path.size() && !::isspace(path[pos]) ; pos++ ) ;
		res.emplace_back( path.substr(start,pos-start) ) ;
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

template<::integral I> static inline I to_int(const char* p) {
	I r = 0 ;
	for( uint8_t i=0 ; i<sizeof(I) ; i++ ) r |= I(uint8_t(p[i]))<<(i*8) ;      // little endian, /!\ : beware of signs, casts, integer promotion, ...
	return r ;
}

template<::integral I> static inline void from_int( char* p , I x ) {
	for( uint8_t i=0 ; i<sizeof(I) ; i++ ) p[i] = char(x>>(i*8)) ;             // little endian
}

//
// assert
//

static inline void kill_self(int sig_num) {
	::kill(::getpid(),sig_num) ;                                               // raise kills the thread, not the process
}

void set_sig_handler( int sig_num , void (*handler)(int) ) ;

void write_backtrace( ::ostream& os , int hide_cnt ) ;

template<class... A> [[noreturn]] void exit( int rc , A const&... args ) {
	OStringStream err ;
	[[maybe_unused]] bool _[] ={false,(err<<args,false)...} ;
	::string err_str = err.str() ;
	if ( !err_str.empty() && err_str.back()!='\n' ) err_str.push_back('\n') ;
	::cerr << err_str ;
	::std::exit(rc) ;
}

template<class... A> [[noreturn]] void crash( int hide_cnt , int sig , A const&... args ) {
	char    buf[PATH_MAX] ;
	ssize_t cnt           = ::readlink("/proc/self/exe",buf,PATH_MAX) ;
	if ( cnt>=0 || cnt<=PATH_MAX ) ::cerr << ::string_view(buf,cnt) << " : " ;
	OStringStream err ;
	[[maybe_unused]] bool _[] ={false,(err<<args,false)...} ;
	::string err_str = err.str() ;
	if ( !err_str.empty() && err_str.back()!='\n' ) err_str.push_back('\n') ;
	::cerr << err_str ;
	set_sig_handler(sig,SIG_DFL) ;
	write_backtrace(::cerr,hide_cnt+1) ;                                       // rather than merely calling abort, this works even if crash_handler is not installed
	kill_self(sig) ;
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

#define SWEAR(     ...) swear     ((__VA_ARGS__),__FILE__,':',__LINE__," in ",__PRETTY_FUNCTION__," : ",#__VA_ARGS__                    ) // actually a single arg, but it could be such as foo<a,b>()
#define FAIL(      ...) fail      (              __FILE__,':',__LINE__," in ",__PRETTY_FUNCTION__," : ",#__VA_ARGS__," : ",(__VA_ARGS__)) // .
#define SWEAR_PROD(...) swear_prod((__VA_ARGS__),__FILE__,':',__LINE__," in ",__PRETTY_FUNCTION__," : ",#__VA_ARGS__                    ) // .
#define FAIL_PROD( ...) fail_prod (              __FILE__,':',__LINE__," in ",__PRETTY_FUNCTION__," : ",#__VA_ARGS__," : ",(__VA_ARGS__)) // .

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

template<class T,class... Ts> static constexpr inline ::common_type_t<T,Ts...> gcd( T arg , Ts... args ) {
	return std::gcd(arg,gcd(args...)) ;
}
template<class T> static constexpr inline T gcd(T arg) { return arg ; }
static constexpr inline int8_t gcd() { return 0 ; }

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
	//
	template<class T1,class T2                           > OP( pair <T1,T2         > const& p ) { return os<<'('<< p.first <<','<<p.second <<                                                ')' ; }
	/**/                                                   OP( tuple<              > const&   ) { return os<<'('<<                                                                           ')' ; }
	template<class T1                                    > OP( tuple<T1            > const& t ) { return os<<'('<<get<0>(t)<<                                                                ')' ; }
	template<class T1,class T2                           > OP( tuple<T1,T2         > const& t ) { return os<<'('<<get<0>(t)<<','<<get<1>(t)<<                                                ')' ; }
	template<class T1,class T2,class T3                  > OP( tuple<T1,T2,T3      > const& t ) { return os<<'('<<get<0>(t)<<','<<get<1>(t)<<','<<get<2>(t)<<                                ')' ; }
	template<class T1,class T2,class T3,class T4         > OP( tuple<T1,T2,T3,T4   > const& t ) { return os<<'('<<get<0>(t)<<','<<get<1>(t)<<','<<get<2>(t)<<','<<get<3>(t)<<                ')' ; }
	template<class T1,class T2,class T3,class T4,class T5> OP( tuple<T1,T2,T3,T4,T5> const& t ) { return os<<'('<<get<0>(t)<<','<<get<1>(t)<<','<<get<2>(t)<<','<<get<3>(t)<<','<<get<4>(t)<<')' ; }
	#undef OP

	static inline ::ostream& operator<<( ::ostream& os , uint8_t const i ) { return os<<uint32_t(i) ; } // avoid output a char when actually a int
	static inline ::ostream& operator<<( ::ostream& os , int8_t  const i ) { return os<<int32_t (i) ; } // .

}

//
// enum management
//

// use N as unknown val, efficient and practical
// usage of iterator over E : for( E e : E::N ) ...
#define ENUM(   E ,                               ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N                    } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_1( E , eq1 ,                         ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1                } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_2( E , eq1 , eq2 ,                   ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2            } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_3( E , eq1 , eq2 , eq3 ,             ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2,eq3        } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_4( E , eq1 , eq2 , eq3 , eq4 ,       ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2,eq3,eq4    } ; _ENUM(E,#__VA_ARGS__)
#define ENUM_5( E , eq1 , eq2 , eq3 , eq4 , eq5 , ... ) enum class E : uint8_t {__VA_ARGS__,N,Unknown=N,eq1,eq2,eq3,eq4,eq5} ; _ENUM(E,#__VA_ARGS__)
#define _ENUM( E , vals_str ) \
	[[maybe_unused]] static inline ::string const& _s_enum_name(E) { static ::string name_str = #E ; return name_str ; } \
	[[maybe_unused]] static inline const char* _enum_name(E e) { \
		static char        cvals[     ]    = vals_str                                    ; \
		static const char* tab  [+E::N]    = {}                                          ; \
		static bool        inited [[maybe_unused]] = (_enum_split(tab,cvals,+E::N),true) ; \
		return tab[+e] ;                                                                   \
	} \
	[[maybe_unused]] static inline ::ostream&      operator<<  ( ::ostream& os , E e ) { if (e<E::N) os << _enum_name(e) ; else os << "N+" << (e-E::N) ; return os                    ; } \
	[[maybe_unused]] static inline EnumIterator<E> begin       (                 E e ) { SWEAR(e==E::N) ;                                                return EnumIterator<E>(E(0)) ; } \
	[[maybe_unused]] static inline EnumIterator<E> end         (                 E e ) { SWEAR(e==E::N) ;                                                return EnumIterator<E>(E::N) ; }

template<class E> concept StdEnum  = ::is_enum_v<E> && ::is_unsigned_v<underlying_type_t<E>> && requires() { E::N ; } ;

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

template<StdEnum E> using EnumUint = underlying_type_t<E>         ;
template<StdEnum E> using EnumInt  = ::make_signed_t<EnumUint<E>> ;
//
template<StdEnum E> static inline constexpr EnumUint<E> operator+(E e) { return EnumUint<E>(e) ; }
template<StdEnum E> static inline constexpr bool        operator!(E e) { return !+e            ; }
template<StdEnum E> static inline constexpr BitMap<E>   operator~(E e) { return ~BitMap<E>(e)  ; }
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

static inline void _enum_split( const char** tab , char* str , size_t n ) {
	const char* start = nullptr ;
	size_t      i     = 0       ;
	for( char* p=str ; *p ; p++ )
		if ( ::isspace(*p) || *p==',' ) { if ( start) { tab[i++]=start ; *p=0 ; start = nullptr ; } }
		else                            { if (!start) {                         start = p       ; } }
	if (start) tab[i++] = start ;
	SWEAR(i==n) ;
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
	static map_s<E> const* s_tab = new map_s<E>{_mk_enum_tab<E>()} ;           // ensure table is never destroyed as we have no control of the order
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
// sockets
//

::string host() ;

struct Fd {
	friend ::ostream& operator<<( ::ostream& , Fd const& ) ;
	static const Fd Cwd    ;
	static const Fd Stdin  ;
	static const Fd Stdout ;
	static const Fd Stderr ;
	static const Fd Std    ;           // the highest standard fd
	// cxtors & casts
	constexpr Fd(                              ) = default ;
	constexpr Fd( Fd const& fd_                )           { *this =        fd_  ;   }
	constexpr Fd( Fd     && fd_                )           { *this = ::move(fd_) ;   }
	constexpr Fd( int       fd_                ) : fd{fd_} {                         }
	/**/      Fd( int       fd_ , bool no_std_ ) : fd{fd_} { if (no_std_) no_std() ; }
	//
	constexpr Fd& operator=(int       fd_) { fd = fd_    ;                return *this ; }
	constexpr Fd& operator=(Fd const& fd_) { fd = fd_.fd ;                return *this ; }
	constexpr Fd& operator=(Fd     && fd_) { fd = fd_.fd ; fd_.detach() ; return *this ; }
	//
	constexpr operator int  () const { return fd      ; }
	constexpr bool operator+() const { return fd>=0   ; }
	constexpr bool operator!() const { return !+*this ; }
	// services
	bool              operator== (Fd const&) const = default ;
	::strong_ordering operator<=>(Fd const&) const = default ;
	void write(::string_view const& data) {
		for( size_t cnt=0 ; cnt<data.size() ;) {
			ssize_t c = ::write(fd,data.data(),data.size()) ;
			SWEAR(c>0) ;
			cnt += c ;
		}
	}
	Fd             dup   () const { return ::dup(fd) ;                  }
	constexpr void close ()       { if (fd!=-1) ::close(fd) ; fd = -1 ; }
	constexpr void detach()       {                           fd = -1 ; }
	void no_std( int min_fd=Std.fd+1 ) {
		if ( !*this || fd>=min_fd ) return ;
		int new_fd = ::fcntl( fd , F_DUPFD_CLOEXEC , min_fd ) ;
		swear_prod(new_fd>=min_fd,"cannot duplicate ",fd) ;
		::close(fd) ;
		fd = new_fd ;
	}
	in_addr_t peer_addr() {
		static_assert(sizeof(in_addr_t)==4) ;                                  // else use adequate ntohs/ntohl according to the size
		struct sockaddr_in peer_addr ;
		socklen_t          len       = sizeof(peer_addr)                                                           ;
		int                rc        = ::getpeername( fd , reinterpret_cast<struct sockaddr*>(&peer_addr) , &len ) ;
		SWEAR(rc ==0                ) ;
		SWEAR(len==sizeof(peer_addr)) ;
		return ntohl(peer_addr.sin_addr.s_addr) ;
	}
	// data
	int fd = -1 ;
} ;
constexpr Fd Fd::Cwd   {int(AT_FDCWD)} ;
constexpr Fd Fd::Stdin {0            } ;
constexpr Fd Fd::Stdout{1            } ;
constexpr Fd Fd::Stderr{2            } ;
constexpr Fd Fd::Std   {2            } ;

struct AutoCloseFd : Fd {
	friend ::ostream& operator<<( ::ostream& , AutoCloseFd const& ) ;
	// cxtors & casts
	using Fd::Fd ;
	AutoCloseFd(AutoCloseFd&& acfd) : Fd{::move(acfd)} { acfd.detach() ; }
	AutoCloseFd(Fd         && fd_ ) : Fd{::move(fd_ )} {                 }
	//
	~AutoCloseFd() { close() ; }
	//
	AutoCloseFd& operator=(int           fd_ ) { if (fd!=fd_) close() ; fd = fd_ ; return *this ; }
	AutoCloseFd& operator=(AutoCloseFd&& acfd) { *this = acfd.fd ; acfd.detach() ; return *this ; }
	AutoCloseFd& operator=(Fd         && fd_ ) { *this = fd_ .fd ;                 return *this ; }
} ;

struct LockedFd : Fd {
	friend ::ostream& operator<<( ::ostream& , LockedFd const& ) ;
	// cxtors & casts
	LockedFd(                         ) = default ;
	LockedFd( Fd fd_ , bool exclusive ) : Fd{fd_}         { lock(exclusive) ; }
	LockedFd(LockedFd&& lfd           ) : Fd{::move(lfd)} { lfd.detach() ;    }
	//
	~LockedFd() { unlock() ; }
	//
	LockedFd& operator=(LockedFd&& lfd) { fd = lfd.fd ; lfd.detach() ; return *this ; }
	//
	void lock  (bool e) { if (fd>=0) flock(fd,e?LOCK_EX:LOCK_SH) ; }
	void unlock(      ) { if (fd>=0) flock(fd,  LOCK_UN        ) ; }
} ;

struct SockFd : AutoCloseFd {
	friend ::ostream& operator<<( ::ostream& , SockFd const& ) ;
	static constexpr in_addr_t LoopBackAddr = 0x7f000001 ;
	// statics
	static in_addr_t s_addr(::string const& server) ;
	// cxtors & casts
	using AutoCloseFd::AutoCloseFd ;
	SockFd(NewType) { init() ; }
	//
	void init() {
		*this = ::socket( AF_INET , SOCK_STREAM|SOCK_CLOEXEC , 0 ) ;
		no_std() ;
	}
	// services
	in_port_t port() const {
		struct sockaddr_in my_addr ;
		socklen_t          len     = sizeof(my_addr)                                                           ;
		int                rc      = ::getsockname( fd , reinterpret_cast<struct sockaddr*>(&my_addr) , &len ) ;
		SWEAR(rc ==0              ) ;
		SWEAR(len==sizeof(my_addr)) ;
		return ntohs(my_addr.sin_port) ;
	}
} ;

struct SlaveSockFd : SockFd {
	friend ::ostream& operator<<( ::ostream& , SlaveSockFd const& ) ;
	// cxtors & casts
	using SockFd::SockFd ;
} ;

struct ServerSockFd : SockFd {
	// statics
	static ::string s_addr_str(in_addr_t addr) {
		::string res ; res.reserve(15) ;                                       // 3 digits per level + 5 digits for the port
		/**/         res += to_string((addr>>24)&0xff) ;
		res += '.' ; res += to_string((addr>>16)&0xff) ;
		res += '.' ; res += to_string((addr>> 8)&0xff) ;
		res += '.' ; res += to_string((addr>> 0)&0xff) ;
		return res ;
	}

	// cxtors & casts
	using SockFd::SockFd ;
	// services
	void listen(int backlog=100) {
		if (!*this) init() ;
		int rc = ::listen(fd,backlog) ;
		swear_prod(rc==0,"cannot listen on ",*this," with backlog ",backlog," (",rc,')') ;
	}
	::string service(in_addr_t addr) { return to_string(s_addr_str(addr),':',port()) ; }
	::string service() const {
		return to_string(host(),':',port()) ;
	}
	SlaveSockFd accept() {
		SlaveSockFd slave_fd = ::accept( fd , nullptr , nullptr ) ;
		swear_prod(+slave_fd,"cannot accept from ",*this) ;
		return slave_fd ;
	}
} ;

struct ClientSockFd : SockFd {
	// cxtors & casts
	using SockFd::SockFd ;
	template<class... A> ClientSockFd(A&&... args) { connect(::forward<A>(args)...) ; }
	// services
	void connect( in_addr_t       server , in_port_t port ) ;
	void connect( ::string const& server , in_port_t port ) { connect( s_addr(server) , port ) ; }
	void connect( ::string const& service ) {
		size_t col = service.rfind(':') ;
		swear_prod(col!=Npos,"bad service : ",service) ;
		connect( service.substr(0,col) , ::stoul(service.substr(col+1)) ) ;
	}
} ;

namespace std {
	template<> struct hash<Fd          > { size_t operator()(Fd           const& fd) const { return fd ; } } ;
	template<> struct hash<AutoCloseFd > { size_t operator()(AutoCloseFd  const& fd) const { return fd ; } } ;
	template<> struct hash<SockFd      > { size_t operator()(SockFd       const& fd) const { return fd ; } } ;
	template<> struct hash<SlaveSockFd > { size_t operator()(SlaveSockFd  const& fd) const { return fd ; } } ;
	template<> struct hash<ServerSockFd> { size_t operator()(ServerSockFd const& fd) const { return fd ; } } ;
	template<> struct hash<ClientSockFd> { size_t operator()(ClientSockFd const& fd) const { return fd ; } } ;
}

//
// Epoll
//

struct Epoll {
	static constexpr uint64_t Forever = -1 ;
	struct Event : epoll_event {
		friend ::ostream& operator<<( ::ostream& , Event const& ) ;
		// cxtors & casts
		using epoll_event::epoll_event ;
		Event() { epoll_event::data.u64 = -1 ; }
		// access
		template<class T=uint32_t> T  data() const requires(sizeof(T)<=4) { return T       (epoll_event::data.u64>>32) ; }
		/**/                       Fd fd  () const                        { return uint32_t(epoll_event::data.u64)     ; }
	} ;
	// cxtors & casts
	Epoll (       ) = default ;
	Epoll (NewType) { init () ; }
	~Epoll(       ) { close() ; }
	// services
	void init() { fd = ::epoll_create1(EPOLL_CLOEXEC) ; }
	template<class T> void add( bool write , Fd fd_ , T data ) {
		static_assert(sizeof(T)<=4) ;
		epoll_event event { .events=write?EPOLLOUT:EPOLLIN , .data={.u64=(uint64_t(uint32_t(data))<<32)|uint32_t(fd_) } } ;
		int rc = epoll_ctl( int(fd) , EPOLL_CTL_ADD , int(fd_) , &event ) ;
		swear_prod(rc==0,"cannot add ",fd_," to epoll ",fd," (",strerror(errno),')') ;
		cnt++ ;
	}
	template<class T> void add_read ( Fd fd_ , T data ) { add(false/*write*/,fd_,data) ; }
	template<class T> void add_write( Fd fd_ , T data ) { add(true /*write*/,fd_,data) ; }
	void add      ( bool write , Fd fd_ ) { add(write         ,fd_,0) ; }
	void add_read (              Fd fd_ ) { add(false/*write*/,fd_  ) ; }
	void add_write(              Fd fd_ ) { add(true /*write*/,fd_  ) ; }
	void del(Fd fd_) {
		int rc = epoll_ctl( fd , EPOLL_CTL_DEL , fd_ , nullptr ) ;
		swear_prod(rc==0,"cannot del ",fd_," from epoll ",fd," (",strerror(errno),')') ;
		cnt-- ;
	}
	void close(Fd fd_) { SWEAR(+fd_) ; del(fd_) ; fd_.close() ; }
	void close(      ) {                          fd .close() ; }
	::vector<Event> wait(uint64_t timeout_ns=Forever) const ;
	// data
	Fd  fd  ;
	int cnt = 0 ;
} ;
::ostream& operator<<( ::ostream& , Epoll::Event const& ) ;

//
// threads
//

template<class T> struct ThreadQueue : private ::deque<T> {
private :
	using Base = ::deque<T> ;
public :
	using Base::empty ;
	using Base::size  ;
	// cxtors & casts
	ThreadQueue() = default ;
	bool operator+() const {
		::unique_lock lock{_mutex} ;
		return !Base::empty() ;
	}
	bool operator!() const { return !+*this ; }
	// services
	/**/                 void push   (T const& x) { ::unique_lock lock{_mutex} ; Base::push_back   (x                 ) ; _cond.notify_one() ; }
	template<class... A> void emplace(A&&...   a) { ::unique_lock lock{_mutex} ; Base::emplace_back(::forward<A>(a)...) ; _cond.notify_one() ; }
	T pop() {
		::unique_lock lock{_mutex} ;
		_cond.wait( lock , [&](){ return !Base::empty() ; } ) ;
		return _pop() ;
	}
	::pair<bool/*popped*/,T> try_pop() {
		::unique_lock lock{_mutex} ;
		if (empty()) return {false/*popped*/,T()} ;
		return {true/*popped*/,_pop()} ;
	}
	::pair<bool/*popped*/,T> pop(::stop_token tkn) {
		::unique_lock lock{_mutex} ;
		if (!_cond.wait( lock , tkn , [&](){ return !Base::empty() ; } )) return {false/*popped*/,T()} ;
		return {true/*popped*/,_pop()} ;
	}
private :
	T _pop() {
		T res = ::move(Base::front()) ;
		Base::pop_front() ;
		return res ;
	}
	// data
	::mutex mutable          _mutex ;
	::condition_variable_any _cond  ;
} ;

//
// processes
//

struct Pipe {
	// cxtors & casts
	Pipe(       ) = default ;
	Pipe(NewType) { open() ; }
	void open() {
		int fds[2] ;
		swear_prod( ::pipe(fds)==0 , "cannot create pipes" ) ;
		read  = fds[0] ;
		write = fds[1] ;
	}
	void close() {
		read .close() ;
		write.close() ;
	}
	// data
	Fd read  ;     // read  side of the pipe
	Fd write ;     // write side of the pipe
} ;

static inline bool/*was_blocked*/ set_sig( int sig_num , Bool3 block ) {
	sigset_t mask ;
	sigemptyset(&mask        ) ;
	sigaddset  (&mask,sig_num) ;
	//
	SWEAR(pthread_sigmask( block==Yes?SIG_BLOCK:SIG_UNBLOCK , block==Maybe?nullptr:&mask , &mask )==0) ;
	//
	return sigismember(&mask,sig_num)!=(block==Yes) ;
}
static inline bool/*did_block  */ block_sig  (int sig_num) { return set_sig(sig_num,Yes  ) ; }
static inline bool/*did_unblock*/ unblock_sig(int sig_num) { return set_sig(sig_num,No   ) ; }
static inline bool/*is_blocked */ probe_sig  (int sig_num) { return set_sig(sig_num,Maybe) ; }

static inline Fd open_sig_fd( int sig_num , bool block=false ) {
	if (block) swear_prod(block_sig(sig_num),"signal ",::strsignal(sig_num)," is already blocked") ;
	else       swear_prod(probe_sig(sig_num),"signal ",::strsignal(sig_num)," is not blocked"    ) ;
	//
	sigset_t mask ;
	sigemptyset(&mask        ) ;
	sigaddset  (&mask,sig_num) ;
	//
	return ::signalfd( -1 , &mask , SFD_CLOEXEC ) ;
}

static inline bool is_sig_sync(int sig_num) {
	switch (sig_num) {
		case SIGILL  :
		case SIGTRAP :
		case SIGABRT :
		case SIGBUS  :
		case SIGFPE  :
		case SIGSEGV : return true  ;
		default      : return false ;
	}
}

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

struct Child {
	static constexpr Fd None{-1} ;
	static constexpr Fd Pipe{-2} ;
	// cxtors & casts
	Child() = default ;
	Child(
		bool            as_group_          , ::vector_s const& args
	,	Fd              stdin_fd=Fd::Stdin , Fd                stdout_fd=Fd::Stdout , Fd stderr_fd=Fd::Stderr
	,	::map_ss const* env     =nullptr   , ::map_ss   const* add_env  =nullptr
	,	::string const& chroot  ={}
	,	::string const& cwd     ={}
	,	void (*pre_exec)()      =nullptr
	) {
		spawn(as_group_,args,stdin_fd,stdout_fd,stderr_fd,env,add_env,chroot,cwd,pre_exec) ;
	}
	~Child() {
		swear_prod(pid==-1,"bad pid ",pid) ;
	}
	// services
	bool/*parent*/ spawn(
		bool            as_group_          , ::vector_s const& args
	,	Fd              stdin_fd=Fd::Stdin , Fd                stdout_fd=Fd::Stdout , Fd stderr_fd=Fd::Stderr
	,	::map_ss const* env     =nullptr   , ::map_ss   const* add_env  =nullptr
	,	::string const& chroot  ={}
	,	::string const& cwd     ={}
	,	void (*pre_exec)()      =nullptr
	) ;
	void mk_daemon() {
		pid = -1 ;
		stdin .detach() ;
		stdout.detach() ;
		stderr.detach() ;
	}
	void waited() {
		pid = -1 ;
	}
	int/*wstatus*/ wait() {
		SWEAR(pid!=-1) ;
		int wstatus ;
		int rc = waitpid(pid,&wstatus,0) ;
		swear_prod(rc==pid,"cannot wait for pid ",pid) ;
		waited() ;
		return wstatus ;
	}
	bool wait_ok() {
		int wstatus = wait() ;
		return WIFEXITED(wstatus) && WEXITSTATUS(wstatus)==0 ;
	}
	void kill(int sig) {
		if (!sig) return ;
		if (as_group) ::kill(-pid,sig) ;                                       // kill group
		else          ::kill( pid,sig) ;                                       // kill process
	}
	//data
	pid_t       pid      = -1    ;
	AutoCloseFd stdin    ;
	AutoCloseFd stdout   ;
	AutoCloseFd stderr   ;
	bool        as_group = false ;
} ;

//
// miscellaneous
//

::string beautify_file_name(::string const& file_name) ;

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
	 Save( T& ref , T const& val ) : saved(ref),_ref(ref) {                      ref  = val   ; if (Fence) fence() ; } // save and init, ensure sequentiality if asked to do so
	 Save( T& ref                ) : saved(ref),_ref(ref) {                                                          } // in some cases, we do not care about the value, just saving and restoring
	~Save(                       )                        { if (Fence) fence() ; _ref = saved ;                      } // restore      , ensure sequentiality if asked to do so
	T  saved ;
private :
	T& _ref ;
} ;
template<class T> using FenceSave = Save<T,true> ;

