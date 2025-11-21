// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "basic_utils.hh"
#include "enum.hh"

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

// START_OF_VERSIONING
// PER_FILE_SYNC : add entry here
enum class FileSync : uint8_t { // method used to ensure real close-to-open file synchronization (including file creation)
	None
,	Dir                         // close file directory after write, open it before read (in practice, open/close upon both events)
,	Sync                        // sync file after write
// aliases
,	Dflt = Dir
} ;
// END_OF_VERSIONING

// prevent dead locks by associating a level to each mutex, so we can verify the absence of dead-locks even in absence of race
// use of identifiers (in the form of an enum) allows easy identification of the origin of misorder
enum class MutexLvl : uint8_t { // identify who is owning the current level to ease debugging
	Unlocked                    // used in Lock to identify when not locked
,	None
// level 1
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
,	Workload                    // must follow Backend
// level 4
,	Autodep                     // must follow Gil
,	Gather                      // must follow Gil
,	Job                         // must follow Backend, by symetry with Node
,	Node                        // must follow NodeCrcDate
,	ReqInfo                     // must follow Req
,	Time                        // must follow BackendId
// level 5
,	Record                      // must follow Autodep
// inner (locks that take no other locks)
,	Audit
,	Codec
,	File
,	Hash
,	ReqIdx
,	Sge
,	Slurm
,	SmallId
,	Thread
// very inner
,	Trace                       // allow tracing anywhere (but tracing may call some syscall)
,	SyscallTab                  // any syscall may need this mutex, which may occur during tracing
,	PdateNew                    // may need time anywhere, even during syscall processing
} ;

enum class PermExt : uint8_t {
	None
,	Group
,	Other
} ;

enum class Rc : uint8_t {
	Ok
,	Fail
,	Internal // should not occur
//
,	BadMakefile
,	BadServer
,	BadState
,	CleanRepo
,	Perm
,	ServerCrash
,	SteadyRepo
,	System
,	Usage
,	Version
} ;

//
// basic miscellaneous
//

struct Fd ;

template<class... As> void dbg( Fd fd , ::string const& title , As const&... args ) ;
template<class... As> void dbg( NewType , ::string const& file , ::string const& title , As const&... args ) ;
template<class... As> void dbg(                                  ::string const& title , As const&... args ) ;
template<class... As> void dbg(                                  const char*     title , As const&... args ) ; // disambiguate

::string const& host() ;
::string const& mail() ; // user@host

struct StrErr {
	friend ::string& operator+=( ::string& , StrErr const& ) ;
	StrErr(int e=errno) : err{e} {}
	operator ::string() const { return ::strerror(err) ; }
	int err ;
} ;

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
template<::integral I> I from_string( const char* txt , bool empty_ok=false , bool hex=false ) { return from_string<I>( ::string_view(txt) , empty_ok , hex ) ; }
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
template<::floating_point F> F from_string( const char* txt , bool empty_ok=false ) { return from_string<F>( ::string_view(txt) , empty_ok ) ; }

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

