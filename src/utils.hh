// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "basic_utils.hh"
#include "enum.hh"

//
// string
//

inline ::string_view substr_view( ::string const& s , size_t start , size_t len=Npos ) {
	SWEAR(start<=s.size()) ;
	return { s.data()+start , ::min(len,s.size()-start) } ;
}

inline ::string const& operator| ( ::string const& a , ::string const& b ) { return +a ?        a  :        b  ; }
inline ::string      & operator| ( ::string      & a , ::string      & b ) { return +a ?        a  :        b  ; }
inline ::string        operator| ( ::string     && a , ::string     && b ) { return +a ? ::move(a) : ::move(b) ; }
inline ::string      & operator|=( ::string      & a , ::string const& b ) { if (!a) a =        b  ; return a ;  }
inline ::string      & operator|=( ::string      & a , ::string     && b ) { if (!a) a = ::move(b) ; return a ;  }

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

template<::integral I,IsOneOf<::string,::string_view> S> I from_string( S const& txt , bool empty_ok=false , bool hex=false ) {
	static constexpr bool IsBool = is_same_v<I,bool> ;
	if ( empty_ok && !txt ) return 0 ;
	::conditional_t<IsBool,size_t,I> res = 0/*garbage*/ ;
	//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	::from_chars_result rc = ::from_chars( txt.data() , txt.data()+txt.size() , res , hex?16:10 ) ;
	//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( IsBool && res>1 ) throw "bool value must be 0 or 1"s       ;
	if ( rc.ec!=::errc{} ) throw ::make_error_code(rc.ec).message() ;
	else                   return res                               ;
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

/**/   ::string mk_shell_str(::string_view  ) ;
/**/   ::string mk_py_str   (::string_view  ) ;
inline ::string mk_py_str   (const char*   s) { return mk_py_str(::string_view(s)) ; }
inline ::string mk_py_str   (bool          b) { return b ? "True" : "False"        ; }

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
template<char Delimiter=0> ::string parse_printable( ::string const& , size_t& pos=::ref(size_t()) ) ;

template<class T> requires(IsOneOf<T,::vector_s,::vmap_s<::vector_s>>) ::string mk_printable   ( T        const&                               , bool empty_ok=true ) ;
template<class T> requires(IsOneOf<T,::vector_s,::vmap_s<::vector_s>>) T        parse_printable( ::string const& , size_t& pos=::ref(size_t()) , bool empty_ok=true ) ;

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

inline ::string_view first_lines( ::string_view txt , size_t n_sep , char sep='\n' ) {
	size_t pos = -1 ;
	for( [[maybe_unused]] size_t _ : iota(n_sep) ) {
		pos = txt.find(sep,pos+1) ;
		if (pos==Npos) return txt ;
	}
	return txt.substr(0,pos+1) ;
}

template<char U,::integral I=size_t,bool RndUp=false>        I        from_string_with_unit    (::string const& s) ; // if U is provided, return value is expressed in this unit
template<char U,::integral I=size_t                 >        ::string to_string_with_unit      (I               x) ;
template<char U,::integral I=size_t                 >        ::string to_short_string_with_unit(I               x) ;
template<       ::integral I=size_t,bool RndUp=false> inline I        from_string_with_unit    (::string const& s) { return from_string_with_unit    <0,I,RndUp>(s) ; }
template<       ::integral I=size_t                 > inline ::string to_string_with_unit      (I               x) { return to_string_with_unit      <0,I      >(x) ; }
template<       ::integral I=size_t                 > inline ::string to_short_string_with_unit(I               x) { return to_short_string_with_unit<0,I      >(x) ; }

//
// span
//

using span_s = ::span<::string> ;

//
// math
//

template<size_t D,class N> inline constexpr N round_down(N n) { return n - N(n%D)              ; }
template<size_t D,class N> inline constexpr N round_up  (N n) { return round_down<D>(N(n+D-1)) ; }
template<size_t D,class N> inline constexpr N div_up    (N n) { return N(n+D-1)/D              ; }

template<class N> inline constexpr N round_down( N n , N d ) { return n - N(n%d)             ; }
template<class N> inline constexpr N round_up  ( N n , N d ) { return round_down(N(n+d-1),d) ; }
template<class N> inline constexpr N div_up    ( N n , N d ) { return N(n+d-1)/d             ; }

static constexpr double Infinity = ::numeric_limits<double>::infinity () ;
static constexpr double Nan      = ::numeric_limits<double>::quiet_NaN() ;

// used to disambiguate some calls
enum class NewType  : uint8_t { New  } ; static constexpr NewType  New  = NewType ::New  ;
enum class DfltType : uint8_t { Dflt } ; static constexpr DfltType Dflt = DfltType::Dflt ;

// Bool3 is both generally useful and provides an example of use the previous helpers
enum class Bool3 : uint8_t {
	No
,	Maybe
,	Yes
} ;
static constexpr Bool3 No    = Bool3::No    ;
static constexpr Bool3 Maybe = Bool3::Maybe ;
static constexpr Bool3 Yes   = Bool3::Yes   ;
inline constexpr Bool3  operator~ ( Bool3  b             ) {                return Bool3(+Yes-+b)                                                      ; }
inline constexpr Bool3  operator| ( Bool3  b1 , Bool3 b2 ) {                return ::max(b1,b2)                                                        ; }
inline constexpr Bool3  operator| ( Bool3  b1 , bool  b2 ) {                return  b2 ? Yes : b1                                                      ; }
inline constexpr Bool3  operator| ( bool   b1 , Bool3 b2 ) {                return  b1 ? Yes : b2                                                      ; }
inline constexpr Bool3  operator& ( Bool3  b1 , Bool3 b2 ) {                return ::min(b1,b2)                                                        ; }
inline constexpr Bool3  operator& ( Bool3  b1 , bool  b2 ) {                return !b2 ? No  : b1                                                      ; }
inline constexpr Bool3  operator& ( bool   b1 , Bool3 b2 ) {                return !b1 ? No  : b2                                                      ; }
inline constexpr Bool3& operator|=( Bool3& b1 , Bool3 b2 ) { b1 = b1 | b2 ; return b1                                                                  ; }
inline constexpr Bool3& operator|=( Bool3& b1 , bool  b2 ) { b1 = b1 | b2 ; return b1                                                                  ; }
inline constexpr Bool3& operator&=( Bool3& b1 , Bool3 b2 ) { b1 = b1 & b2 ; return b1                                                                  ; }
inline constexpr Bool3& operator&=( Bool3& b1 , bool  b2 ) { b1 = b1 & b2 ; return b1                                                                  ; }
inline constexpr Bool3  common    ( Bool3  b1 , Bool3 b2 ) {                return b1==Yes ? (b2==Yes?Yes:Maybe) : b1==No ? ( b2==No?No:Maybe) : Maybe ; }
inline constexpr Bool3  common    ( Bool3  b1 , bool  b2 ) {                return b2      ? (b1==Yes?Yes:Maybe) :          ( b1==No?No:Maybe)         ; }
inline constexpr Bool3  common    ( bool   b1 , Bool3 b2 ) {                return b1      ? (b2==Yes?Yes:Maybe) :          ( b2==No?No:Maybe)         ; }
inline constexpr Bool3  common    ( bool   b1 , bool  b2 ) {                return b1      ? (b2     ?Yes:Maybe) :          (!b2    ?No:Maybe)         ; }

//
// mutexes
//

// prevent dead locks by associating a level to each mutex, so we can verify the absence of dead-locks even in absence of race
// use of identifiers (in the form of an enum) allows easy identification of the origin of misorder
enum class MutexLvl : uint8_t { // identify who is owning the current level to ease debugging
	Unlocked                    // used in Lock to identify when not locked
,	None
// level 1
,	Audit
,	JobExec
,	Rule
,	StartJob
// level 2
,	Backend                     // must follow StartJob
// level 3
,	BackendId                   // must follow Backend
,	Gil                         // must follow Backend
,	NodeCrcDate                 // must follow Backend
,	Req                         // must follow Backend
,	TargetDir                   // must follow Backend
// level 4
,	Autodep1                    // must follow Gil
,	Gather                      // must follow Gil
,	Job                         // must follow Backend, by symetry with Node
,	Node                        // must follow NodeCrcDate
,	ReqInfo                     // must follow Req
,	Time                        // must follow BackendId
// level 5
,	Autodep2                    // must follow Autodep1
// inner (locks that take no other locks)
,	File
,	Hash
,	Sge
,	Slurm
,	SmallId
,	Thread
,	Workload
// very inner
,	Trace                       // allow tracing anywhere (but tracing may call some syscall)
,	SyscallTab                  // any syscall may need this mutex, which may occur during tracing
,	PdateNew                    // may need time anywhere, even during syscall processing
} ;

extern thread_local MutexLvl t_mutex_lvl ;
template<MutexLvl Lvl_,bool S=false/*shared*/> struct Mutex : ::conditional_t<S,::shared_mutex,::mutex> {
	using Base =                                              ::conditional_t<S,::shared_mutex,::mutex> ;
	static constexpr MutexLvl Lvl = Lvl_ ;
	// services
	void lock         (MutexLvl& lvl)             { SWEAR( t_mutex_lvl< Lvl         , t_mutex_lvl,Lvl ) ; lvl         = t_mutex_lvl ; t_mutex_lvl = Lvl                ; Base::lock         () ; }
	void lock_shared  (MutexLvl& lvl) requires(S) { SWEAR( t_mutex_lvl< Lvl         , t_mutex_lvl,Lvl ) ; lvl         = t_mutex_lvl ; t_mutex_lvl = Lvl                ; Base::lock_shared  () ; }
	void unlock       (MutexLvl& lvl)             { SWEAR( t_mutex_lvl==Lvl && +lvl , t_mutex_lvl,Lvl ) ; t_mutex_lvl = lvl         ; lvl         = MutexLvl::Unlocked ; Base::unlock       () ; }
	void unlock_shared(MutexLvl& lvl) requires(S) { SWEAR( t_mutex_lvl==Lvl && +lvl , t_mutex_lvl,Lvl ) ; t_mutex_lvl = lvl         ; lvl         = MutexLvl::Unlocked ; Base::unlock_shared() ; }
	#ifndef NDEBUG
		void swear_locked() { SWEAR(t_mutex_lvl>=Lvl,t_mutex_lvl) ; SWEAR(!Base::try_lock()) ; }
	#else
		void swear_locked() {}
	#endif
} ;

#ifndef NDEBUG
	struct NoMutex { // check exclusion is guaranteed by caller
		// services
		void lock         (MutexLvl&) { SWEAR(!_busy.exchange(true)) ; }
		void lock_shared  (MutexLvl&) { SWEAR(!_busy               ) ; }
		void unlock       (MutexLvl&) { _busy = false                ; }
		void unlock_shared(MutexLvl&) { SWEAR(!_busy               ) ; }
		void swear_locked (         ) { SWEAR( _busy               ) ; }
	private :
		::atomic<bool> _busy { false } ;
	} ;
#else
	struct NoMutex {
		// services
		void lock         (MutexLvl&) {}
		void lock_shared  (MutexLvl&) {}
		void unlock       (MutexLvl&) {}
		void unlock_shared(MutexLvl&) {}
		void swear_locked (         ) {}
	} ;
#endif

template<class M> struct Lock {
	// cxtors & casts
	Lock() = default ;
	Lock (M& m) : _mutex{&m} { lock  () ; }
	~Lock(    )              { unlock() ; }
	// services
	void lock  () { SWEAR(!_lvl) ; _mutex->lock  (_lvl) ; }
	void unlock() { SWEAR(+_lvl) ; _mutex->unlock(_lvl) ; }
	// data
	M*       _mutex = nullptr            ; // must be !=nullptr to lock
	MutexLvl _lvl   = MutexLvl::Unlocked ; // valid when _locked
} ;
template<class M> struct SharedLock {
	// cxtors & casts
	SharedLock() = default ;
	SharedLock (M& m) : _mutex{&m} { lock  () ; }
	~SharedLock(    )              { unlock() ; }
	// services
	void lock  () { SWEAR(!_lvl) ; _mutex->lock_shared  (_lvl) ; }
	void unlock() { SWEAR(+_lvl) ; _mutex->unlock_shared(_lvl) ; }
	// data
	M*       _mutex = nullptr            ; // must be !=nullptr to lock
	MutexLvl _lvl   = MutexLvl::Unlocked ; // valid when _locked
} ;

#ifndef NDEBUG
	struct NoLock {                 // check exclusion is guaranteed by caller
		// cxtors & casts
		NoLock() = default ;
		NoLock (NoMutex& m) : _mutex{&m} { lock  () ; }
		~NoLock(          )              { unlock() ; }
		// services
		void lock  () { _mutex->lock  (::ref(MutexLvl())) ; }
		void unlock() { _mutex->unlock(::ref(MutexLvl())) ; }
		// data
		NoMutex* _mutex = nullptr ; // must be !=nullptr to lock
	} ;
	struct NoSharedLock {           // check exclusion is guaranteed by caller
		// cxtors & casts
		NoSharedLock() = default ;
		NoSharedLock (NoMutex& m) : _mutex{&m} { lock  () ; }
		~NoSharedLock(          )              { unlock() ; }
		// services
		void lock  () { _mutex->lock_shared  (::ref(MutexLvl())) ; }
		void unlock() { _mutex->unlock_shared(::ref(MutexLvl())) ; }
		// data
		NoMutex* _mutex = nullptr ; // must be !=nullptr to lock
	} ;
#else
	struct NoLock {
		NoLock() = default ;
		NoLock(NoMutex&) {}
		void lock  () {}
		void unlock() {}
	} ;
	struct NoSharedLock {
		NoSharedLock() = default ;
		NoSharedLock(NoMutex&) {}
		void lock  () {}
		void unlock() {}
	} ;
#endif

template<class T,MutexLvl Lvl=MutexLvl::Unlocked> struct Atomic : ::atomic<T> {
	using Base = ::atomic<T> ;
	using Base::load ;
	// cxtors & casts
	using Base::Base      ;
	using Base::operator= ;
	// accesses
	auto  operator+ () const { return +load() ; }
	auto& operator* () const { return *load() ; }
	auto* operator->() const { return &*self  ; }
	// services
	void wait(T const& old) requires(bool(+Lvl)) ;
} ;
template<class T,MutexLvl Lvl> ::string& operator+=( ::string& os , Atomic<T,Lvl> const& a ) { // START_OF_NO_COV
	return os <<"Atomic("<< a.load() <<')' ;
}                                                                                              // END_OF_NO_COV

//
// Save
//

inline void fence() { ::atomic_signal_fence(::memory_order_acq_rel) ; } // ensure execution order in case of crash to guaranty disk integrity

template<class T,bool Fence=false> struct Save {
	Save ( T& ref , T const& val ) : saved{ref} , _ref{ref} { _ref =        val  ; if (Fence) fence() ;                        } // save and init, ensure sequentiality if asked to do so
	Save ( T& ref , T     && val ) : saved{ref} , _ref{ref} { _ref = ::move(val) ; if (Fence) fence() ;                        } // save and init, ensure sequentiality if asked to do so
	Save ( T& ref                ) : saved{ref} , _ref{ref} {                                                                  } // in some cases, we just want to save and restore
	~Save(                       )                          {                      if (Fence) fence() ; _ref = ::move(saved) ; } // restore      , ensure sequentiality if asked to do so
	T saved ;
private :
	T& _ref ;
} ;
template<class T> struct Save<Atomic<T>> {
	using AT = Atomic<T> ;
	Save ( AT& ref , T const& val ) : saved{ref} , _ref{ref} { _ref =        val    ; }
	Save ( AT& ref , T     && val ) : saved{ref} , _ref{ref} { _ref = ::move(val  ) ; }
	Save ( AT& ref                ) : saved{ref} , _ref{ref} {                        }
	~Save(                        )                          { _ref = ::move(saved) ; }
	T saved ;
private :
	AT& _ref ;
} ;
template<class T> using FenceSave = Save<T,true> ;

//
// SmallIds
//

template<::unsigned_integral T,bool ThreadSafe=false> struct SmallIds {
private :
	using _Mutex   = ::conditional_t< ThreadSafe , Mutex<MutexLvl::SmallId> , NoMutex > ;
	using _Lock    = ::conditional_t< ThreadSafe , Lock<_Mutex>             , NoLock  > ;
	using _AtomicT = ::conditional_t< ThreadSafe , Atomic<T>                , T       > ;
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
			auto it = free_ids.begin() ;
			res = *it ;
			free_ids.erase(it) ;
		}
		SWEAR(n_acquired<Max<T>) ;                                                  // ensure no overflow
		n_acquired++ ;                                                              // protected by _mutex
		return res ;
	}
	void release(T id) {
		if (!id) return ;                                                           // id 0 has not been acquired
		_Lock lock     { _mutex }                   ;
		bool  inserted = free_ids.insert(id).second ; SWEAR(inserted,id,free_ids) ; // else, double release
		SWEAR(n_acquired>Min<T>) ;                                                  // ensure no underflow
		n_acquired-- ;                                                              // protected by _mutex
	}
	// data
	::set<T> free_ids    ;
	T        n_allocated = 1 ;                                                      // dont use id 0 so that it is free to mean "no id"
	_AtomicT n_acquired  = 0 ;                                                      // can be freely read by any thread if ThreadSafe
