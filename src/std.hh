// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dlfcn.h>        // dlopen
#include <fcntl.h>        //  O_*, openat, fcntl
#include <link.h>         // struct link_map
#include <netinet/ip.h>   // in_addr_t, in_port_t
#include <signal.h>       // SIG*, kill
#include <sys/file.h>     // AT_*, F_*, FD_*, LOCK_*
#include <sys/mman.h>     // mmap, munmap
#include <sys/mount.h>    // mount
#include <sys/resource.h> // getrlimit
#include <sys/sendfile.h>
#include <sys/stat.h>     // struct stat
#include <sys/types.h>    // ushort, uint, ulong, ...
#include <sys/wait.h>
#include <unistd.h>       // sysconf

#include <cmath>   // ldexp
#include <cstddef> // nullptr_t
#include <cstdint>
#include <cstring> // memcpy, strchr, strerror, strlen, strncmp, strnlen, strsignal

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <charconv> // from_chars, to_chars
#include <concepts>
#include <condition_variable>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// import std definitions to simplify code
using std::array                               ;
using std::as_const                            ;
using std::atomic                              ;
using std::atomic_signal_fence                 ;
using std::basic_string                        ;
using std::basic_string_view                   ;
using std::bit_ceil                            ;
using std::bit_width                           ;
using std::chars_format                        ;
using std::cmp_greater                         ;
using std::cmp_less                            ;
using std::condition_variable_any              ;
using std::conditional_t                       ;
using std::convertible_to                      ;
using std::countl_zero                         ;
using std::current_exception                   ;
using std::decay_t                             ;
using std::declval                             ;
using std::endian                              ;
using std::erase_if                            ;
using std::errc                                ;
using std::exception                           ;
using std::floating_point                      ;
using std::forward                             ;
using std::from_chars                          ;
using std::from_chars_result                   ;
using std::function                            ;
using std::get                                 ;
using std::has_unique_object_representations_v ;
using std::hash                                ;
using std::initializer_list                    ;
using std::integral                            ;
using std::is_aggregate_v                      ;
using std::is_arithmetic_v                     ;
using std::is_base_of_v                        ;
using std::is_const_v                          ;
using std::is_empty_v                          ;
using std::is_enum_v                           ;
using std::is_floating_point_v                 ;
using std::is_integral_v                       ;
using std::is_pointer_v                        ;
using std::is_same_v                           ;
using std::is_signed_v                         ;
using std::is_standard_layout_v                ;
using std::is_trivial_v                        ;
using std::is_trivially_copyable_v             ;
using std::is_unsigned_v                       ;
using std::is_void_v                           ;
using std::jthread                             ;
using std::latch                               ;
using std::launder                             ;
using std::make_error_code                     ;
using std::make_move_iterator                  ;
using std::make_signed_t                       ;
using std::make_unsigned_t                     ;
using std::map                                 ;
using std::memory_order_acq_rel                ;
using std::monostate                           ;
using std::move                                ;
using std::mutex                               ;
using std::nullptr_t                           ;
using std::numeric_limits                      ;
using std::operator""s                         ;
using std::optional                            ;
using std::pair                                ;
using std::partial_ordering                    ;
using std::popcount                            ;
using std::remove_const_t                      ;
using std::remove_reference_t                  ;
using std::rethrow_exception                   ;
using std::set                                 ;
using std::set_terminate                       ;
using std::shared_mutex                        ;
using std::size                                ;
using std::span                                ;
using std::stop_callback                       ;
using std::stop_token                          ;
using std::string                              ;
using std::string_view                         ;
using std::strong_ordering                     ;
using std::swap                                ;
using std::thread                              ;
using std::tie                                 ;
using std::to_chars                            ;
using std::to_chars_result                     ;
using std::to_string                           ;
using std::tuple                               ;
using std::underlying_type_t                   ;
using std::unique_ptr                          ;
using std::unordered_map                       ;
using std::unordered_set                       ;
using std::unsigned_integral                   ;
using std::variant                             ;
using std::vector                              ;
// keep std definitions hidden by global definitions to simplify usage
using std::ranges::all_of  ;
using std::ranges::any_of  ;
using std::binary_search   ;
using std::count           ;
using std::count_if        ;
using std::lower_bound     ;
using std::max             ;
using std::min             ;
using std::ranges::none_of ;
using std::sort            ;
using std::stable_sort     ;
// special cases
using std::getline ;    // getline also has a C version that would hide std::getline without explicit using
namespace this_thread {
	using std::this_thread::get_id ;
} ;

#ifdef __cpp_lib_unreachable
	using std::unreachable ;
#else
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

#define self (*this)

//
// std lib name simplification
//

