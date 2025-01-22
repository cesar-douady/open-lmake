// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <netinet/ip.h> // in_addr_t, in_port_t
#include <signal.h>     // SIG*, kill
#include <sys/file.h>   // AT_*, F_*, FD_*, LOCK_*, O_*, fcntl, flock, openat
#include <sys/types.h>  // ushort, uint, ulong, ...

#include <cstring> // memcpy, strchr, strerror, strlen, strncmp, strnlen, strsignal

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <charconv> // from_chars_result
#include <concepts>
#include <functional>
#include <ios>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sys_config.h"

using namespace std ; // use std at top level so one write ::stuff instead of std::stuff
using std::getline  ; // special case getline which also has a C version that hides std::getline

#define self (*this)

template<class T> requires requires(T const& x) { !+x ; } constexpr bool operator!(T const& x) { return !+x ; }

static constexpr size_t Npos = ::string::npos ;

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

template<class T> static constexpr T Max = ::numeric_limits<T>::max() ;
template<class T> static constexpr T Min = ::numeric_limits<T>::min() ;

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

namespace std {                                                                                                          // must be defined in std or operator! does not recognize it
	template<class T,size_t N> inline constexpr bool operator+(::array <T,N> const&  ) { return  N                   ; }
	template<class T,class  U> inline constexpr bool operator+(::pair  <T,U> const& p) { return  +p.first||+p.second ; }
	template<class K,class  V> inline constexpr bool operator+(::map   <K,V> const& m) { return !m.empty()           ; }
	template<class K,class  V> inline constexpr bool operator+(::umap  <K,V> const& m) { return !m.empty()           ; }
	template<class K         > inline constexpr bool operator+(::set   <K  > const& s) { return !s.empty()           ; }
	template<class K         > inline constexpr bool operator+(::uset  <K  > const& s) { return !s.empty()           ; }
	template<class T         > inline constexpr bool operator+(::vector<T  > const& v) { return !v.empty()           ; }
}

#define VT(T) typename T::value_type

// easy transformation of a container into another
template<class K,        class V> inline ::set   <K                                                   > mk_set   (V const& v) { return            { v.begin() , v.end() } ; }
template<class K,        class V> inline ::uset  <K                                                   > mk_uset  (V const& v) { return            { v.begin() , v.end() } ; }
template<        class T,class V> inline ::vector<                                  T                 > mk_vector(V const& v) { return ::vector<T>( v.begin() , v.end() ) ; }
template<class K,class T,class V> inline ::map   <K                                ,T                 > mk_map   (V const& v) { return            { v.begin() , v.end() } ; }
template<class K,class T,class V> inline ::umap  <K                                ,T                 > mk_umap  (V const& v) { return            { v.begin() , v.end() } ; }
template<class K,class T,class V> inline ::vmap  <K                                ,T                 > mk_vmap  (V const& v) { return            { v.begin() , v.end() } ; }
// with implicit key type
template<        class T,class V> inline ::map   <remove_const_t<VT(V)::first_type>,T                 > mk_map   (V const& v) { return            { v.begin() , v.end() } ; }
template<        class T,class V> inline ::umap  <remove_const_t<VT(V)::first_type>,T                 > mk_umap  (V const& v) { return            { v.begin() , v.end() } ; }
template<        class T,class V> inline ::vmap  <remove_const_t<VT(V)::first_type>,T                 > mk_vmap  (V const& v) { return            { v.begin() , v.end() } ; }
// with implicit item type
template<                class V> inline ::set   <remove_const_t<VT(V)            >                   > mk_set   (V const& v) { return            { v.begin() , v.end() } ; }
template<                class V> inline ::uset  <remove_const_t<VT(V)            >                   > mk_uset  (V const& v) { return            { v.begin() , v.end() } ; }
template<                class V> inline ::vector<                                  VT(V)             > mk_vector(V const& v) { return            { v.begin() , v.end() } ; }
template<                class V> inline ::map   <remove_const_t<VT(V)::first_type>,VT(V)::second_type> mk_map   (V const& v) { return            { v.begin() , v.end() } ; }
template<                class V> inline ::umap  <remove_const_t<VT(V)::first_type>,VT(V)::second_type> mk_umap  (V const& v) { return            { v.begin() , v.end() } ; }
template<                class V> inline ::vmap  <remove_const_t<VT(V)::first_type>,VT(V)::second_type> mk_vmap  (V const& v) { return            { v.begin() , v.end() } ; }

// keys & vals
template<class K,class M> inline ::set   <K> const mk_key_set   (M const& m) { ::set   <K> res ;                         for( auto const& [k,v] : m) res.insert   (k) ; return res ; }
template<class K,class M> inline ::uset  <K> const mk_key_uset  (M const& m) { ::uset  <K> res ;                         for( auto const& [k,v] : m) res.insert   (k) ; return res ; }
template<class K,class M> inline ::vector<K> const mk_key_vector(M const& m) { ::vector<K> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m) res.push_back(k) ; return res ; }
template<class T,class M> inline ::set   <T>       mk_val_set   (M const& m) { ::set   <T> res ;                         for( auto const& [k,v] : m) res.insert   (v) ; return res ; }
template<class T,class M> inline ::uset  <T>       mk_val_uset  (M const& m) { ::uset  <T> res ;                         for( auto const& [k,v] : m) res.insert   (v) ; return res ; }
template<class T,class M> inline ::vector<T>       mk_val_vector(M const& m) { ::vector<T> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m) res.push_back(v) ; return res ; }
// with implicit item type
template<class M> inline ::set   <remove_const_t<VT(M)::first_type >> const mk_key_set   (M const& m) { return mk_key_set   <remove_const_t<VT(M)::first_type >>(m) ; }
template<class M> inline ::uset  <remove_const_t<VT(M)::first_type >> const mk_key_uset  (M const& m) { return mk_key_uset  <remove_const_t<VT(M)::first_type >>(m) ; }
template<class M> inline ::vector<remove_const_t<VT(M)::first_type >> const mk_key_vector(M const& m) { return mk_key_vector<remove_const_t<VT(M)::first_type >>(m) ; }
template<class M> inline ::set   <               VT(M)::second_type >       mk_val_set   (M const& m) { return mk_val_set   <               VT(M)::second_type >(m) ; }
template<class M> inline ::uset  <               VT(M)::second_type >       mk_val_uset  (M const& m) { return mk_val_uset  <               VT(M)::second_type >(m) ; }
template<class M> inline ::vector<               VT(M)::second_type >       mk_val_vector(M const& m) { return mk_val_vector<               VT(M)::second_type >(m) ; }

