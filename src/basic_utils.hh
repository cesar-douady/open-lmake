// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "sys_config.h"

#include "std.hh"

#define LIKELY(  x) __builtin_expect(bool(x),1)
#define UNLIKELY(x) __builtin_expect(bool(x),0)

static constexpr size_t Npos = ::string::npos ;

#if HAS_UINT128
	using int128_t  = __int128_t  ;
	using uint128_t = __uint128_t ;
#endif

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
template<class T,class D=::monostate> using NoVoid = ::conditional_t<::is_void_v<T>,D,T> ;
template<class T,T... X> requires(false) struct Err {} ;                                   // for debug purpose, to be used as a tracing point through the diagnostic message

template<size_t NB> using Uint = ::conditional_t< NB<=8 , uint8_t , ::conditional_t< NB<=16 , uint16_t , ::conditional_t< NB<=32 , uint32_t , ::conditional_t< NB<=64 , uint64_t , void > > > > ;

template<class T1,class T2> concept Same = ::is_same_v<T1,T2> ;

template<class T,class... Ts> struct IsOneOfHelper ;
template<class T,class... Ts> concept IsOneOf = IsOneOfHelper<T,Ts...>::yes ;
template<class T                     > struct IsOneOfHelper<T         > { static constexpr bool yes = false                                 ; } ;
template<class T,class T0,class... Ts> struct IsOneOfHelper<T,T0,Ts...> { static constexpr bool yes = ::is_same_v<T,T0> || IsOneOf<T,Ts...> ; } ;

template<class T> concept IsChar = ::is_trivial_v<T> && ::is_standard_layout_v<T> ; // necessary property to make a ::basic_string
template<class T> using AsChar = ::conditional_t<IsChar<T>,T,char> ;                // provide default value if not a Char so as to make ::basic_string before knowing if it is possible

template<class D,class B> concept IsA =                     // for use in template : template<IsA<B> T> ... applies template for all T deriving from B
	::is_same_v<::remove_const_t<B>,::remove_const_t<D>>    // case where B is not a class
||	::is_base_of_v<::remove_const_t<B>,::remove_const_t<D>> // case where B is a class
;

template<class T> static constexpr size_t NBits = sizeof(T)*8 ;

template<class T> static constexpr T Max = ::numeric_limits<T>::max() ;
template<class T> static constexpr T Min = ::numeric_limits<T>::min() ;

#define VT(T) typename T::value_type
#define RC(T) ::remove_const_t<T>

// easy transformation of a container into another
template<class K,        class V> ::set   <K                                       > mk_set   (V const& v) { return { v.begin() , v.end() } ; }
template<class K,        class V> ::uset  <K                                       > mk_uset  (V const& v) { return { v.begin() , v.end() } ; }
template<        class T,class V> ::vector<                      T                 > mk_vector(V const& v) { return { v.begin() , v.end() } ; }
template<class K,class T,class V> ::map   <K                    ,T                 > mk_map   (V const& v) { return { v.begin() , v.end() } ; }
template<class K,class T,class V> ::umap  <K                    ,T                 > mk_umap  (V const& v) { return { v.begin() , v.end() } ; }
template<class K,class T,class V> ::vmap  <K                    ,T                 > mk_vmap  (V const& v) { return { v.begin() , v.end() } ; }
// with implicit key type
template<        class T,class V> ::map   <RC(VT(V)::first_type),T                 > mk_map   (V const& v) { return { v.begin() , v.end() } ; }
template<        class T,class V> ::umap  <RC(VT(V)::first_type),T                 > mk_umap  (V const& v) { return { v.begin() , v.end() } ; }
template<        class T,class V> ::vmap  <RC(VT(V)::first_type),T                 > mk_vmap  (V const& v) { return { v.begin() , v.end() } ; }
// with implicit item type
template<                class V> ::set   <RC(VT(V)            )                   > mk_set   (V const& v) { return { v.begin() , v.end() } ; }
template<                class V> ::uset  <RC(VT(V)            )                   > mk_uset  (V const& v) { return { v.begin() , v.end() } ; }
template<                class V> ::vector<                      VT(V)             > mk_vector(V const& v) { return { v.begin() , v.end() } ; }
template<                class V> ::map   <RC(VT(V)::first_type),VT(V)::second_type> mk_map   (V const& v) { return { v.begin() , v.end() } ; }
template<                class V> ::umap  <RC(VT(V)::first_type),VT(V)::second_type> mk_umap  (V const& v) { return { v.begin() , v.end() } ; }
template<                class V> ::vmap  <RC(VT(V)::first_type),VT(V)::second_type> mk_vmap  (V const& v) { return { v.begin() , v.end() } ; }