private :
	_Mutex _mutex ;
} ;

//
// OrderedMap & OrderedSet
// ordered by insertion order
//

// /!\ OrderedSet is like OrderedMap without value, both codes must stay in sync
template<class K> struct OrderedSet {
	// cxtors & casts
	OrderedSet() = default ;
	operator ::vector<K>() const {
		::vector<K> res(_data.size()) ; for( auto const& [k,i] : _data ) res[i] = k ;
		return res ;
	}
	// accesses
	bool operator+(          ) const { return +_data            ; }
	bool contains (K const& k) const { return _data.contains(k) ; }
	// services
	bool push(K const& k) {                                   // store if k does not already exists
		return _data.try_emplace( k , _data.size() ).second ;
	}
	// data
private :
	::umap<K,size_t/*order*/> _data ;
} ;

// /!\ OrderedSet is like OrderedMap without value, both codes must stay in sync
template<class K,class V> struct OrderedMap {
	// cxtors & casts
	OrderedMap() = default ;
	operator ::vmap<K,V>() const {
		::vmap<K,V> res(_data.size()) ; for( auto const& [k,i_v] : _data ) res[i_v.first] = {k,i_v.second} ;
		return res ;
	}
	// accesses
	bool operator+(          ) const { return +_data            ; }
	bool contains (K const& k) const { return _data.contains(k) ; }
	// services
	bool push( K const& k , V const& v ) {                        // store if k does not already exists
		return _data.try_emplace( k , _data.size() , v ).second ;
	}
	// data
private :
	::umap<K,::pair<size_t,NoVoid<V>>> _data ;
} ;

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