template<char Delimiter=0> ::string mk_printable(::string const&    ) ;
template<char Delimiter=0> ::string mk_printable(::string     && txt) {
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

template<char U,::integral I=size_t,bool RndUp=false> I        from_string_with_unit    (::string const& s) ; // if U is provided, return value is expressed in this unit
template<char U,::integral I=size_t                 > ::string to_string_with_unit      (I               x) ;
template<char U,::integral I=size_t                 > ::string to_short_string_with_unit(I               x) ;
template<       ::integral I=size_t,bool RndUp=false> I        from_string_with_unit    (::string const& s) { return from_string_with_unit    <0,I,RndUp>(s) ; }
template<       ::integral I=size_t                 > ::string to_string_with_unit      (I               x) { return to_string_with_unit      <0,I      >(x) ; }
template<       ::integral I=size_t                 > ::string to_short_string_with_unit(I               x) { return to_short_string_with_unit<0,I      >(x) ; }

//
// span
//

using span_s = ::span<::string> ;

//
// math
//

template<size_t D,::unsigned_integral I> constexpr I round_down(I n) { return     n   - I( n   %D)         ; }
template<size_t D,::unsigned_integral I> constexpr I round_up  (I n) { return n ? n-1 - I((n-1)%D) + D : 0 ; }
template<size_t D,::unsigned_integral I> constexpr I div_up    (I n) { return n ?       I(n-1)/D   + 1 : 0 ; } // (n+D-1)/D does not work when n is close to max value

template<::unsigned_integral I> constexpr I round_down( I n , I d ) { return     n   - I( n   %d)         ; }
template<::unsigned_integral I> constexpr I round_up  ( I n , I d ) { return n ? n-1 - I((n-1)%d) + d : 0 ; }
template<::unsigned_integral I> constexpr I div_up    ( I n , I d ) { return n ?       I(n-1)/d   + 1 : 0 ; } // (n+d-1)/d does not work when n is close to max value

static constexpr double Infinity = ::numeric_limits<double>::infinity () ;
static constexpr double Nan      = ::numeric_limits<double>::quiet_NaN() ;

//
// mutexes
//

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
		void lock         (MutexLvl&) { char b = _busy.exchange(t_thread_key) ; SWEAR(!b    ,b    ) ; }
		void lock_shared  (MutexLvl&) {                                         SWEAR(!_busy,_busy) ; }
		void unlock       (MutexLvl&) {          _busy = 0                    ;                       }
		void unlock_shared(MutexLvl&) {                                         SWEAR(!_busy,_busy) ; }
		void swear_locked (         ) {                                         SWEAR( _busy,_busy) ; }
	private :
		::atomic<char> _busy { 0 } ;
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
inline ::string const& no_empty_s(::string const& dir_s) {
	if (+dir_s)                                  return dir_s ;
	else        { static ::string dot_s = "./" ; return dot_s ; }
}

struct NfsGuard ;
//
template<class F> struct _File ;
template<class F> ::string& operator+=( ::string& os , _File<F> const& fs ) ;
using File     = _File<::string       > ;
using FileRef  = _File<::string const&> ;
using FileView = _File<::string_view  > ;

struct _FdAction {
	int       flags     = O_RDONLY ;
	mode_t    mod       = -1       ;                                                         // default to an invalid mod (0 may be usefully used to create a no-access file)
	bool      err_ok    = false    ;
	bool      mk_dir    = true     ;
	bool      force     = false    ;                                                         // unlink any file on path to file if necessary
	PermExt   perm_ext  = {}       ;
	bool      no_std    = false    ;
	NfsGuard* nfs_guard = nullptr  ;
} ;
struct Fd {
	friend ::string& operator+=( ::string& , Fd const& ) ;
	//
	static const Fd Cwd    ;
	static const Fd Stdin  ;
	static const Fd Stdout ;
	static const Fd Stderr ;
	static const Fd Std    ;                                                                 // the highest standard fd
	//
	using Action = _FdAction ;
	// cxtors & casts
private :
	static int _s_mk_fd( FileRef file , Action action ) ;
public :
	constexpr Fd() = default ;
	constexpr Fd( int fd_                ) : fd{fd_} {                         }
	/**/      Fd( int fd_ , bool no_std_ ) : fd{fd_} { if (no_std_) no_std() ; }
	//
	Fd( FileRef     file , Action action={} ) ;
	Fd( const char* file , Action action={} ) ;
	//
	constexpr operator int  () const { return fd                    ; }
	constexpr bool operator+() const { return fd>=0 || fd==AT_FDCWD ; }                      // other negative values are used to spawn processes
	//
	void swap(Fd& fd_) { ::swap(fd,fd_.fd) ; }
	// services
	constexpr bool              operator== (Fd const&                    ) const = default ;
	constexpr ::strong_ordering operator<=>(Fd const&                    ) const = default ;
	/**/      void              write      (::string_view data           ) const ;           // writing does not modify the Fd object
	/**/      ::string          read       (size_t        sz        =Npos) const ;           // read sz bytes or to eof
	/**/      ::vector_s        read_lines (bool          partial_ok=true) const ;           // if partial_ok => ok if last line does not end with \n
	/**/      size_t            read_to    (::span<char>  dst            ) const ;
	/**/      Fd                dup        (                             ) const { return ::dup(fd) ;                     }
	constexpr Fd                detach     (                             )       { Fd res = self ; fd = -1 ; return res ; }
	constexpr void              close      (                             )       ;
	/**/      bool              is_std     (                             ) const { return fd>=0 && fd<Std.fd            ; }
	/**/      void              no_std     (                             )       ;
	/**/      void              cloexec    (bool          set       =true) const { ::fcntl(fd,F_SETFD,set?FD_CLOEXEC:0) ; }
	constexpr size_t            hash       (                             ) const { return fd ;                            }
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
	AcFd() = default ;
	AcFd( Fd fd_                       ) : Fd{fd_        } {              }
	AcFd( AcFd&& acfd                  )                   { swap(acfd) ; }
	AcFd( int fd_ , bool no_std_=false ) : Fd{fd_,no_std_} {              }
	//
	AcFd( FileRef     file , Action action={} ) ;
	AcFd( const char* file , Action action={} ) ;
	//
	~AcFd() { close() ; }
	//
	AcFd& operator=(int       fd_ ) { if (fd!=fd_) { close() ; fd = fd_ ; } return self ; }
	AcFd& operator=(Fd const& fd_ ) { self = fd_.fd ;                       return self ; }
	AcFd& operator=(AcFd   && acfd) { swap(acfd) ;                          return self ; }
} ;

template<class F> struct _File {
	friend ::string& operator+=<>( ::string& , _File<F> const& ) ;
	static constexpr bool IsStr  = ::is_same_v<F,::string       > ;
	static constexpr bool IsRef  = ::is_same_v<F,::string const&> ;
	static constexpr bool IsView = ::is_same_v<F,::string_view  > ;
private :
	static F _s_dflt_file() requires(IsStr ) {                         return {}    ; }
	static F _s_dflt_file() requires(IsRef ) { static ::string s_str ; return s_str ; } // although documented as constexpr constructible, ::string constants may not be available before main started
	static F _s_dflt_file() requires(IsView) { static ::string s_str ; return s_str ; } // .
	// cxtors & casts
public :
	_File( Fd at_ , F        const& file_=_s_dflt_file() )                   : at{at_    } , file{       file_ } {}
	_File(          F        const& file_=_s_dflt_file() )                   : at{Fd::Cwd} , file{       file_ } {}
	_File( Fd at_ , ::string const& file_                ) requires( IsView) : at{at_    } , file{       file_ } {}
	_File(          ::string const& file_                ) requires( IsView) : at{Fd::Cwd} , file{       file_ } {}
	_File( Fd at_ , F            && file_                ) requires( IsStr ) : at{at_    } , file{::move(file_)} {}
	_File(          F            && file_                ) requires( IsStr ) : at{Fd::Cwd} , file{::move(file_)} {}
	_File( Fd at_ , const char*     file_                ) requires(!IsRef ) : at{at_    } , file{       file_ } {} // disambiguate
	_File(          const char*     file_                ) requires(!IsRef ) : at{Fd::Cwd} , file{       file_ } {} // .
	_File( Fd at_ , const char*     file_                ) requires( IsRef ) = delete ;                             // .
	_File(          const char*     file_                ) requires( IsRef ) = delete ;                             // .
	//
	template<class F2> _File(_File<F2> const& f2)                 : at{f2.at} , file{       f2.file } {}
	template<class F2> _File(_File<F2>     && f2) requires(IsStr) : at{f2.at} , file{::move(f2.file)} {}
	//
	template<class F2> _File& operator=(F2 const& f2)                 { at = f2.at ; file =        f2.file  ; return self ; }
	template<class F2> _File& operator=(F2     && f2) requires(IsStr) { at = f2.at ; file = ::move(f2.file) ; return self ; }
	//
	// accesses
	bool operator+() const { return +at ; }
	// services
	template<class F2> bool operator==(_File<F2> const& f2) const { return at==f2.at && file==f2.file ; }
	size_t hash() const ;
	// data
	Fd at   ;
	F  file ;
} ;
template<class F> ::string& operator+=( ::string& os , _File<F> const& f ) {
	if (f.at!=Fd::Cwd) {
		if (+f.at) os <<'<'<< f.at.fd <<">/" ;
		else       os <<"<>/"                ;
	}
	return os << f.file ;
}
struct NfsGuardDir {                                              // open/close uphill dirs before read accesses and after write accesses
	// cxtors & casts
	~NfsGuardDir() { flush() ; }
	//services
	void access      (FileRef path ) ;
	void access_dir_s(FileRef dir_s) ;
	void change      (FileRef path ) ;
	void flush       (             ) ;
	// data
	::uset<File> fetched_dirs_s  ;
	::uset<File> to_stamp_dirs_s ;
} ;
struct NfsGuardSync {                                             // fsync file after they are written
	// cxtors & casts
	~NfsGuardSync() { flush() ; }
	//services
	void access      (FileRef     ) {                          }
	void access_dir_s(FileRef     ) {                          }
	void change      (FileRef path) { to_stamp.emplace(path) ; }
	void flush() {
		for( auto const& f : to_stamp ) if ( AcFd fd{f,{.err_ok=true}} ; +fd ) ::fsync(fd) ;
		to_stamp.clear() ;
	}
	// data
	::uset<File> to_stamp ;
} ;
struct NfsGuard : ::variant< ::monostate , NfsGuardDir , NfsGuardSync > {
	// cxtors & casts
	constexpr NfsGuard(FileSync fs=FileSync::None) {
		switch (fs) {                                             // PER_FILE_SYNC : add entry here
			case FileSync::None :                break ;
			case FileSync::Dir  : emplace<1>() ; break ;
			case FileSync::Sync : emplace<2>() ; break ;
		DF}                                                       // NO_COV
	}
	// accesses
	FileSync file_sync() const {
		switch (index()) {                                        // PER_FILE_SYNC : add entry here
			case 0 : return FileSync::None ;
			case 1 : return FileSync::Dir  ;
			case 2 : return FileSync::Sync ;
		DF}                                                       // NO_COV
	}
	// services
	FileRef access(FileRef path) {                                // return file, must be called before any access to file or its inode if not sure it was produced locally
		switch (index()) {                                        // PER_FILE_SYNC : add entry here
			case 0 :                               break ;
			case 1 : ::get<1>(self).access(path) ; break ;
			case 2 : ::get<2>(self).access(path) ; break ;
		DF}                                                       // NO_COV
		return path ;
	}
	FileRef access_dir_s(FileRef dir_s) {                         // return file, must be called before any access to file or its inode if not sure it was produced locally
		switch (index()) {                                        // PER_FILE_SYNC : add entry here
			case 0 :                                      break ;
			case 1 : ::get<1>(self).access_dir_s(dir_s) ; break ;
			case 2 : ::get<2>(self).access_dir_s(dir_s) ; break ;
		DF}                                                       // NO_COV
		return dir_s ;
	}
	FileRef change(FileRef path) {                                // return file, must be called before any write access to file or its inode if not sure it is going to be read only locally
		switch (index()) {                                        // PER_FILE_SYNC : add entry here
			case 0 :                               break ;
			case 1 : ::get<1>(self).change(path) ; break ;
			case 2 : ::get<2>(self).change(path) ; break ;
		DF}                                                       // NO_COV
		return path ;
	}
	FileRef update(FileRef path) {
		access(path) ;
		change(path) ;
		return path ;
	}
	void flush() {
		switch (index()) {                                        // PER_FILE_SYNC : add entry here
			case 0 :                          break ;
			case 1 : ::get<1>(self).flush() ; break ;
			case 2 : ::get<2>(self).flush() ; break ;
		DF}                                                       // NO_COV
	}
} ;

//
// NfsGuardLock
//

// several methods are implemented :
// - fd based
//   - using fcntl
//   - using flock
// - file based
//   - using O_CREAT|O_EXCL options in open
//   - creating a tmp file and using link to create the lock
//   - using mkdir
// dont know yet which are faster and which actually work even under heavy loads

struct _NfsGuardLockAction {
	bool    err_ok   = false ; // ok if the lock cannot be created
	bool    mk_dir   = true  ; // create dir chain as needed
	PermExt perm_ext = {}    ; // used for fd based to ensure lock file can be adequately accessed
} ;

//
// fd based locks
//
struct _LockerFd {
	using Action = _NfsGuardLockAction ;
	// cxtors & casts
	_LockerFd( FileRef f , Action a={} ) : fd{ f , {.flags=O_RDWR|O_CREAT,.mod=0666,.err_ok=a.err_ok,.mk_dir=a.mk_dir,.perm_ext=a.perm_ext} } {}
	// accesses
	bool operator+() const { return +fd ; }
	// services
	void lock      () = delete ; // must be provided by child
	void keep_alive() {}
	// data
	AcFd fd ;
} ;
//
struct _LockerFcntl : _LockerFd { using _LockerFd::_LockerFd ; void lock() ; } ;
struct _LockerFlock : _LockerFd { using _LockerFd::_LockerFd ; void lock() ; } ;
//
template<class Locker> struct _FileLockFd : Locker {
	using typename Locker::Action ;
	using          Locker::lock   ;
	using          Locker::fd     ;
	// cxtors & casts
	_FileLockFd() = default ;
	_FileLockFd( FileRef f , Action a={} ) : Locker{f,a} {
		if (!fd) return ;
		lock() ;
	}
} ;

//
// file based locks
//
enum class _FileLockFileTrial {
	Locked
,	NoDir
,	Retry
,	Fail
} ;
//
struct _LockerFile {
	using Action = _NfsGuardLockAction ;
	using Trial  = _FileLockFileTrial  ;
	// cxtors & casts
	_LockerFile(FileRef) ;
	// accesses
	bool operator+() const { return +spec ; }
	// services
	Trial try_lock  (                                       ) = delete ; // must be provided by child
	void  unlock    ( bool err_ok=false , bool is_dir=false ) ;
	void  keep_alive(                                       ) ;
	// data
	File     spec ;
	uint64_t date ;                                                      // cant use Pdate here
} ;
struct _LockerLink    : _LockerFile { using _LockerFile::_LockerFile ; Trial try_lock() ; ::string tmp ; bool has_tmp=false ; } ;
struct _LockerExcl    : _LockerFile { using _LockerFile::_LockerFile ; Trial try_lock() ;                                     } ;
struct _LockerSymlink : _LockerFile { using _LockerFile::_LockerFile ; Trial try_lock() ;                                     } ;
struct _LockerMkdir   : _LockerFile { using _LockerFile::_LockerFile ; Trial try_lock() ; void unlock(bool err_ok=false) ;    } ;
//
template<class Locker> struct _FileLockFile : Locker {
	using typename Locker::Action   ;
	using          Locker::try_lock ;
	using          Locker::spec     ;
	using          Locker::unlock   ;
	// cxtors & casts
	_FileLockFile( FileRef , Action ) ;
	~_FileLockFile() {
		if (!spec.at) return ;
		unlock() ;
	}
} ;

//
// actual class to be used
//
using _FileLock =
	::variant<
		::monostate                                                              // /!\ fake locks, for experimental purpose only
	,	_FileLockFd  <_LockerFcntl  >
	,	_FileLockFd  <_LockerFlock  >
	,	_FileLockFile<_LockerExcl   >
	,	_FileLockFile<_LockerSymlink>
	,	_FileLockFile<_LockerLink   >
	,	_FileLockFile<_LockerMkdir  >
	>
;
struct NfsGuardLock : _FileLock , NfsGuard                                       // NfsGuard must follow _FileLock so that it is flushed before lock is released upon destruction
{
	using Action = _NfsGuardLockAction ;
	NfsGuardLock( FileSync , FileRef , Action={} ) ;
	bool operator+() const {
		switch (_FileLock::index()) {                                            // PER_FILE_SYNC : add entry here
			case 0 : return true                                         ;       // NO_COV, pretend lock is taken
			case 1 : return +get<1>(static_cast<_FileLock const&>(self)) ;
			case 2 : return +get<2>(static_cast<_FileLock const&>(self)) ;
			case 3 : return +get<3>(static_cast<_FileLock const&>(self)) ;
			case 4 : return +get<4>(static_cast<_FileLock const&>(self)) ;
			case 5 : return +get<5>(static_cast<_FileLock const&>(self)) ;
			case 6 : return +get<6>(static_cast<_FileLock const&>(self)) ;
		DF}                                                                      // NO_COV
	}
	void keep_alive() {
		switch (_FileLock::index()) {                                            // PER_FILE_SYNC : add entry here
			case 0 : return                                                    ; // NO_COV
			case 1 : return get<1>(static_cast<_FileLock&>(self)).keep_alive() ;
			case 2 : return get<2>(static_cast<_FileLock&>(self)).keep_alive() ;
			case 3 : return get<3>(static_cast<_FileLock&>(self)).keep_alive() ;
			case 4 : return get<4>(static_cast<_FileLock&>(self)).keep_alive() ;
			case 5 : return get<5>(static_cast<_FileLock&>(self)).keep_alive() ;
			case 6 : return get<6>(static_cast<_FileLock&>(self)).keep_alive() ;
		DF}                                                                      // NO_COV
	}
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

template<class... As> [[noreturn]] void exit( Rc rc , As const&... args ) {
	Fd::Stderr.write(ensure_nl(cat(args...))) ;
	::std::exit(+rc) ;
}

template<class... As> void dbg( Fd fd , ::string const& title , As const&... args ) {
	::string msg = title             ;
	/**/  (( msg <<' '<< args ),...) ;
	/**/     msg <<'\n'              ;
	{ Save sav_errno{errno} ; fd.write(msg) ; }
}
template<class... As> void dbg( ::string const& title , As const&... args ) { dbg( Fd::Stderr , title , args... ) ; }
template<class... As> void dbg( const char*     title , As const&... args ) { dbg( Fd::Stderr , title , args... ) ; } // disambiguate
template<class... As> void dbg( NewType , ::string const& file , ::string const& title , As const&... args ) {
	Save sav_errno { errno } ;
	dbg( AcFd(file,{O_WRONLY|O_APPEND|O_CREAT,0666}) , title , args... ) ;
}

template<::integral I> I random() {
	::string buf_char = AcFd("/dev/urandom").read(sizeof(I)) ; SWEAR(buf_char.size()==sizeof(I),buf_char.size()) ;
	I        buf_int  ;                                        ::memcpy( &buf_int , buf_char.data() , sizeof(I) ) ;
	return buf_int ;
}

// use constexpr processing rather than #ifdef/#endif so as to discriminate between defined/undefined macros on a single line
template<::integral I,I DfltVal> consteval I _macro_val_str(const char* n_str) {
	I    n      = 0     ;
	bool is_neg = false ;
	if (*n_str=='-') {
		is_neg = true ;
		n_str++ ;
	}
	for( const char* p=n_str ; *p ; p++ )
		if      (!( '0'<=*p && *p<='9'  )) return DfltVal ;
		else if (   ::is_same_v<I,bool>  ) n |= *p!='0'           ;
		else                               n  = (n*10) + (*p-'0') ;
	if ( !::is_same_v<I,bool> && is_neg ) n = -n ;
	return n ;
}
#define _MACRO_VAL_STR(x         ) #x                                                         // indirect macro to ensure we get the defined value when arg to MACRO_VAL is a defined macro
#define MACRO_VAL(     macro,dflt) _macro_val_str<decltype(dflt),dflt>(_MACRO_VAL_STR(macro))

//
// Implementation
//

//
// Fd
//

inline Fd  ::Fd  ( FileRef file , Action action ) : Fd{ _s_mk_fd(file,action) , action.no_std } {}
inline AcFd::AcFd( FileRef file , Action action ) : Fd{ file , action                         } {}

inline Fd  ::Fd  ( const char* file , Action action ) : Fd  {File(file),action} {}
inline AcFd::AcFd( const char* file , Action action ) : AcFd{File(file),action} {}

inline constexpr void Fd::close() {
	if (!self         ) return ;
	if (::close(fd)!=0) throw cat("cannot close fd ",fd," : ",::strerror(errno)) ;
	self = {} ;
}

inline void Fd::no_std() {
	if (!is_std()) return ;
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
	SWEAR( rc==0 , sig ) ;            // killing outselves should always be ok
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
			case 0    : res += "\\0"  ; break ;
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
template<char Delimiter> ::string parse_printable( ::string const& x , size_t&/*inout*/ pos ) {
	static_assert(_can_be_delimiter(Delimiter)) ;
	SWEAR( pos<=x.size() , x,pos ) ;
	::string res ;
	const char* x_c = x.c_str() ;
	for( char c ; (c=x_c[pos]) ; pos++ )
		if      (c==Delimiter    ) break/*for*/ ;
		else if (!is_printable(c)) break/*for*/ ;
		else if (c!='\\'         ) res += c ;
		else {
			pos++ ;
			throw_unless( pos<x.size() , "\\ at end of string" ) ;
			switch (x_c[pos]) {
				case '0'  : res += char(0   ) ; break/*switch*/ ;
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
					pos += 2 ;
					throw_unless( pos<x.size() , "\\x near end of string" ) ;
					char x = 0 ; if ( char d=x_c[pos-1] ; d>='0' && d<='9' ) x += d-'0' ; else if ( d>='a' && d<='f' ) x += 10+d-'a' ; else throw cat("illegal \\x hex digit ",d) ;
					x <<= 4    ; if ( char d=x_c[pos  ] ; d>='0' && d<='9' ) x += d-'0' ; else if ( d>='a' && d<='f' ) x += 10+d-'a' ; else throw cat("illegal \\x hex digit ",d) ;
					res += x ;
				} break/*switch*/ ;
				//
				default : throw cat("illegal \\",x_c[pos]) ;
			}
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