// support container arg to standard utility functions
using std::sort          ;                              // keep std definitions
using std::stable_sort   ;                              // .
using std::binary_search ;                              // .
using std::lower_bound   ;                              // .
using std::min           ;                              // .
using std::max           ;                              // .
#define CMP ::function<bool(VT(T) const&,VT(T) const&)>
template<class T> inline          void              sort         ( T      & x ,                  CMP cmp ) {         ::sort         ( x.begin() , x.end() ,     cmp ) ; }
template<class T> inline          void              stable_sort  ( T      & x ,                  CMP cmp ) {         ::stable_sort  ( x.begin() , x.end() ,     cmp ) ; }
template<class T> inline          bool              binary_search( T const& x , VT(T) const& v , CMP cmp ) { return  ::binary_search( x.begin() , x.end() , v , cmp ) ; }
template<class T> inline typename T::const_iterator lower_bound  ( T const& x , VT(T) const& v , CMP cmp ) { return  ::lower_bound  ( x.begin() , x.end() , v , cmp ) ; }
template<class T> inline          void              sort         ( T      & x                            ) {         ::sort         ( x.begin() , x.end()           ) ; }
template<class T> inline          void              stable_sort  ( T      & x                            ) {         ::stable_sort  ( x.begin() , x.end()           ) ; }
template<class T> inline          bool              binary_search( T const& x , VT(T) const& v           ) { return  ::binary_search( x.begin() , x.end() , v       ) ; }
template<class T> inline typename T::const_iterator lower_bound  ( T const& x , VT(T) const& v           ) { return  ::lower_bound  ( x.begin() , x.end() , v       ) ; }
#undef CMP

#undef TVT

template<class T> inline T& grow( ::vector<T>& v , size_t i ) {
	if(i>=v.size()) v.resize(i+1) ;
	return v[i] ;
}

//
// assert
//

struct Fd ;

extern thread_local char t_thread_key ;

void kill_self      ( int sig                ) ;
void write_backtrace( Fd      , int hide_cnt ) ;

template<void (*Handler)(int sig,void* addr)> inline void _sig_action( int sig , siginfo_t* si , void* ) {
	Handler(sig,si->si_addr) ;
}
template<void (*Handler)(int sig,void* addr)> void set_sig_handler(int sig) {
	sigset_t         empty  ;      sigemptyset(&empty) ;                      // sigemptyset can be a macro
	struct sigaction action = {} ;
	action.sa_sigaction = _sig_action<Handler>  ;
	action.sa_mask      = empty                 ;
	action.sa_flags     = SA_RESTART|SA_SIGINFO ;
	::sigaction( sig , &action , nullptr ) ;
}
template<void (*Handler)(int sig)> void set_sig_handler(int sig) {
	sigset_t         empty  ;      sigemptyset(&empty) ;                      // sigemptyset can be a macro
	struct sigaction action = {} ;
	action.sa_handler = Handler    ;
	action.sa_mask    = empty      ;
	action.sa_flags   = SA_RESTART ;
	::sigaction( sig , &action , nullptr ) ;
}

template<class... A> [[noreturn]] void crash( int hide_cnt , int sig , A const&... args ) ;

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

template<class... A> [[noreturn]] inline void fail( A const&... args [[maybe_unused]] ) {
	#ifndef NDEBUG
		crash( 1 , SIGABRT , "fail" , args... ) ;
	#else
		unreachable() ;
	#endif
}

template<class... A> inline constexpr void swear( bool cond , A const&... args [[maybe_unused]] ) {
	#ifndef NDEBUG
		if (!cond) crash( 1 , SIGABRT , "assertion violation" , args... ) ;
	#else
		if (!cond) unreachable() ;
	#endif
}

template<class... A> [[noreturn]] inline void fail_prod( A const&... args ) {
	crash( 1 , SIGABRT , "fail" , args... ) ;
}

template<class... A> constexpr inline void swear_prod( bool cond , A const&... args ) {
	if (!cond) crash( 1 , SIGABRT , "assertion violation" , args... ) ;
}

#define _FAIL_STR2(x) #x
#define _FAIL_STR(x) _FAIL_STR2(x)
#define FAIL(           ...) fail      (       "@" __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__            __VA_OPT__(,": " #__VA_ARGS__ " =",)__VA_ARGS__)
#define FAIL_PROD(      ...) fail_prod (       "@" __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__            __VA_OPT__(,": " #__VA_ARGS__ " =",)__VA_ARGS__)
#define SWEAR(     cond,...) swear     ((cond),"@" __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__,": " #cond __VA_OPT__(" : " #__VA_ARGS__ " =",)__VA_ARGS__)
#define SWEAR_PROD(cond,...) swear_prod((cond),"@" __FILE__ ":" _FAIL_STR(__LINE__) " in",__PRETTY_FUNCTION__,": " #cond __VA_OPT__(" : " #__VA_ARGS__ " =",)__VA_ARGS__)

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
	SWEAR(rc==0,sig) ;                // killing outselves should always be ok
}

//
// iota
//

template<class T> concept Iotable = ::is_integral_v<T>||::is_enum_v<T> ;
template<bool WithStart,Iotable I> struct Iota {
	struct Iterator {
		constexpr Iterator(I c) : cur{c} {}
		// services
		constexpr bool      operator==(Iterator const&) const = default ;
		constexpr I         operator* (               ) const {                                return cur   ; }
		constexpr Iterator& operator++(               )       { cur++ ;                        return self  ; }
		constexpr Iterator  operator++(int            )       { Iterator self_=self ; ++self ; return self_ ; }
		// data
		I cur = {} ;
	} ;
	// cxtors & casts
	/**/               constexpr Iota(        I e ) requires(!WithStart) : bounds{  e} {}
	template<class I1> constexpr Iota( I1 b , I e ) requires( WithStart) : bounds{b,e} {}
	// services
	constexpr Iterator begin() const { return Iterator(WithStart ? bounds[0] : I(0))          ; }
	constexpr Iterator end  () const { return Iterator(bounds[WithStart]           )          ; }
	constexpr size_t   size () const { return +bounds[WithStart] - +(WithStart?bounds[0]:I()) ; }
	// data
	I bounds[1+WithStart] = {} ;
} ;