enum class FdAction : uint8_t {
	Read
,	ReadNonBlock
,	ReadNoFollow
,	Dir
,	Write
,	Append
,	Create
,	CreateExe
,	CreateReadOnly
,	CreateNoFollow
,	CreateNoFollowExe
,	ReadWrite
,	CreateRead
,	CreateReadTrunc
} ;
struct Fd {
	friend ::string& operator+=( ::string& , Fd const& ) ;
	static const Fd Cwd    ;
	static const Fd Stdin  ;
	static const Fd Stdout ;
	static const Fd Stderr ;
	static const Fd Std    ;                                                           // the highest standard fd
	// cxtors & casts
private :
	static int _s_mk_fd( Fd at , ::string const& file , bool err_ok , FdAction action=FdAction::Read ) ;
public :
	constexpr Fd(                        ) = default ;
	constexpr Fd( int fd_                ) : fd{fd_} {                         }
	/**/      Fd( int fd_ , bool no_std_ ) : fd{fd_} { if (no_std_) no_std() ; }
	//
	Fd( Fd at , const char* file ) : Fd{at,::string(file)} {}                          // ensure no confusion
	Fd(         const char* file ) : Fd{   ::string(file)} {}                          // .
	//
	Fd( Fd at , ::string const& file , bool err_ok , FdAction action=FdAction::Read , bool no_std_=false ) : Fd{ _s_mk_fd(at,file,err_ok,action) , no_std_ } {}
	Fd(         ::string const& file , bool err_ok , FdAction action=FdAction::Read , bool no_std_=false ) : Fd{ Cwd , file , err_ok , action , no_std_    } {}
	Fd( Fd at , ::string const& file ,               FdAction action=FdAction::Read , bool no_std_=false ) : Fd{ at  , file , false  , action , no_std_    } {}
	Fd(         ::string const& file ,               FdAction action=FdAction::Read , bool no_std_=false ) : Fd{ Cwd , file , false  , action , no_std_    } {}
	//
	constexpr operator int  () const { return fd    ; }
	constexpr bool operator+() const { return fd>=0 ; }
	//
	void swap(Fd& fd_) { ::swap(fd,fd_.fd) ; }
	// services
	constexpr bool              operator== (Fd const&              ) const = default ;
	constexpr ::strong_ordering operator<=>(Fd const&              ) const = default ;
	/**/      void              write      (::string_view data     ) const ;           // writing does not modify the Fd object
	/**/      ::string          read       (size_t        sz  =Npos) const ;           // read sz bytes or to eof
	/**/      ::vector_s        read_lines (                       ) const ;
	/**/      size_t            read_to    (::span<char>  dst      ) const ;
	/**/      Fd                dup        (                       ) const { return ::dup(fd) ;                     }
	constexpr Fd                detach     (                       )       { Fd res = self ; fd = -1 ; return res ; }
	constexpr void              close      (                       ) ;
	/**/      void              no_std     (                       ) ;
	/**/      void              cloexec    (bool          set =true) const { ::fcntl(fd,F_SETFD,set?FD_CLOEXEC:0) ; }
	constexpr size_t            hash       (                       ) const { return fd ;                            }
protected :
	::string& append_to_str( ::string& os , const char* class_name ) const {
		os <<class_name<<"(" ;
		if (self==Cwd) os << "Cwd" ;
		else           os << fd    ;
		return os <<')' ;
	}
public :
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
	AcFd(                              ) = default ;
	AcFd( Fd fd_                       ) : Fd{fd_        } {              }
	AcFd( AcFd&& acfd                  )                   { swap(acfd) ; }
	AcFd( int fd_ , bool no_std_=false ) : Fd{fd_,no_std_} {              }
	//
	AcFd( Fd at , const char* file ) : AcFd{at,::string(file)} {} // ensure no confusion
	AcFd(         const char* file ) : AcFd{   ::string(file)} {} // .
	//
	AcFd( Fd at , ::string const& file , bool err_ok , FdAction action=FdAction::Read , bool no_std_=false ) : Fd{ at  , file , err_ok , action , no_std_   } {}
	AcFd(         ::string const& file , bool err_ok , FdAction action=FdAction::Read , bool no_std_=false ) : AcFd{ Cwd , file , err_ok , action , no_std_ } {}
	AcFd( Fd at , ::string const& file ,               FdAction action=FdAction::Read , bool no_std_=false ) : AcFd{ at  , file , false  , action , no_std_ } {}
	AcFd(         ::string const& file ,               FdAction action=FdAction::Read , bool no_std_=false ) : AcFd{ Cwd , file , false  , action , no_std_ } {}
	//
	~AcFd() { close() ; }
	//
	AcFd& operator=(int       fd_ ) { if (fd!=fd_) { close() ; fd = fd_ ; } return self ; }
	AcFd& operator=(Fd const& fd_ ) { self = fd_ .fd ;                      return self ; }
	AcFd& operator=(AcFd   && acfd) { swap(acfd) ;                          return self ; }
} ;