// keys & vals
template<class K,class M> ::set   <K> const mk_key_set   (M const& m) { ::set   <K> res ;                         for( auto const& [k,v] : m ) res.insert   (k) ; return res ; }
template<class K,class M> ::uset  <K> const mk_key_uset  (M const& m) { ::uset  <K> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m ) res.insert   (k) ; return res ; }
template<class K,class M> ::vector<K> const mk_key_vector(M const& m) { ::vector<K> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m ) res.push_back(k) ; return res ; }
template<class T,class M> ::set   <T>       mk_val_set   (M const& m) { ::set   <T> res ;                         for( auto const& [k,v] : m ) res.insert   (v) ; return res ; }
template<class T,class M> ::uset  <T>       mk_val_uset  (M const& m) { ::uset  <T> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m ) res.insert   (v) ; return res ; }
template<class T,class M> ::vector<T>       mk_val_vector(M const& m) { ::vector<T> res ; res.reserve(m.size()) ; for( auto const& [k,v] : m ) res.push_back(v) ; return res ; }
// with implicit item type
template<class M> ::set   <RC(VT(M)::first_type )> const mk_key_set   (M const& m) { return mk_key_set   <RC(VT(M)::first_type )>(m) ; }
template<class M> ::uset  <RC(VT(M)::first_type )> const mk_key_uset  (M const& m) { return mk_key_uset  <RC(VT(M)::first_type )>(m) ; }
template<class M> ::vector<RC(VT(M)::first_type )> const mk_key_vector(M const& m) { return mk_key_vector<RC(VT(M)::first_type )>(m) ; }
template<class M> ::set   <   VT(M)::second_type >       mk_val_set   (M const& m) { return mk_val_set   <   VT(M)::second_type >(m) ; }
template<class M> ::uset  <   VT(M)::second_type >       mk_val_uset  (M const& m) { return mk_val_uset  <   VT(M)::second_type >(m) ; }
template<class M> ::vector<   VT(M)::second_type >       mk_val_vector(M const& m) { return mk_val_vector<   VT(M)::second_type >(m) ; }

// item replication
template<size_t N,class V> ::array<V,N> mk_array(V const& v) {
	::array<V,N> res ; for( V& e : res ) e = v ;
	return res ;
}

#undef RC
#undef VT

namespace std {
	template<class T> requires( requires(T t){t.hash();} ) struct hash<T> {
		size_t operator()(T const& x) const {
			return x.hash() ;
		}
	} ;
}

template<class T> T& grow( ::vector<T>& v , size_t i ) {
	if (i>=v.size()) v.resize(i+1) ;
	return v[i] ;
}

template<typename T> void append_move( ::vector<T>& dst, ::vector<T>&& src ) {
    if (!dst) dst = ::move(src) ;
    else      dst.insert( dst.end() , ::make_move_iterator(src.begin()) , ::make_move_iterator(src.end()) ) ;
}
//
// assert
//

struct Fd ;

extern thread_local char t_thread_key ;

void kill_self      ( int sig              ) ;
void write_backtrace( Fd      , int n_hide ) ;