// array
template<                size_t N> using array_s  = ::array        <         ::string,N> ;
template<class K,class V,size_t N> using amap     = ::array<::pair <K       ,V>      ,N> ;
template<        class V,size_t N> using amap_s   = ::amap         <::string,V       ,N> ;
template<                size_t N> using amap_ss  = ::amap_s       <         ::string,N> ;
// pair
template<        class V         > using pair_s   = ::pair         <::string,V         > ;
/**/                               using pair_ss  = ::pair_s       <         ::string  > ;
// map
template<        class V         > using map_s    = ::map          <::string,V         > ;
/**/                               using map_ss   = ::map_s        <         ::string  > ;
// set
/**/                               using set_s    = ::set          <         ::string  > ;
// umap
template<class K,class V         > using umap     = ::unordered_map<K       ,V         > ;
template<        class V         > using umap_s   = ::umap         <::string,V         > ;
/**/                               using umap_ss  = ::umap_s       <         ::string  > ;
// uset
template<class K                 > using uset     = ::unordered_set<K                  > ;
/**/                               using uset_s   = ::uset         <::string           > ;
// vector
/**/                               using vector_s = ::vector       <         ::string  > ;
template<class K,class V         > using vmap     = ::vector<::pair<K       ,V>        > ;
template<        class V         > using vmap_s   = ::vmap         <::string,V         > ;
/**/                               using vmap_ss  = ::vmap_s       <         ::string  > ;
// optional
/**/                               using optional_s = ::optional   <         ::string  > ;


template<class T,size_t N        > constexpr bool operator+(::array   <T,N  > const&  ) { return  N                   ; }
template<class T,class  U        > constexpr bool operator+(::pair    <T,U  > const& p) { return  +p.first||+p.second ; }
template<class K,class  V        >           bool operator+(::map     <K,V  > const& m) { return !m.empty()           ; }
template<class K,class  V,class C>           bool operator+(::map     <K,V,C> const& m) { return !m.empty()           ; }
template<class K,class  V        >           bool operator+(::umap    <K,V  > const& m) { return !m.empty()           ; }
template<class K                 >           bool operator+(::set     <K    > const& s) { return !s.empty()           ; }
template<class K         ,class C>           bool operator+(::set     <K  ,C> const& s) { return !s.empty()           ; }
template<class K                 >           bool operator+(::uset    <K    > const& s) { return !s.empty()           ; }
template<class T                 > constexpr bool operator+(::vector  <T    > const& v) { return !v.empty()           ; }
template<class T                 > constexpr bool operator+(::optional<T    > const& o) { return  o.has_value()       ; }
//
inline                      bool operator+(::string const& s) { return !s.empty() ; } // empty() is not constexpr in C++20
inline            constexpr bool operator+(::string_view   s) { return !s.empty() ; } // .
template<class T> constexpr bool operator+(::span<T>       v) { return !v.empty() ; } // .

template<class T> requires requires(T const& x) { !+x ; } constexpr bool operator!(T const& x) { return !+x ; }

// support container arg to standard utility functions
#define VT        typename T::value_type
#define CMP       ::function<bool(VT const&,VT const&)>
#define PRED      ::function<bool(VT const&          )>
#define FUNC      ::function<void(VT      &          )>
#define DFLT_PRED [](VT const& v)->bool { return +v ; }

template<class T> constexpr          bool              binary_search( T const& x , VT const& v , CMP  cmp            ) { return ::binary_search( x.begin() , x.end() , v , cmp  ) ; }
template<class T> constexpr          bool              binary_search( T const& x , VT const& v                       ) { return ::binary_search( x.begin() , x.end() , v        ) ; }
template<class T> constexpr          ptrdiff_t         count        ( T const& x , VT const& v                       ) { return ::count        ( x.begin() , x.end() , v        ) ; }
template<class T> constexpr          ptrdiff_t         count_if     ( T const& x ,               PRED pred=DFLT_PRED ) { return ::count_if     ( x.begin() , x.end() ,     pred ) ; }
template<class T> constexpr typename T::const_iterator lower_bound  ( T const& x , VT const& v , CMP  cmp            ) { return ::lower_bound  ( x.begin() , x.end() , v , cmp  ) ; }
template<class T> constexpr typename T::const_iterator lower_bound  ( T const& x , VT const& v                       ) { return ::lower_bound  ( x.begin() , x.end() , v        ) ; }
template<class T> constexpr          void              sort         ( T      & x ,               CMP  cmp            ) {        ::sort         ( x.begin() , x.end() ,     cmp  ) ; }
template<class T> constexpr          void              sort         ( T      & x                                     ) {        ::sort         ( x.begin() , x.end()            ) ; }
template<class T> constexpr          void              stable_sort  ( T      & x ,               CMP  cmp            ) {        ::stable_sort  ( x.begin() , x.end() ,     cmp  ) ; }
template<class T> constexpr          void              stable_sort  ( T      & x                                     ) {        ::stable_sort  ( x.begin() , x.end()            ) ; }

template<class V,class T> V  max( T const& x , ::function<V(VT const&)> val , V  init={} ) { for( auto const& v : x ) init = ::max(init,val(v)) ; return ::move(init) ; }
template<class V,class T> V  min( T const& x , ::function<V(VT const&)> val , V  init={} ) { for( auto const& v : x ) init = ::min(init,val(v)) ; return ::move(init) ; }
template<        class T> VT max( T const& x ,                                VT init={} ) { for( auto const& v : x ) init = ::max(init,    v ) ; return ::move(init) ; }
template<        class T> VT min( T const& x ,                                VT init={} ) { for( auto const& v : x ) init = ::min(init,    v ) ; return ::move(init) ; }

#undef DFLT_PRED
#undef FUNC
#undef PRED
#undef CMP
#undef VT

template<class T> constexpr T  copy(T const& x) { return   x ; }
template<class T> constexpr T& ref (T     && x) { return *&x ; }