inline ::string file_msg( Fd at , ::string file ) {
	if      (at==Fd::Cwd) return                   file  ;
	else if (+at        ) return cat('@',at.fd,':',file) ;
	else                  return cat("@<nowhere>:",file) ;
}

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

inline ::umap_ss mk_environ() {
	::umap_ss res ;
	for( char** e=environ ; *e ; e++ )
		if ( const char* eq = ::strchr(*e,'=') )
			res[{*e,size_t(eq-*e)}] = eq+1 ;
	return res ;
}

template<class T> struct SaveInc {
	 SaveInc(T& ref) : _ref{ref} { SWEAR(_ref<Max<T>) ; _ref++ ; } // increment
	~SaveInc(      )             { SWEAR(_ref>Min<T>) ; _ref-- ; } // restore
private :
	T& _ref ;
} ;

// like a ::uniq_ptr except object is not destroyed upon destruction
// meant to be used as static variables for which destuction at end of execution is a mess because of order
template<class T,MutexLvl A=MutexLvl::Unlocked> struct StaticUniqPtr ;
//
template<class T,MutexLvl A> ::string& operator+=( ::string& os , StaticUniqPtr<T,A> const& sup ) ;
template<class T,MutexLvl A> struct StaticUniqPtr {
	friend ::string& operator+=<>( ::string& , StaticUniqPtr<T,A> const& ) ;
	template<class,MutexLvl> friend struct StaticUniqPtr ;
	// cxtors & casts
	/**/                 StaticUniqPtr() = default ;
	/**/                 StaticUniqPtr(T*                   p  )                    : _ptr{p       } {}
	/**/                 StaticUniqPtr(NewType                 )                    : _ptr{new T   } {}
	/**/                 StaticUniqPtr(StaticUniqPtr<T  >&& sup) requires(bool(+A)) : _ptr{sup._ptr} { sup._ptr = nullptr ; } // allow move as long as both are not atomic
	template<MutexLvl B> StaticUniqPtr(StaticUniqPtr<T,B>&& sup) requires(     !A ) : _ptr{sup._ptr} { sup._ptr = nullptr ; } // .
	//
	StaticUniqPtr& operator=(NewType) { self = new T ; return self ; }
	//
	StaticUniqPtr& operator=(T*              p  ) requires(!A) { { if (_ptr) delete _ptr ; } _ptr = p                    ; return self ; }
	StaticUniqPtr& operator=(StaticUniqPtr&& sup) requires(!A) { { if (_ptr) delete _ptr ; } _ptr = &*sup ; sup.detach() ; return self ; }
	//
	StaticUniqPtr& operator=(T*                 p  ) requires(bool(+A)) { T* q = _ptr.exchange(p    ) ; { if (q) delete q ; }                return self ; }
	StaticUniqPtr& operator=(StaticUniqPtr<T>&& sup) requires(bool(+A)) { T* q = _ptr.exchange(&*sup) ; { if (q) delete q ; } sup.detach() ; return self ; }
	//
	StaticUniqPtr           (StaticUniqPtr const&) = delete ;
	StaticUniqPtr& operator=(StaticUniqPtr const&) = delete ;
	// accesses
	bool     operator+ () const { return _ptr    ; }
	T      & operator* ()       { return *_ptr   ; }
	T const& operator* () const { return *_ptr   ; }
	T      * operator->()       { return &*self  ; }
	T const* operator->() const { return &*self  ; }
	void     detach    ()       { _ptr = nullptr ; }
	// data
private :
	::conditional_t<+A,Atomic<T*,A>,T*> _ptr = nullptr ;
} ;
template<class T,MutexLvl A> ::string& operator+=( ::string& os , StaticUniqPtr<T,A> const& sup ) {                           // START_OF_NO_COV
	return os << sup._ptr ;
}                                                                                                                             // END_OF_NO_COV