template<void (*Handler)(int sig,void* addr)> void _sig_action( int sig , siginfo_t* si , void* ) {
	Handler(sig,si->si_addr) ;
}
inline struct sigaction get_sig_handler(int sig) {
	struct sigaction action ; ::sigaction( sig , nullptr/*act*/ , &action ) ;
	return action ;
}
inline void restore_sig_handler( int sig , struct sigaction const& action ) {
	::sigaction( sig , &action , nullptr/*oldact*/ ) ;
}
template<void (*Handler)(int sig,void* addr)> void set_sig_handler(int sig) {
	sigset_t         empty  ;      sigemptyset(&empty) ;                      // sigemptyset can be a macro
	struct sigaction action = {} ;
	action.sa_sigaction = _sig_action<Handler>  ;
	action.sa_mask      = empty                 ;
	action.sa_flags     = SA_RESTART|SA_SIGINFO ;
	::sigaction( sig , &action , nullptr/*oldact*/ ) ;
}
template<void (*Handler)(int sig)> void set_sig_handler(int sig) {
	sigset_t         empty  ;      sigemptyset(&empty) ;                      // sigemptyset can be a macro
	struct sigaction action = {} ;
	action.sa_handler = Handler    ;
	action.sa_mask    = empty      ;
	action.sa_flags   = SA_RESTART ;
	::sigaction( sig , &action , nullptr/*oldact*/ ) ;
}

template<void (*Handler)(int sig)> struct WithSigHandler {
	WithSigHandler(int sig_) : sig{sig_} , sav_sa{get_sig_handler(sig_)} {
		set_sig_handler<Handler>(sig) ;
	}
	~WithSigHandler() {
		restore_sig_handler( sig , sav_sa ) ;
	}
	// data
	int    sig              = 0  ;
	struct sigaction sav_sa = {} ;
} ;

//
// math
//

constexpr inline uint8_t n_bits(size_t n) { return NBits<size_t>-::countl_zero(n-1) ; } // number of bits to store n states

template<::integral T=size_t> constexpr T lsb_msk (uint8_t b) { return  (T(1)<<b)-1 ; }
template<::integral T=size_t> constexpr T msb_msk (uint8_t b) { return -(T(1)<<b)   ; }

//
// iota
//

template<bool WithStart,class T> struct Iota {
	using value_type = T ;
	struct Iterator {
		using value_type      = T         ;
		using difference_type = ptrdiff_t ;
		// cxtors & casts
		Iterator() = default ;
		constexpr Iterator(T c) : cur{c} {}
		// services
		constexpr bool      operator==(Iterator const&) const = default ;
		constexpr T         operator* (               ) const {                                return cur   ; }
		constexpr Iterator& operator++(               )       { cur = T(+cur+1) ;              return self  ; }
		constexpr Iterator  operator++(int            )       { Iterator self_=self ; ++self ; return self_ ; }
		// data
		T cur = {} ;
	} ;
	// cxtors & casts
	Iota() = default ;
	/**/               constexpr Iota(        T e ) requires(!WithStart) : bounds{     e} {}
	/**/               constexpr Iota(        T e ) requires( WithStart) : bounds{T( ),e} {}
	template<class T1> constexpr Iota( T1 b , T e ) requires( WithStart) : bounds{T(b),e} {}
	//accesses
	constexpr bool contains(T idx) const { return idx>=_first() && idx<_end() ; }
private :
	constexpr T _first() const { return WithStart ? bounds[0] : T() ; }
	constexpr T _end  () const { return bounds[WithStart]           ; }
	// services
public :
	constexpr Iterator begin() const { return Iterator(_first())  ; }
	constexpr Iterator end  () const { return Iterator(_end()  )  ; }
	constexpr size_t   size () const { return +_end() - +_first() ; }
	// data
	T bounds[1+WithStart] = {} ;
} ; //!                                   with_start
template<         class T > constexpr Iota<false   ,T > iota(            T  end ) ;
template<class T1,class T2> constexpr Iota<true    ,T2> iota( T1 begin , T2 end ) ;
//                                  with_start
template<class T> using Iota1 = Iota<false   ,T> ;
template<class T> using Iota2 = Iota<true    ,T> ;