template<           Iotable I > inline constexpr Iota<false/*with_start*/,I > iota(            I  end ) {                                    return {   end} ; }
template<Iotable I1,Iotable I2> inline constexpr Iota<true /*with_start*/,I2> iota( I1 begin , I2 end ) { I2 b2=I2(begin) ; SWEAR(b2<=end) ; return {b2,end} ; }

//
// string
//

inline ::string_view substr_view( ::string const& s , size_t start , size_t len=Npos ) {
	SWEAR(start<=s.size()) ;
	return { s.data()+start , ::min(len,s.size()-start) } ;
}

template<::unsigned_integral I> ::string to_hex( I v , uint8_t width=sizeof(I)*2 ) {
	::string res ( width , '0' ) ;
	for( uint8_t i : iota(width) ) {
		uint8_t d = v%16 ;
		res[width-1-i] = d<10 ? '0'+d : 'a'+d-10 ;
		v >>= 4 ;
		if (!v) break ;
	}
	SWEAR(!v,v,res) ;
	return res ;
}

struct First {
	bool operator()() { uint8_t v = _val ; _val = ::min(_val+1,2) ; return v==0 ; }
	//
	template<class T> T operator()( T&& first , T&& other=T() ) { return self() ? ::forward<T>(first) : ::forward<T>(other) ; }
	//
	template<class T> T operator()( T&& first , T&& second , T&& other ) {
		uint8_t v = _val ;
		self() ;
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

namespace std {                                                                        // must be defined in std or operator! does not recognize it
	inline                   bool operator+(::string const& s) { return !s.empty() ; } // empty() is not constexpr in C++20
	inline                   bool operator+(::string_view   s) { return !s.empty() ; } // .
	template<class T> inline bool operator+(::span<T>       v) { return !v.empty() ; } // .
}

namespace std {
	template<class F> concept _CanDoFunc    = requires(::string s,F* f) { f(s) ; }              ;
	template<class T> concept _CanAdd       = requires(::string s,T  x) { s+=x ; }              ;
	template<class N> concept _CanDoToChars = ::is_arithmetic_v<N> && !IsOneOf<N,char,bool>     ;
	template<class T> concept _CanDoToHex   = !::is_same_v<::decay_t<T>,char> && !_CanDoFunc<T> ;
	template<class T> concept _CanDoBool    = ::is_same_v<::decay_t<T>,bool>                    ;                // use a template to avoid having to high a priority when compiler selects an overload
	#if __cplusplus<202600L
		inline ::string  operator+ ( ::string     && s , ::string_view   v ) {                    s.append(  v.data(),v.size()) ; return ::move(s) ; }
		inline ::string  operator+ ( ::string_view   v , ::string     && s ) {                    s.insert(0,v.data(),v.size()) ; return ::move(s) ; }
		inline ::string  operator+ ( ::string const& s , ::string_view   v ) { ::string r = s   ; r.append(  v.data(),v.size()) ; return        r  ; }
		inline ::string  operator+ ( ::string_view   v , ::string const& s ) { ::string r { v } ; r.append(  s.data(),s.size()) ; return        r  ; }
		inline ::string& operator+=( ::string      & s , ::string_view   v ) {                    s.append(  v.data(),v.size()) ; return        s  ; }
	#endif
	inline ::string  operator+ ( ::string     && s , nullptr_t         ) { return ::move(s       ) +  "(null)" ; }
	inline ::string  operator+ ( ::string const& s , nullptr_t         ) { return        s         +  "(null)" ; }
	inline ::string  operator+ ( nullptr_t         , ::string const& s ) { return        "(null)"  +   s       ; }
	inline ::string& operator+=( ::string      & s , nullptr_t         ) { return        s         += "(null)" ; }
	//
	template<_CanDoBool B> inline ::string  operator+ ( ::string     && s , B               b ) { return ::move(s               ) +  (b?"true":"false") ; }
	template<_CanDoBool B> inline ::string  operator+ ( ::string const& s , B               b ) { return        s                 +  (b?"true":"false") ; }
	template<_CanDoBool B> inline ::string  operator+ ( B               b , ::string const& s ) { return       (b?"true":"false") +   s                 ; }
	template<_CanDoBool B> inline ::string& operator+=( ::string      & s , B               b ) { return        s                 +=  b?"true":"false"  ; }
	//
	template<_CanDoToHex T> ::string _ptr_to_hex(T* p) {
		if (p) return "0x"+to_hex(reinterpret_cast<uintptr_t>(p)) ;
		else   return "(null)"                                    ;
	}
	template<_CanDoToHex T> inline ::string  operator+ ( ::string     && s , T*              p ) { return ::move     (s) +  _ptr_to_hex(p) ; }
	template<_CanDoToHex T> inline ::string  operator+ ( ::string const& s , T*              p ) { return             s  +  _ptr_to_hex(p) ; }
	template<_CanDoToHex T> inline ::string  operator+ ( T*              p , ::string const& s ) { return _ptr_to_hex(p) +              s  ; }
	template<_CanDoToHex T> inline ::string& operator+=( ::string      & s , T*              p ) { return             s  += _ptr_to_hex(p) ; }
	//
	template<_CanDoToChars N> inline ::string _to_string_append(N n) {
		::string res ( 30 , 0 ) ;
		::to_chars_result rc = ::to_chars( res.data() , res.data()+res.size() , n ) ; SWEAR(rc.ec==::errc()) ;
		res.resize(rc.ptr-res.data()) ;
		return res ;
	}
	template<_CanDoToChars N> inline ::string  operator+ ( ::string     && s , N               n ) { return ::move           (s) +  _to_string_append(n) ; }
	template<_CanDoToChars N> inline ::string  operator+ ( ::string const& s , N               n ) { return                   s  +  _to_string_append(n) ; }
	template<_CanDoToChars N> inline ::string  operator+ ( N               n , ::string const& s ) { return _to_string_append(n) +                    s  ; }
	template<_CanDoToChars N> inline ::string& operator+=( ::string      & s , N               n ) { return                   s  += _to_string_append(n) ; }
	//
	template<_CanDoFunc F> inline ::string& operator+=( ::string& s , F*                 f ) { f(s)          ; return s ; }
	template<_CanAdd    T> inline ::string& operator+=( ::string& s , ::atomic<T> const& x ) { s += x.load() ; return s ; }
	//
	template<_CanAdd T> inline ::string& operator<<( ::string& s , T&& x ) { s += ::forward<T>(x) ; return s ; } // work around += right associativity
}

inline ::string widen( ::string && s , size_t sz , bool right=false , char fill=' ' ) {
	if (s.size()>=sz) return ::move(s)   ;
	::string f ( sz-s.size() , fill ) ;
	if (right       ) return ::move(f)+s ;
	/**/              return ::move(s)+f ;
}
inline ::string widen( ::string const& s , size_t sz , bool right=false , char fill=' ' ) {
	if (s.size()>=sz) return        s    ;
	::string f ( sz-s.size() , fill ) ;
	if (right       ) return ::move(f)+s ;
	/**/              return        s +f ;
}

template<class... A> inline ::string cat(A&&... args) {
	::string res ;
	[[maybe_unused]] bool _[] = { false , (res+=args,false)... } ;
	return res ;
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
template<::integral I> inline I from_string( const char* txt , bool empty_ok=false , bool hex=false ) { return from_string<I>( ::string_view(txt) , empty_ok , hex ) ; }
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
template<::floating_point F> inline F from_string( const char* txt , bool empty_ok=false ) { return from_string<F>( ::string_view(txt) , empty_ok ) ; }

/**/   ::string mk_json_str (::string_view  ) ;
/**/   ::string mk_shell_str(::string_view  ) ;
/**/   ::string mk_py_str   (::string_view  ) ;
inline ::string mk_py_str   (const char*   s) { return mk_py_str(::string_view(s)) ; }
inline ::string mk_py_str   (bool          b) { return b ? "True" : "False"        ; }

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

template<char Delimiter=0>        ::string mk_printable(::string const&    ) ;
template<char Delimiter=0> inline ::string mk_printable(::string     && txt) {
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
	::string res ;                   res.reserve(s.size()+N*(s.size()>>4)) ;         // anticipate lines of size 16, this is a reasonable pessimistic guess (as overflow is expensive)
	bool     sol = true            ;
	::string pfx = ::string(i*N,C) ;
	for( char c : s ) {
		if (sol) res += pfx     ;
		/**/     res += c       ;
		/**/     sol  = c=='\n' ;
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
inline ::vector_s split(::string_view txt) {
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
inline ::vector_s split( ::string_view txt , char sep , size_t n_sep=Npos ) {
	::vector_s res ;
	size_t     pos = 0 ;
	for( [[maybe_unused]] size_t _ : iota(n_sep) ) {
		size_t end = txt.find(sep,pos) ;
		res.emplace_back( txt.substr(pos,end-pos) ) ;
		if (end==Npos) return res ;                   // we have exhausted all sep's
		pos = end+1 ;                                 // after the sep
	}
	res.emplace_back(txt.substr(pos)) ;               // all the remaining as last component after n_sep sep's
	return res ;
}

inline ::string_view first_lines( ::string_view txt , size_t n_sep , char sep='\n' ) {
	size_t pos = -1 ;
	for( [[maybe_unused]] size_t _ : iota(n_sep) ) {
		pos = txt.find(sep,pos+1) ;
		if (pos==Npos) return txt ;
	}
	return txt.substr(0,pos+1) ;
}

template<::integral I> inline I decode_int(const char* p) {
	I r = 0 ;
	for( uint8_t i : iota<uint8_t>(sizeof(I)) ) r |= I(uint8_t(p[i]))<<(i*8) ; // little endian, /!\ : beware of signs, casts & integer promotion
	return r ;
}

template<::integral I> inline void encode_int( char* p , I x ) {
	for( uint8_t i : iota<uint8_t>(sizeof(I)) ) p[i] = char(x>>(i*8)) ; // little endian
}

::string glb_subst( ::string&& txt , ::string const& sub , ::string const& repl ) ;

template<char U,::integral I=size_t>        I        from_string_with_unit(::string const& s) ;                                          // provide default unit in U. ...
template<char U,::integral I=size_t>        ::string to_string_with_unit  (I               x) ;                                          // ... If provided, return value is expressed in this unit
template<       ::integral I=size_t> inline I        from_string_with_unit(::string const& s) { return from_string_with_unit<0,I>(s) ; }
template<       ::integral I=size_t> inline ::string to_string_with_unit  (I               x) { return to_string_with_unit  <0,I>(x) ; }

template<class... A> inline constexpr void throw_if    ( bool cond , A const&... args ) { if ( cond) throw cat(args...) ; }
template<class... A> inline constexpr void throw_unless( bool cond , A const&... args ) { if (!cond) throw cat(args...) ; }

//
// span
//

using span_s = ::span<::string> ;

//
// math
//

constexpr inline uint8_t n_bits(size_t n) { return NBits<size_t>-::countl_zero(n-1) ; } // number of bits to store n states

template<::integral T=size_t> constexpr inline T lsb_msk (uint8_t b) { return  (T(1)<<b)-1 ; }
template<::integral T=size_t> constexpr inline T msb_msk (uint8_t b) { return -(T(1)<<b)   ; }

template<size_t D,class N> inline constexpr N round_down(N n) { return n - n%D              ; }
template<size_t D,class N> inline constexpr N round_up  (N n) { return round_down<D>(n+D-1) ; }
template<size_t D,class N> inline constexpr N div_up    (N n) { return (n+D-1)/D            ; }

static constexpr double Infinity = ::numeric_limits<double>::infinity () ;
static constexpr double Nan      = ::numeric_limits<double>::quiet_NaN() ;

//
// string formatting
//

namespace std {

	#define IOP(...) inline ::string& operator+=( ::string& os , __VA_ARGS__ )
	template<class T,size_t N> IOP(          T    const  a[N] ) { First f ; os <<'[' ; for( T    const&  x    : a ) { os<<f("",",")<<x         ; } return os <<']' ; }
	template<class T,size_t N> IOP( ::array <T,N> const& a    ) { First f ; os <<'[' ; for( T    const&  x    : a ) { os<<f("",",")<<x         ; } return os <<']' ; }
	template<class T         > IOP( ::vector<T  > const& v    ) { First f ; os <<'[' ; for( T    const&  x    : v ) { os<<f("",",")<<x         ; } return os <<']' ; }
	template<class T         > IOP( ::span  <T  > const& v    ) { First f ; os <<'[' ; for( T    const&  x    : v ) { os<<f("",",")<<x         ; } return os <<']' ; }
	template<class K         > IOP( ::uset  <K  > const& s    ) { First f ; os <<'{' ; for( K    const&  k    : s ) { os<<f("",",")<<k         ; } return os <<'}' ; }
	template<class K         > IOP( ::set   <K  > const& s    ) { First f ; os <<'{' ; for( K    const&  k    : s ) { os<<f("",",")<<k         ; } return os <<'}' ; }
	template<class K,class V > IOP( ::umap  <K,V> const& m    ) { First f ; os <<'{' ; for( auto const& [k,v] : m ) { os<<f("",",")<<k<<':'<<v ; } return os <<'}' ; }
	template<class K,class V > IOP( ::map   <K,V> const& m    ) { First f ; os <<'{' ; for( auto const& [k,v] : m ) { os<<f("",",")<<k<<':'<<v ; } return os <<'}' ; }
	//
	template<class A,class B > IOP( ::pair<A,B> const& p ) { return os <<'('<< p.first <<','<< p.second <<')' ; }
	/**/                       IOP( uint8_t     const  i ) { return os << uint32_t(i)                         ; } // avoid output a char when actually a int
	/**/                       IOP( int8_t      const  i ) { return os << int32_t (i)                         ; } // .
	#undef IOP

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
	::array<char,Sz*2> res   {}           ;
	char*              q     = res.data() ;
	bool               first = true       ;
	for( char c : camel0 ) {
		if ( 'A'<=c && c<='Z' ) { { if (!first) *q++ = '_' ; } *q++ = 'a'+(c-'A') ; }
		else                                                   *q++ =      c      ;
		first = !c ;
	}
	return res ;
}

template<size_t Sz,size_t VSz> constexpr ::array<string_view,Sz> _enum_mk_tab(::array<char,VSz> const& vals) {
	::array<string_view,Sz> res  {}            ;
	const char*             item = vals.data() ;
	for( size_t i : iota(Sz) ) {
		size_t len = 0 ; while (item[len]) len++ ;
		res[i] = {item,len} ;
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstringop-overread" // seems to be a gcc bug as even protecting last iteration is not enough
		item += len+1 ;                                      // point to the start of the next one (not dereferenced at last iteration)
		#pragma GCC diagnostic pop
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

template<StdEnum E> inline ::string_view camel     (E e) { return          EnumCamels<E>[+e]        ; }
template<StdEnum E> inline ::string_view snake     (E e) { return          EnumSnakes<E>[+e]        ; }
template<StdEnum E> inline ::string      camel_str (E e) { return ::string(EnumCamels<E>[+e])       ; }
template<StdEnum E> inline ::string      snake_str (E e) { return ::string(EnumSnakes<E>[+e])       ; }
template<StdEnum E> inline const char*   camel_cstr(E e) { return          EnumCamels<E>[+e].data() ; } // string_view's in this table have a terminating null
template<StdEnum E> inline const char*   snake_cstr(E e) { return          EnumSnakes<E>[+e].data() ; } // .

namespace std {
	template<StdEnum E> inline ::string  operator+ ( ::string     && s , E               e ) { return ::move(s)+snake(e)                          ; }
	template<StdEnum E> inline ::string  operator+ ( ::string const& s , E               e ) { return        s +snake(e)                          ; }
	template<StdEnum E> inline ::string  operator+ ( E               e , ::string const& s ) { return snake (e)+      s                           ; }
	template<StdEnum E> inline ::string& operator+=( ::string      & s , E               e ) { return e<All<E> ? s<<snake(e) : s<<"N+"<<(+e-N<E>) ; }
}

template<StdEnum E> ::umap_s<E> _mk_enum_tab() {
	::umap_s<E> res ;
	for( E e : iota(All<E>) ) {
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

template<StdEnum E> inline bool can_mk_enum(::string const& x) {
	return _mk_enum<E>(x).second ;
}

template<StdEnum E> inline E mk_enum(::string const& x) {
	::pair<E,bool/*ok*/> res = _mk_enum<E>(x) ;
	throw_unless( res.second , "cannot make enum ",EnumName<E>," from ",x ) ;
	return res.first ;
}

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

template<StdEnum E> inline constexpr EnumUint<E> operator+(E e) { return EnumUint<E>(e) ; }
//
template<StdEnum E> inline constexpr E          operator+ (E  e,EnumInt<E> i) {                e = E(+e+ i) ; return e  ; }
template<StdEnum E> inline constexpr E&         operator+=(E& e,EnumInt<E> i) {                e = E(+e+ i) ; return e  ; }
template<StdEnum E> inline constexpr E          operator- (E  e,EnumInt<E> i) {                e = E(+e- i) ; return e  ; }
template<StdEnum E> inline constexpr EnumInt<E> operator- (E  e,E          o) { EnumInt<E> d ; d =   +e-+o  ; return d  ; }
template<StdEnum E> inline constexpr E&         operator-=(E& e,EnumInt<E> i) {                e = E(+e- i) ; return e  ; }
template<StdEnum E> inline constexpr E          operator++(E& e             ) {                e = E(+e+ 1) ; return e  ; }
template<StdEnum E> inline constexpr E          operator++(E& e,int         ) { E e_ = e ;     e = E(+e+ 1) ; return e_ ; }
template<StdEnum E> inline constexpr E          operator--(E& e             ) {                e = E(+e- 1) ; return e  ; }
template<StdEnum E> inline constexpr E          operator--(E& e,int         ) { E e_ = e ;     e = E(+e- 1) ; return e_ ; }
//
template<StdEnum E> inline constexpr E  operator& (E  e,E o) {           return ::min(e,o) ; }
template<StdEnum E> inline constexpr E  operator| (E  e,E o) {           return ::max(e,o) ; }
template<StdEnum E> inline constexpr E& operator&=(E& e,E o) { e = e&o ; return e          ; }
template<StdEnum E> inline constexpr E& operator|=(E& e,E o) { e = e|o ; return e          ; }
//
template<StdEnum E> inline E    decode_enum( const char* p ) { return E(decode_int<EnumUint<E>>(p)) ; }
template<StdEnum E> inline void encode_enum( char* p , E e ) { encode_int(p,+e) ;                     }

template<StdEnum E> struct BitMap {
	template<StdEnum> friend ::string& operator+=( ::string& , BitMap const ) ;
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
	// accesses
	constexpr Val operator+() const { return _val ; }
	// services
	constexpr bool    operator==( BitMap const&     ) const = default ;
	constexpr bool    operator<=( BitMap other      ) const { return !(  _val & ~other._val )    ;                 }
	constexpr bool    operator>=( BitMap other      ) const { return !( ~_val &  other._val )    ;                 }
	constexpr BitMap  operator~ (                   ) const { return BitMap(lsb_msk(N<E>)&~_val) ;                 }
	constexpr BitMap  operator& ( BitMap other      ) const { return BitMap(_val&other._val)     ;                 }
	constexpr BitMap  operator| ( BitMap other      ) const { return BitMap(_val|other._val)     ;                 }
	constexpr BitMap& operator&=( BitMap other      )       { self = self&other ; return self    ;                 }
	constexpr BitMap& operator|=( BitMap other      )       { self = self|other ; return self    ;                 }
	constexpr bool    operator[]( E      bit_       ) const { return (_val>>+bit_) & Val(1)      ;                 }
	constexpr uint8_t popcount  (                   ) const { return ::popcount(_val)            ;                 }
	constexpr void    set       ( E flag , bool val )       { if (val) self |= flag ; else self &= ~BitMap(flag) ; } // operator~(E) is not always recognized because of namespace's
	// data
private :
	Val _val = 0 ;
} ;
//
template<StdEnum E> inline constexpr BitMap<E> operator~(E e) { return ~BitMap<E>(e)  ; }

template<StdEnum E>inline  BitMap<E> mk_bitmap( ::string const& x , char sep=',' ) {
	BitMap<E> res ;
	for( ::string const& s : split(x,sep) ) res |= mk_enum<E>(s) ;
	return res ;
}

template<StdEnum E> inline ::string& operator+=( ::string& os , BitMap<E> const bm ) {
	os <<'(' ;
	bool first = true ;
	for( E e : iota(All<E>) ) if (bm[e]) {
		if (first) { os <<      e ; first = false ; }
		else       { os <<'|'<< e ;                 }
	}
	return os <<')' ;
}

// used in static_assert when defining a table indexed by enum to fire if enum updates are not propagated to tab def
template<StdEnum E,class T> inline constexpr bool chk_enum_tab(::amap<E,T,N<E>> tab) {
	for( E e : iota(All<E>) ) if (tab[+e].first!=e) return false/*ok*/ ;
	/**/                                            return true /*ok*/ ;
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
inline Bool3  operator~ ( Bool3  b             ) {                return Bool3(+Yes-+b)                                                      ; }
inline Bool3  operator| ( Bool3  b1 , bool  b2 ) {                return  b2 ? Yes : b1                                                      ; }
inline Bool3  operator& ( Bool3  b1 , bool  b2 ) {                return !b2 ? No  : b1                                                      ; }
inline Bool3  operator| ( bool   b1 , Bool3 b2 ) {                return  b1 ? Yes : b2                                                      ; }
inline Bool3  operator& ( bool   b1 , Bool3 b2 ) {                return !b1 ? No  : b2                                                      ; }
inline Bool3& operator|=( Bool3& b1 , bool  b2 ) { b1 = b1 | b2 ; return b1                                                                  ; }
inline Bool3& operator&=( Bool3& b1 , bool  b2 ) { b1 = b1 & b2 ; return b1                                                                  ; }
inline Bool3  common    ( Bool3  b1 , Bool3 b2 ) {                return b1==Yes ? (b2==Yes?Yes:Maybe) : b1==No ? ( b2==No?No:Maybe) : Maybe ; }
inline Bool3  common    ( Bool3  b1 , bool  b2 ) {                return b2      ? (b1==Yes?Yes:Maybe) :          ( b1==No?No:Maybe)         ; }
inline Bool3  common    ( bool   b1 , Bool3 b2 ) {                return b1      ? (b2==Yes?Yes:Maybe) :          ( b2==No?No:Maybe)         ; }
inline Bool3  common    ( bool   b1 , bool  b2 ) {                return b1      ? (b2     ?Yes:Maybe) :          (!b2    ?No:Maybe)         ; }

//
// Fd
// necessary here in utils.hh, so cannot be put in higher level include such as fd.hh
//

inline ::string with_slash(::string&& path) {
	if (!path           ) return "/"          ;
	if (path=="."       ) return {}           ;
	if (path.back()!='/') path += '/'         ;
	/**/                  return ::move(path) ;
}
inline ::string no_slash(::string&& path) {
	if ( !path                              ) return "."          ;
	if ( path.back()=='/' && path.size()!=1 ) path.pop_back()     ;                 // special case '/' as this is the usual convention : no / at the end of dirs, except for /
	/**/                                      return ::move(path) ;
}
inline ::string with_slash(::string const& path) {
	if (!path           ) return "/"      ;
	if (path=="."       ) return {}       ;
	if (path.back()!='/') return path+'/' ;
	/**/                  return path     ;
}
inline ::string no_slash(::string const& path) {
	if ( !path                              ) return "."                          ;
	if ( path.back()=='/' && path.size()!=1 ) return path.substr(0,path.size()-1) ; // special case '/' as this is the usual convention : no / at the end of dirs, except for /
	/**/                                      return path                         ;
}

ENUM( FdAction
,	Read
,	Write
,	Append
,	CreateReadOnly
,	Dir
)
struct Fd {
	friend ::string& operator+=( ::string& , Fd const& ) ;
	static const Fd Cwd    ;
	static const Fd Stdin  ;
	static const Fd Stdout ;
	static const Fd Stderr ;
	static const Fd Std    ;                                                                              // the highest standard fd
	static constexpr FdAction Read           = FdAction::Read           ;
	static constexpr FdAction Write          = FdAction::Write          ;
	static constexpr FdAction Append         = FdAction::Append         ;
	static constexpr FdAction CreateReadOnly = FdAction::CreateReadOnly ;
	static constexpr FdAction Dir            = FdAction::Dir            ;
	// cxtors & casts
	constexpr Fd(                        ) = default ;
	constexpr Fd( int fd_                ) : fd{fd_} {                         }
	/**/      Fd( int fd_ , bool no_std_ ) : fd{fd_} { if (no_std_) no_std() ; }
	//
	Fd(         ::string const& file , FdAction action=Read , bool no_std_=false ) : Fd{ Cwd , file , action , no_std_ } {}
	Fd( Fd at , ::string const& file , FdAction action=Read , bool no_std_=false ) :
		Fd{
			::openat(
				at , action==Dir&&file!="/" ? no_slash(file).c_str() : file.c_str()
			,		action==Read           ? O_RDONLY                      | O_CLOEXEC
				:	action==Write          ? O_WRONLY | O_CREAT | O_TRUNC  | O_CLOEXEC
				:	action==Append         ? O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC
				:	action==CreateReadOnly ? O_WRONLY | O_CREAT | O_TRUNC  | O_CLOEXEC
				:	action==Dir            ? O_RDONLY | O_DIRECTORY        | O_CLOEXEC
				:	0                                                                                     // force error
			,	action==CreateReadOnly ? 0444 : 0666
			)
		,	no_std_
		}
	{}
	//
	constexpr operator int  () const { return fd    ; }
	constexpr bool operator+() const { return fd>=0 ; }
	//
	void swap(Fd& fd_) { ::swap(fd,fd_.fd) ; }
	// services
	/**/      bool              operator== ( Fd const&                                ) const = default ;
	/**/      ::strong_ordering operator<=>( Fd const&                                ) const = default ;
	/**/      void              write      ( ::string_view data                       ) const ;           // writing does not modify the Fd object
	/**/      ::string          read       ( size_t sz=Npos   , bool no_file_ok=false ) const ;           // read sz bytes or to eof
	/**/      ::vector_s        read_lines (                    bool no_file_ok=false ) const ;
	/**/      size_t            read_to    ( ::span<char> dst , bool no_file_ok=false ) const ;
	/**/      Fd                dup        (                                          ) const { return ::dup(fd) ;                     }
	constexpr Fd                detach     (                                          )       { Fd res = self ; fd = -1 ; return res ; }
	constexpr void              close      (                                          ) ;
	/**/      void              no_std     (                                          ) ;
	/**/      void              cloexec    (bool set=true                             ) const { ::fcntl(fd,F_SETFD,set?FD_CLOEXEC:0) ; }
	// data
	int fd = -1 ;
} ;
constexpr Fd Fd::Cwd   {int(AT_FDCWD)} ;
constexpr Fd Fd::Stdin {0            } ;
constexpr Fd Fd::Stdout{1            } ;
constexpr Fd Fd::Stderr{2            } ;
constexpr Fd Fd::Std   {2            } ;

struct AcFd : Fd {
	friend ::string& operator+=( ::string& , AcFd const& ) ;
	// cxtors & casts
	AcFd(                                                                          ) = default ;
	AcFd( Fd fd_                                                                   ) : Fd{fd_                            } {              }
	AcFd( AcFd&& acfd                                                              )                                       { swap(acfd) ; }
	AcFd( int fd_                                             , bool no_std_=false ) : Fd{fd_,no_std_                    } {              }
	AcFd(         ::string const& file , FdAction action=Read , bool no_std_=false ) : Fd{       file , action , no_std_ } {              }
	AcFd( Fd at , ::string const& file , FdAction action=Read , bool no_std_=false ) : Fd{ at  , file , action , no_std_ } {              }
	//
	~AcFd() { close() ; }
	//
	AcFd& operator=(int       fd_ ) { if (fd!=fd_) { close() ; fd = fd_ ; } return self ; }
	AcFd& operator=(Fd const& fd_ ) { self = fd_ .fd ;                      return self ; }
	AcFd& operator=(AcFd   && acfd) { swap(acfd) ;                          return self ; }
} ;

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
,	Workload
// very inner
,	Trace       // allow tracing anywhere (but tracing may call some syscall)
,	SyscallTab  // any syscall may need this mutex, which may occur during tracing
,	PdateNew    // may need time anywhere, even during syscall processing
)

extern thread_local MutexLvl t_mutex_lvl ;
template<MutexLvl Lvl_,bool S=false/*shared*/> struct Mutex : ::conditional_t<S,::shared_timed_mutex,::timed_mutex> {
	using Base =                                              ::conditional_t<S,::shared_timed_mutex,::timed_mutex> ;
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

template<class M,bool S=false> struct Lock {
	// cxtors & casts
	Lock (                          ) = default ;
	Lock ( Lock&& l                 )              { self = ::move(l) ;      }
	Lock ( M& m , bool do_lock=true ) : _mutex{&m} { if (do_lock) lock  () ; }
	~Lock(                          )              { if (_locked) unlock() ; }
	Lock& operator=(Lock&& l) {
		if (_locked) unlock() ;
		_mutex    = l._mutex  ;
		_lvl      = l._lvl    ;
		_locked   = l._locked ;
		l._locked = false     ;
		return self ;
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
			throw_unless( n_allocated< Max<T> , "cannot allocate id" ) ;
			n_allocated++ ;
		} else {
			res = *free_ids.begin() ;
			free_ids.erase(res) ;
		}
		SWEAR(n_acquired<Max<T>) ;      // ensure no overflow
		n_acquired++ ;                  // protected by _mutex
		return res ;
	}
	void release(T id) {
		if (!id) return ;               // id 0 has not been acquired
		_Lock lock { _mutex } ;
		SWEAR(!free_ids.contains(id)) ; // else, double release
		free_ids.insert(id) ;
		SWEAR(n_acquired>Min<T>) ;      // ensure no underflow
		n_acquired-- ;                  // protected by _mutex
	}
	// data
	set<T>   free_ids    ;
	T        n_allocated = 1 ;          // dont use id 0 so that it is free to mean "no id"
	_AtomicT n_acquired  = 0 ;          // can be freely read by any thread if ThreadSafe
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
	 SaveInc(T& ref) : _ref{ref} { SWEAR(_ref<Max<T>) ; _ref++ ; } // increment
	~SaveInc(      )             { SWEAR(_ref>Min<T>) ; _ref-- ; } // restore
private :
	T& _ref ;
} ;

ENUM( Rc
,	Ok
,	Fail
,	Perm
,	Usage
,	Format
,	Param
,	System
)

template<class... As> [[noreturn]] inline void exit( Rc rc , As const&... args ) {
	Fd::Stderr.write(ensure_nl(cat(args...))) ;
	::std::exit(+rc) ;
}

template<class... As> void dbg( ::string const& title , As const&... args ) {
	::string              msg = title                                 ;
	[[maybe_unused]] bool _[] = { false , (msg<<' '<<args,false)... } ;
	msg += '\n' ;
	Fd::Stderr.write(msg) ;
}

template<::integral I> I random() {
	::string buf_char = AcFd("/dev/urandom").read(sizeof(I)) ; SWEAR(buf_char.size()==sizeof(I),buf_char.size()) ;
	I        buf_int  ;                                        ::memcpy( &buf_int , buf_char.data() , sizeof(I) ) ;
	return buf_int ;
}

//
// Implementation
//

//
// Fd
//

inline constexpr void Fd::close() {
	if (!self        ) return ;
	if (::close(fd)<0) throw "cannot close fd "s+fd+" : "+::strerror(errno) ;
	self = {} ;
}

inline void Fd::no_std() {
	if ( !self || fd>Std.fd ) return ;
	int new_fd = ::fcntl( fd , F_DUPFD_CLOEXEC , Std.fd+1 ) ;
	swear_prod(new_fd>Std.fd,"cannot duplicate",fd) ;
	close() ;
	fd = new_fd ;
}

//
// assert
//

::string get_exe() ;

extern bool _crash_busy ;
template<class... A> [[noreturn]] void crash( int hide_cnt , int sig , A const&... args ) {
	if (!_crash_busy) {                                                                     // avoid recursive call in case syscalls are highjacked (hoping sig handler management are not)
		_crash_busy = true ;
		::string err_msg = get_exe() ;
		if (t_thread_key!='?') err_msg <<':'<< t_thread_key ;
		/**/                   err_msg <<" :"               ;
		[[maybe_unused]] bool _[] = {false,(err_msg<<' '<<args,false)...} ;
		err_msg << '\n' ;
		Fd::Stderr.write(err_msg) ;
		set_sig_handler<SIG_DFL>(sig) ;
		write_backtrace(Fd::Stderr,hide_cnt+1) ;
		kill_self(sig) ;                                                                    // rather than merely calling abort, this works even if crash_handler is not installed
		// continue to abort in case kill did not work for some reason
	}
	set_sig_handler<SIG_DFL>(SIGABRT) ;
	::abort() ;
}

//
// string
//

inline constexpr bool is_word_char(char c) {
	if ( '0'<=c && c<='9' ) return true  ;
	if ( 'a'<=c && c<='z' ) return true  ;
	if ( 'A'<=c && c<='Z' ) return true  ;
	if ( c=='_'           ) return true  ;
	/**/                    return false ;
}

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
					char c1 = (c&0xf0)>>4 ;                         // /!\ c is signed
					char c2 =  c&0x0f     ;
					res += "\\x"                               ;
					res += char( c1>=10 ? 'a'+c1-10 : '0'+c1 ) ;
					res += char( c2>=10 ? 'a'+c2-10 : '0'+c2 ) ;
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
template<char U,::integral I> I from_string_with_unit(::string const& s) {
	using I64 = ::conditional_t<is_signed_v<I>,int64_t,uint64_t> ;
	I64                 val     = 0 /*garbage*/                   ;
	const char*         s_start = s.c_str()                       ;
	const char*         s_end   = s_start+s.size()                ;
	::from_chars_result fcr     = ::from_chars(s_start,s_end,val) ;
	//
	throw_unless( fcr.ec==::errc() , "unrecognized value "        ,s ) ;
	throw_unless( fcr.ptr>=s_end-1 , "partially recognized value ",s ) ;
	//
	static constexpr int8_t B = _unit_val(U       ) ;
	/**/             int8_t b = _unit_val(*fcr.ptr) ;
	//
	if (b<=B) {
		if (uint8_t(B-b)>=NBits<I>) {
			val = 0 ;
		} else {
			val >>= uint8_t(B-b) ;
			throw_unless( val<=Max<I> , "overflow"  ) ;
			throw_unless( val>=Min<I> , "underflow" ) ;
		}
	} else {
		if (uint8_t(b-B)>=NBits<I>) {
			throw_unless( val<=0 , "overflow"  ) ;
			throw_unless( val>=0 , "underflow" ) ;
		} else {
			throw_unless( val<=I( Max<I> >>uint8_t(b-B)) , "overflow"  ) ;
			throw_unless( val>=I( Min<I> >>uint8_t(b-B)) , "underflow" ) ;
			val <<= uint8_t(b-B) ;
		}
	}
	return I(val) ;
}

template<char U,::integral I> ::string to_string_with_unit(I x) {
	if (!x) {
		if (U) return "0"s+U ;
		else   return "0"    ;
	}
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