enum class Rc : uint8_t {
	Ok
,	Fail
,	Perm
,	Usage
,	Format
,	Param
,	System
} ;

template<class... As> [[noreturn]] inline void exit( Rc rc , As const&... args ) {
	Fd::Stderr.write(ensure_nl(cat(args...))) ;
	::std::exit(+rc) ;
}

template<class... As> void dbg( ::string const& title , As const&... args ) {
	::string msg = title             ;
	/**/  (( msg <<' '<< args ),...) ;
	/**/     msg <<'\n'              ;
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
	if (!self         ) return ;
	if (::close(fd)!=0) throw cat("cannot close fd ",fd," : ",::strerror(errno)) ;
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

inline void kill_self(int sig) {      // raise kills the thread, not the process
	int rc = ::kill(::getpid(),sig) ; // dont use kill_process as we call kill ourselves even if we are process 1 (in a namespace)
	SWEAR(rc==0,sig) ;                // killing outselves should always be ok
}

::string get_exe       () ;
::string _crash_get_now() ;

// START_OF_NO_COV for debug only
extern bool _crash_busy ;
template<class... A> [[noreturn]] void crash( int hide_cnt , int sig , A const&... args ) {
	if (!_crash_busy) {                                                                     // avoid recursive call in case syscalls are highjacked (hoping sig handler management are not)
		_crash_busy = true ;
		::string err_msg = get_exe() ;
		if (t_thread_key!='?') err_msg <<':'<< t_thread_key                   ;
		/**/                   err_msg <<" ("<<_crash_get_now()<<") :"        ;
		/**/                (( err_msg <<' '<< args                    ),...) ;
		/**/                   err_msg <<'\n'                                 ;
		Fd::Stderr.write(err_msg) ;
		set_sig_handler<SIG_DFL>(sig) ;
		write_backtrace(Fd::Stderr,hide_cnt+1) ;
		kill_self(sig) ;                                                                    // rather than merely calling abort, this works even if crash_handler is not installed
		// continue to abort in case kill did not work for some reason
	}
	set_sig_handler<SIG_DFL>(SIGABRT) ;
	::abort() ;
}
// END_OF_NO_COV

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
template<char Delimiter> ::string parse_printable( ::string const& x , size_t& pos ) {
	static_assert(_can_be_delimiter(Delimiter)) ;
	SWEAR(pos<=x.size(),x,pos) ;
	::string res ;
	const char* x_c = x.c_str() ;
	for( char c ; (c=x_c[pos]) ; pos++ )
		if      (c==Delimiter    ) break/*for*/ ;
		else if (!is_printable(c)) break/*for*/ ;
		else if (c!='\\'         ) res += c ;
		else
			switch (x_c[++pos]) {
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
					char x = 0 ; if ( char d=x_c[++pos] ; d>='0' && d<='9' ) x += d-'0' ; else if ( d>='a' && d<='f' ) x += 10+d-'a' ; else throw cat("illegal hex digit ",d) ;
					x <<= 4    ; if ( char d=x_c[++pos] ; d>='0' && d<='9' ) x += d-'0' ; else if ( d>='a' && d<='f' ) x += 10+d-'a' ; else throw cat("illegal hex digit ",d) ;
					res += x ;
				} break/*switch*/ ;
				//
				default : throw cat("illegal code \\",x_c[pos]) ;
			}
	return res ;
}