struct First {
	// accesses
	bool operator+() const { return _val ; }
	// services
	bool advance() { uint8_t v = _val ; _val = ::min(_val+1,2) ; return v==0 ; }
	//
	/**/              const char* operator()(                                       ) { return advance() , ""                                        ; }
	template<class T> T           operator()( T&& first , T&& other =T()            ) { return advance() ? ::forward<T>(first) : ::forward<T>(other) ; }
	template<class T> T           operator()( T&& first , T&& second    , T&& other ) ;
	//
	const char* operator()( const char* first ,                      const char* other="" ) { return operator()<const char*&>(first,       other) ; }
	const char* operator()( const char* first , const char* second , const char* other    ) { return operator()<const char*&>(first,second,other) ; }
	//
	// data
private :
	uint8_t _val=0 ;
} ;

//
// string formatting
//

template<class F> concept _CanDoFunc    = requires(::string s,::decay_t<F> f) { f(s) ; } ;
template<class T> concept _CanDoBool    = ::is_same_v<::decay_t<T>,bool>                 ; // use a template to avoid having too high a priority when compiler selects an overload
template<class T> concept _CanDoPtr     = !_CanDoFunc<T*>                                ;
template<class N> concept _CanDoToChars = ::is_arithmetic_v<N> && !IsOneOf<N,char,bool>  ;

/**/                                       void operator>>( ::string_view          , ::string& ) ;
/**/                                       void operator>>( ::string const&        , ::string& ) ;
/**/                                       void operator>>( char                   , ::string& ) ;
/**/                                       void operator>>( const char*            , ::string& ) ;
/**/                                       void operator>>(       char*            , ::string& ) ;
/**/                                       void operator>>( nullptr_t              , ::string& ) ;
/**/                                       void operator>>( uint8_t                , ::string& ) ;
/**/                                       void operator>>( int8_t                 , ::string& ) ;
template<_CanDoFunc    F                 > void operator>>( F               const& , ::string& ) ;
template<_CanDoBool    B                 > void operator>>( B                      , ::string& ) ;
template<_CanDoPtr     T                 > void operator>>( T*                     , ::string& ) ;
template<_CanDoToChars N                 > void operator>>( N                      , ::string& ) ;
template<class         T,size_t N        > void operator>>(          T[N]          , ::string& ) ;
template<class         T,size_t N        > void operator>>( ::array <T,N  > const& , ::string& ) ;
template<class         T                 > void operator>>( ::vector<T    > const& , ::string& ) ;
template<class         T                 > void operator>>( ::span  <T    > const& , ::string& ) ;
template<class         K                 > void operator>>( ::uset  <K    > const& , ::string& ) ;
template<class         K                 > void operator>>( ::set   <K    > const& , ::string& ) ;
template<class         K,         class C> void operator>>( ::set   <K,  C> const& , ::string& ) ;
template<class         K,class V         > void operator>>( ::umap  <K,V  > const& , ::string& ) ;
template<class         K,class V         > void operator>>( ::map   <K,V  > const& , ::string& ) ;
template<class         K,class V ,class C> void operator>>( ::map   <K,V,C> const& , ::string& ) ;
template<class         A,class B         > void operator>>( ::pair<A,B>     const& , ::string& ) ;
template<class         T                 > void operator>>( ::optional<T>   const& , ::string& ) ;
#if HAS_UINT128
	void operator>>( uint128_t i , ::string& os ) ;
	void operator>>( int128_t  i , ::string& os ) ;
#endif

template<class T> ::string& operator<<( ::string& os , T&& ) ;

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

// START_OF_NO_COV for debug/trace only

/**/                 [[noreturn]] inline    void compile_time_crash(                                                      ) { throw 0 ; } // not constexpr, which forces a compile-time error
template<class... A> [[noreturn]]           void run_time_crash    ( int n_hide , int sig , int n_hdrs , A const&... args ) ;             // isolate so ::string can be declared with clang
template<class... A> [[noreturn]] constexpr void crash             ( int n_hide , int sig , int n_hdrs , A const&... args ) {
	if (::is_constant_evaluated()) compile_time_crash(                                   ) ;
	else                           run_time_crash    ( n_hide , sig , n_hdrs , args... ) ;
}

template<class... A> [[noreturn]] constexpr void _fail_prod (             int n_hdrs , A const&... args ) {            crash( 1 , SIGABRT , 1+n_hdrs , "fail : "               ,args... ) ; }
template<class... A>              constexpr void _swear_prod( bool cond , int n_hdrs , A const&... args ) { if (!cond) crash( 1 , SIGABRT , 1+n_hdrs , "assertion violation : ",args... ) ; }
#ifndef NDEBUG
	template<class... A> [[noreturn]] constexpr void _fail (             int n_hdrs , A const&... args ) { _fail_prod (        n_hdrs , args... ) ; }
	template<class... A>              constexpr void _swear( bool cond , int n_hdrs , A const&... args ) { _swear_prod( cond , n_hdrs , args... ) ; }
#else
	template<class... A> [[noreturn]] constexpr void _fail (             int        , A const&...      ) {            ::unreachable() ;             }
	template<class... A>              constexpr void _swear( bool cond , int        , A const&...      ) { if (!cond) ::unreachable() ;             }
#endif //!                                                                                                      n_hdrs
template<class... A> [[noreturn]] constexpr void fail      (             A const&... args ) { _fail      (        0  , args... ) ; }
template<class... A>              constexpr void swear     ( bool cond , A const&... args ) { _swear     ( cond , 0  , args... ) ; }
template<class... A> [[noreturn]] constexpr void fail_prod (             A const&... args ) { _fail_prod (        0  , args... ) ; }
template<class... A>              constexpr void swear_prod( bool cond , A const&... args ) { _swear_prod( cond , 0  , args... ) ; }

#define _FAIL_STR2(x) #x
#define _FAIL_STR(x) _FAIL_STR2(x)
#define FAIL(           ...) _fail      (       3,"@" __FILE__ ":" _FAIL_STR(__LINE__) " in ",__PRETTY_FUNCTION__             __VA_OPT__(," : " #__VA_ARGS__ " = ",) __VA_ARGS__)
#define FAIL_PROD(      ...) _fail_prod (       3,"@" __FILE__ ":" _FAIL_STR(__LINE__) " in ",__PRETTY_FUNCTION__             __VA_OPT__(," : " #__VA_ARGS__ " = ",) __VA_ARGS__)
#define SWEAR(     cond,...) _swear     ((cond),3,"@" __FILE__ ":" _FAIL_STR(__LINE__) " in ",__PRETTY_FUNCTION__," : " #cond __VA_OPT__( " : " #__VA_ARGS__ " = ",) __VA_ARGS__)
#define SWEAR_PROD(cond,...) _swear_prod((cond),3,"@" __FILE__ ":" _FAIL_STR(__LINE__) " in ",__PRETTY_FUNCTION__," : " #cond __VA_OPT__( " : " #__VA_ARGS__ " = ",) __VA_ARGS__)

#define DF default : FAIL() ; // for use at end of switch statements
#define DN default :        ; // .

template<::unsigned_integral I> ::string to_hex( I v , uint8_t width=sizeof(I)*2 ) {
	::string res ( width , '0' ) ;
	for( uint8_t i : iota(width) ) {
		uint8_t d = v%16 ;
		res[width-1-i] = d<10 ? '0'+d : 'a'+d-10 ;
		v >>= 4 ;
		if (!v) break ;
	}
	SWEAR( !v , v,res ) ;
	return res ;
}

#if __cplusplus<202600L
	inline ::string operator+( ::string     && s , ::string_view   v ) {                    s.append(  v.data(),v.size()) ; return ::move(s) ; }
	inline ::string operator+( ::string_view   v , ::string     && s ) {                    s.insert(0,v.data(),v.size()) ; return ::move(s) ; }
	inline ::string operator+( ::string const& s , ::string_view   v ) { ::string r = s   ; r.append(  v.data(),v.size()) ; return        r  ; }
	inline ::string operator+( ::string_view   v , ::string const& s ) { ::string r { v } ; r.append(  s.data(),s.size()) ; return        r  ; }
#endif