constexpr inline int8_t _unit_val(char u) {
	constexpr ::array<int8_t,256> Tab = []()->::array<int8_t,256> {
		::array<int8_t,256> res ; for( size_t i : iota(res.size()) ) res[i] = 127 ;
		res['a'] = -6 ;
		res['f'] = -5 ;
		res['p'] = -4 ;
		res['n'] = -3 ;
		res['u'] = -2 ;
		res['m'] = -1 ;
		res[0  ] =  0 ;
		res['k'] =  1 ;
		res['M'] =  2 ;
		res['G'] =  3 ;
		res['T'] =  4 ;
		res['P'] =  5 ;
		res['E'] =  6 ;
		return res ;
	}() ;
	int8_t res = Tab[uint8_t(u)] ;
	throw_if( res==127 , "unrecognized suffix ",u ) ;
	return res ;
}
template<char U,::integral I,bool RndUp> I from_string_with_unit(::string const& x) {
	double              val     ;
	const char*         x_start = x.c_str()                                             ;
	const char*         x_end   = x_start+x.size()                                      ;
	::from_chars_result fcr     = ::from_chars(x_start,x_end,val,::chars_format::fixed) ;
	//
	throw_unless( fcr.ec==::errc() , "unrecognized value "        ,x ) ;
	throw_unless( fcr.ptr>=x_end-1 , "partially recognized value ",x ) ;
	//
	val = ::ldexp( val , 10*(_unit_val(*fcr.ptr)-_unit_val(U)) ) ; // scale val to take units into account
	throw_unless( val<double(Max<I>)+1 , "overflow"  ) ;           // use </> and +/-1 to ensure conservative test for 64 bits int (as double mantissa is only 54 bits)
	throw_unless( val>double(Min<I>)-1 , "underflow" ) ;           // .
	if (RndUp) return I(::ceil (val)) ;
	else       return I(::floor(val)) ;
}