inline                    void operator>>( ::string_view   v , ::string& os ) { os.append   (v)                                    ; }
inline                    void operator>>( ::string const& s , ::string& os ) { os.append   (s)                                    ; }
inline                    void operator>>( char            c , ::string& os ) { os.push_back(c)                                    ; }
inline                    void operator>>( const char*     s , ::string& os ) { os.append   (s)                                    ; }
inline                    void operator>>(       char*     s , ::string& os ) { os.append   (s)                                    ; }
inline                    void operator>>( nullptr_t         , ::string& os ) { os << "<null>"                                     ; }
inline                    void operator>>( uint8_t         i , ::string& os ) { os << uint32_t(i)                                  ; }                             // avoid char when actually a int
inline                    void operator>>( int8_t          i , ::string& os ) { os << int32_t (i)                                  ; }                             // .
template<_CanDoFunc    F> void operator>>( F        const& f , ::string& os ) { f(os)                                              ; }
template<_CanDoBool    B> void operator>>( B               b , ::string& os ) { os << (b?"true":"false")                           ; }
template<_CanDoPtr     T> void operator>>( T*              p , ::string& os ) { os << "0x"<<to_hex(reinterpret_cast<uintptr_t>(p)) ; }
template<_CanDoToChars N> void operator>>( N               n , ::string& os ) {
	size_t old_sz = os.size()  ;
	size_t new_sz = old_sz+30 ;                                                                                                                                    // more than enough to print a number
	os.resize(new_sz) ;
	::to_chars_result rc = ::to_chars( os.data()+old_sz , os.data()+new_sz , n ) ; SWEAR(rc.ec==::errc()) ; SWEAR(rc.ptr<=os.data()+new_sz) ;
	os.resize(rc.ptr-os.data()) ;
}
//
template<class T,size_t N        > void operator>>(          T             a[N] , ::string& os ) { First f ; os<<'[' ; for( T    const&  x    : a ) { os<<f("",",")<<x         ; } os<<']' ; }
template<class T,size_t N        > void operator>>( ::array <T,N  > const& a    , ::string& os ) { First f ; os<<'[' ; for( T    const&  x    : a ) { os<<f("",",")<<x         ; } os<<']' ; }
template<class T                 > void operator>>( ::vector<T    > const& v    , ::string& os ) { First f ; os<<'[' ; for( T    const&  x    : v ) { os<<f("",",")<<x         ; } os<<']' ; }
template<class T                 > void operator>>( ::span  <T    > const& v    , ::string& os ) { First f ; os<<'[' ; for( T    const&  x    : v ) { os<<f("",",")<<x         ; } os<<']' ; }
template<class K                 > void operator>>( ::uset  <K    > const& s    , ::string& os ) { First f ; os<<'{' ; for( K    const&  k    : s ) { os<<f("",",")<<k         ; } os<<'}' ; }
template<class K                 > void operator>>( ::set   <K    > const& s    , ::string& os ) { First f ; os<<'{' ; for( K    const&  k    : s ) { os<<f("",",")<<k         ; } os<<'}' ; }
template<class K,         class C> void operator>>( ::set   <K,  C> const& s    , ::string& os ) { First f ; os<<'{' ; for( K    const&  k    : s ) { os<<f("",",")<<k         ; } os<<'}' ; }
template<class K,class V         > void operator>>( ::umap  <K,V  > const& m    , ::string& os ) { First f ; os<<'{' ; for( auto const& [k,v] : m ) { os<<f("",",")<<k<<':'<<v ; } os<<'}' ; }
template<class K,class V         > void operator>>( ::map   <K,V  > const& m    , ::string& os ) { First f ; os<<'{' ; for( auto const& [k,v] : m ) { os<<f("",",")<<k<<':'<<v ; } os<<'}' ; }
template<class K,class V ,class C> void operator>>( ::map   <K,V,C> const& m    , ::string& os ) { First f ; os<<'{' ; for( auto const& [k,v] : m ) { os<<f("",",")<<k<<':'<<v ; } os<<'}' ; }
template<class A,class B         > void operator>>( ::pair<A,B>     const& p    , ::string& os ) { os <<'('<<p.first<<','<<p.second<<')' ;                                                   }
template<class T                 > void operator>>( ::optional<T>   const& o    , ::string& os ) { if (+o) os<<*o ; else os<<"<none>" ;                                                      }
//
#if HAS_UINT128
	inline void operator>>( uint128_t i , ::string& os ) {                                                                                                         // non-standard
		static constexpr uint64_t Max10 = uint64_t(10)*uint64_t(1000000000)*uint64_t(1000000000) ; static_assert( Max<uint64_t>/2<Max10 && Max10<Max<uint64_t> ) ; // 10^19
		// try easy case
		if (i<=Max<uint64_t>) { os << uint64_t(i) ; return ; }
		// do complex case
		uint128_t msb     = i/Max10 ;
		uint64_t  lsb     = i%Max10 ;
		::string  lsb_str ;           lsb_str << lsb ;
		::string  s       ;
		s << msb                                         ;
		s << widen(lsb_str,19,true/*right*/,'0'/*fill*/) ;
		os << s ;
	}
	inline void operator>>( int128_t i , ::string& os ) {                                                                                                          // non-standard
		if (i>=0) os <<      uint128_t( i) ;
		else      os << '-'<<uint128_t(-i) ;
	}
#endif

// END_OF_NO_COV

template<class... A> struct _CatHelper {
	static ::string s_cat(A&&... args) {
		::string res ;
		((res<<args),...) ;
		return res ;
	}
} ;
template<class... A> struct _CatHelper<::string&&,A...> {
	static ::string s_cat(::string&& res , A&&... args ) {
		((res+=args),...) ;
		return ::move(res) ;
	}
} ;
template<class... A> ::string cat(A&&... args) {
	return _CatHelper<A...>::s_cat(::forward<A>(args)...) ;
}

// ::isspace is too high level as it accesses environment, which may not be available during static initialization
inline constexpr bool is_space(char c) {
	constexpr ::array<bool,256> Tab = []()->::array<bool,256> {
		::array<bool,256> res = {} ;
		res['\f'] = true ;
		res['\n'] = true ;
		res['\r'] = true ;
		res['\t'] = true ;
		res['\v'] = true ;
		res[' ' ] = true ;
		return res ;
	}() ;
	return Tab[uint8_t(c)] ;
}

inline ::string strip(::string&& txt) {
	size_t start = 0 ;
	while ( start<txt.size() && is_space(txt[start])) start++        ;
	while ( start<txt.size() && is_space(txt.back())) txt.pop_back() ;
	if (start) return ::move(txt).substr(start) ;
	else       return ::move(txt)               ;
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

//
// miscellaneous
//

template<class... A> constexpr void throw_if    ( bool cond , A const&... args ) { if ( cond) throw cat(args...) ; }
template<class... A> constexpr void throw_unless( bool cond , A const&... args ) { if (!cond) throw cat(args...) ; }

template<::integral I> I decode_int(const char* p) {
	I r ; ::memcpy( &r , p , sizeof(I) ) ;
	return r ;
}

template<::integral I> void encode_int( char* p , I x ) {
	::memcpy( p , &x , sizeof(I) ) ;
}

//
// Implementation
//

template<class T> ::string& operator<<( ::string& os , T&& x ) { ::forward<T>(x)>>os ; return os ; }

template<         class T > constexpr Iota<false   ,T > iota(            T  end ) {                                    return {   end} ; }
template<class T1,class T2> constexpr Iota<true    ,T2> iota( T1 begin , T2 end ) { T2 b2=T2(begin) ; SWEAR(b2<=end) ; return {b2,end} ; }

template<class T> T First::operator()( T&& first , T&& second , T&& other ) {
	uint8_t v = _val ;
	advance() ;
	switch (v) {
		case 0 : return ::forward<T>(first ) ;
		case 1 : return ::forward<T>(second) ;
		case 2 : return ::forward<T>(other ) ;
	DF}                                        // NO_COV
}