template<char U,::integral I> ::string to_string_with_unit(I x) {
	int8_t e = _unit_val(U) ;                                     // INVARIANT : value = x<<10*e
	if (x)
		while ((x&0x3ff)==0) {                                    // .
			if (e>=6) break ;
			x >>= 10 ;
			e++ ;
		}
	::string res = ::to_string(x) ;
	if (e) res += "afpnum?kMGTPE"[e+6] ;
	return res ;
}

template<char U,::integral I> ::string to_short_string_with_unit(I x) {
	static constexpr int8_t E = _unit_val(U) ;
	//
	bool   neg = x<0 ; if (neg) x = -x ;                                                         // INVARIANT : value == (neg?-1:1)*::ldexp(d,10*e)
	double d   = x   ;                                                                           // .
	int8_t e   = E   ;                                                                           // .
	while (d>=(1<<10)) {                                                                         // d can be above 999.5 as above 100, there is no decimal point and we can have 4 digits
		if (e>=6) return neg?"----":"++++" ;
		d = ::ldexp(d,-10) ;
		e++ ;
	}
	::string          res  ( 1/*neg*/+4/*d*/+1/*e*/ , 0 )               ;
	::to_chars_result tcr                                               ; tcr.ptr = res.data() ;
	int8_t            prec = e==E ? 0 : d>=99.95 ? 0 : d>=9.995 ? 1 : 2 ;                        // no frac part if result is known exact, /!\ beware of rounding to nearest
	if (neg) *tcr.ptr++ = '-'                                                                  ;
	/**/      tcr       = ::to_chars( tcr.ptr , tcr.ptr+4 , d , ::chars_format::fixed , prec ) ; SWEAR(tcr.ec==::errc()) ;
	if (e)   *tcr.ptr++ = "afpnum?kMGTPE"[e+6]                                                 ;
	res.resize(size_t(tcr.ptr-res.data())) ;
	return res ;
}

//
// Atomic
//

template<class T,MutexLvl Lvl> void Atomic<T,Lvl>::wait(T const& old) requires(bool(+Lvl)) {
	SWEAR( t_mutex_lvl<Lvl , t_mutex_lvl,Lvl ) ;
	Save sav { t_mutex_lvl , Lvl } ;
	Base::wait(old) ;
}
