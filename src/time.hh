// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/stat.h> // struct stat

#include <cmath>
#include <ctime>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>

#include "utils.hh"

// FileTag is defined here as it is used for Ddate and disk.hh includes this file anyway
ENUM_1( FileTag
,	Target = Lnk // >=Target means file can be generated as a target
,	None
,	Dir
,	Lnk
,	Reg          // >=Reg means file is a regular file
,	Empty        // empty and not executable
,	Exe          // a regular file with exec permission
)

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

namespace Time {

	struct Delay       ;
	struct CoarseDelay ;
	struct Date        ;
	struct Ddate       ;
	struct Pdate       ;

	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase {
		friend CoarseDelay ;
		static constexpr T    TicksPerSecond = 1'000'000'000l                 ; // if modified some methods have to be rewritten, as indicated by static asserts
		static constexpr bool IsUnsigned     = ::is_unsigned_v<T>             ;
		static constexpr bool IsNs           = TicksPerSecond==1'000'000'000l ;
		//
		using Tick     = T                                            ;
		using Tick32   = ::conditional_t<IsUnsigned,uint32_t,int32_t> ;
		using TimeSpec = struct ::timespec                            ;
		using TimeVal  = struct ::timeval                             ;
		// cxtors & casts
		constexpr          TimeBase(                  )                      = default ;
		constexpr explicit TimeBase(int             v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(long            v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(uint            v ) requires(IsUnsigned) : _val{   v*TicksPerSecond                           } {                                     }
		constexpr explicit TimeBase(ulong           v ) requires(IsUnsigned) : _val{   v*TicksPerSecond                           } {                                     }
		constexpr explicit TimeBase(double          v )                      : _val{ T(v*TicksPerSecond)                          } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(float           v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(TimeSpec const& ts)                      : _val{   ts.tv_sec*TicksPerSecond + ts.tv_nsec      } { static_assert(IsNs) ; if (IsUnsigned) SWEAR(ts.tv_sec>=0) ; }
		constexpr explicit TimeBase(TimeVal  const& tv)                      : _val{   tv.tv_sec*TicksPerSecond + tv.tv_usec*1000 } { static_assert(IsNs) ; if (IsUnsigned) SWEAR(tv.tv_sec>=0) ; }
		constexpr explicit TimeBase(NewType,Tick    v )                      : _val{   v                                          } {}
		constexpr explicit operator TimeSpec() const { TimeSpec ts{ .tv_sec=time_t(sec()) , .tv_nsec=nsec_in_s() } ; return ts                          ; }
		constexpr explicit operator TimeVal () const { TimeVal  tv{ .tv_sec=time_t(sec()) , .tv_usec=usec_in_s() } ; return tv                          ; }
		constexpr explicit operator double  () const {                                                               return double(_val)/TicksPerSecond ; }
		constexpr explicit operator float   () const {                                                               return float (_val)/TicksPerSecond ; }
		// accesses
		constexpr bool operator+() const { return _val ; }
		constexpr Tick val      () const { return _val ; }
		//
		constexpr Tick   sec      () const {                       return _val       /TicksPerSecond ; }
		constexpr Tick   msec     () const {                       return nsec     ()/1000'000       ; }
		constexpr Tick32 msec_in_s() const {                       return nsec_in_s()/1000'000       ; }
		constexpr Tick   usec     () const {                       return nsec     ()/1000           ; }
		constexpr Tick32 usec_in_s() const {                       return nsec_in_s()/1000           ; }
		constexpr Tick   nsec     () const { static_assert(IsNs) ; return _val                       ; }
		constexpr Tick32 nsec_in_s() const { static_assert(IsNs) ; return _val%TicksPerSecond        ; }
		//
		void clear() { _val = 0 ; }
		// data
	protected :
		T _val = 0 ;
	} ;

	struct Delay : TimeBase<int64_t> {
		using Base = TimeBase<int64_t> ;
		friend ::string& operator+=( ::string& , Delay const ) ;
		friend Date  ;
		friend Ddate ;
		friend Pdate ;
		friend CoarseDelay ;
		static constexpr size_t ShortStrSz = 6 ;
		static const     Delay  Lowest     ;
		static const     Delay  Highest    ;
		static const     Delay  Forever    ;
		// statics
	private :
		static bool/*slept*/ _s_sleep( ::stop_token tkn , Delay sleep , Pdate until , bool flush=true ) ; // if flush, consider we slept if asked to stop but we do not have to wait
		// cxtors & casts
	public :
		using Base::Base ;
		constexpr Delay(Base v) : Base(v) {}
		operator ::chrono::nanoseconds() const { return ::chrono::nanoseconds(nsec()) ; }
		// services
		constexpr bool              operator== (Delay const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Delay const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Delay  operator- (           ) const {                     return Delay(New,-_val          ) ; }
		constexpr Delay  operator+ (Delay other) const {                     return Delay(New,_val+other._val) ; }
		constexpr Delay  operator- (Delay other) const {                     return Delay(New,_val-other._val) ; }
		constexpr Delay& operator+=(Delay other)       { self = self+other ; return self                       ; }
		constexpr Delay& operator-=(Delay other)       { self = self-other ; return self                       ; }
		constexpr Date   operator+ (Date       ) const ;
		//
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay  operator* (T f) const ;
		template<class T> requires(::is_signed_v    <T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_unsigned_v  <T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator*=(T f)       { self = self*f ; return self ; }
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator/=(T f)       { self = self/f ; return self ; }
		//
		constexpr Delay round_sec () const {                       return Delay( New , _val-_val% TicksPerSecond           ) ; }
		constexpr Delay round_msec() const { static_assert(IsNs) ; return Delay( New , _val-_val%(TicksPerSecond/1000    ) ) ; }
		constexpr Delay round_usec() const { static_assert(IsNs) ; return Delay( New , _val-_val%(TicksPerSecond/1000'000) ) ; }
		//
		bool/*slept*/ sleep_for( ::stop_token , bool flush=true ) const ;                                 // if flush, consider we slept if asked to stop but we do not have to wait
		void          sleep_for(                                ) const ;
		//
		::string str      (uint8_t prec=0) const ;
		::string short_str(              ) const ;
		size_t   hash     (              ) const { return _val ; }
	} ;
	constexpr Delay Delay::Lowest  { New , Min<Tick> } ;
	constexpr Delay Delay::Highest { New , Max<Tick> } ;
	constexpr Delay Delay::Forever { New , Max<Tick> } ;

	// short float representation of time (positive)
	// when exp<=0, representation is linear after TicksPerSecond
	// else       , it is floating point
	struct CoarseDelay {
		friend ::string& operator+=( ::string& , CoarseDelay const ) ;
		using Val = uint16_t ;
		static constexpr int64_t  TicksPerSecond = 1000  ; // this may be freely modified
		static constexpr uint8_t  Mantissa       = 11    ; // .
		static constexpr uint32_t Scale          = 28294 ; // (::logf(Delay::TicksPerSecond)-::logf(TicksPerSecond))*(1<<Mantissa) ;
		//
		static const CoarseDelay Lowest  ;
		static const CoarseDelay Highest ;
		// statics
	private :
		static constexpr Val _Factor(uint32_t percent) { return (1<<Mantissa)*percent/100 ; }
		// cxtors & casts
		explicit constexpr CoarseDelay( NewType , Val v ) : _val{v} {}
	public :
		constexpr CoarseDelay(       ) = default ;
		constexpr CoarseDelay(Delay d) {
			uint32_t t = ::logf(d._val)*(1<<Mantissa)+0.5 ;
			if      ( t >= (1<<NBits<Val>)+Scale ) _val = -1      ;
			else if ( t <                  Scale ) _val =  0      ;
			else                                   _val = t-Scale ;
		}
		constexpr operator Delay() const {
			if (!_val) return Delay() ;
			else       return Delay(New,int64_t(::expf(float(_val+Scale)/(1<<Mantissa)))) ;
		}
		constexpr explicit operator double() const { return double(Delay(self)) ; }
		constexpr explicit operator float () const { return float (Delay(self)) ; }
		// accesses
		constexpr bool operator+() const { return _val ; }
		//
		constexpr Delay::Tick   sec      () const { return Delay(self).sec      () ; }
		constexpr Delay::Tick   nsec     () const { return Delay(self).nsec     () ; }
		constexpr Delay::Tick32 nsec_in_s() const { return Delay(self).nsec_in_s() ; }
		constexpr Delay::Tick   usec     () const { return Delay(self).usec     () ; }
		constexpr Delay::Tick32 usec_in_s() const { return Delay(self).usec_in_s() ; }
		constexpr Delay::Tick   msec     () const { return Delay(self).msec     () ; }
		constexpr Delay::Tick32 msec_in_s() const { return Delay(self).msec_in_s() ; }
		// services
		constexpr CoarseDelay       operator+  (Delay              d) const { return Delay(self) + d ;        }
		constexpr CoarseDelay&      operator+= (Delay              d)       { self = self + d ; return self ; }
		constexpr bool              operator== (CoarseDelay const& d) const { return _val== d._val ;          }
		constexpr ::strong_ordering operator<=>(CoarseDelay const& d) const { return _val<=>d._val ;          }
		//
		CoarseDelay scale_up  (uint32_t percent) const { return CoarseDelay( New , _val>=Val(-1)-_Factor(percent) ? Val(-1) : Val(_val+_Factor(percent)) ) ; }
		CoarseDelay scale_down(uint32_t percent) const { return CoarseDelay( New , _val<=        _Factor(percent) ? Val( 0) : Val(_val-_Factor(percent)) ) ; }
		//
		::string short_str() const { return Delay(self).short_str() ; }
		size_t   hash     () const { return _val                    ; }
		// data
	private :
		Val _val = 0 ;
	} ;
	constexpr CoarseDelay CoarseDelay::Lowest  { New , Val(1)   } ;
	constexpr CoarseDelay CoarseDelay::Highest { New , Max<Val> } ;

}

//
// mutexes
//

extern thread_local MutexLvl t_mutex_lvl ;
template<MutexLvl Lvl_,bool S=false/*shared*/> struct Mutex : ::conditional_t<S,::shared_timed_mutex,::timed_mutex> {
	using Base =                                              ::conditional_t<S,::shared_timed_mutex,::timed_mutex> ;
	static constexpr MutexLvl Lvl = Lvl_ ;
	// services
	void lock(MutexLvl& lvl) {
		SWEAR( t_mutex_lvl<Lvl , t_mutex_lvl,Lvl ) ;
		Base::lock() ;
		lvl         = t_mutex_lvl ;
		t_mutex_lvl = Lvl         ;
	}
	void unlock(MutexLvl lvl) {
		SWEAR( t_mutex_lvl==Lvl , t_mutex_lvl,Lvl ) ;
		t_mutex_lvl = lvl ;
		Base::unlock() ;
	}
	void lock_shared(MutexLvl& lvl) requires(S) {
		SWEAR( t_mutex_lvl<Lvl , t_mutex_lvl,Lvl ) ;
		Base::lock_shared() ;
		lvl         = t_mutex_lvl ;
		t_mutex_lvl = Lvl         ;
	}
	void unlock_shared(MutexLvl lvl) requires(S) {
		SWEAR( t_mutex_lvl==Lvl , t_mutex_lvl,Lvl ) ;
		t_mutex_lvl = lvl ;
		Base::unlock_shared() ;
	}
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
	Lock (    ) = default ;
	Lock (M& m) : _mutex{&m} {              lock  () ; }
	~Lock(    )              { if (_locked) unlock() ; }
	// services
	void lock  () requires(!S) { SWEAR(!_locked) ; _locked = true  ; _mutex->lock         (_lvl) ; }
	void lock  () requires( S) { SWEAR(!_locked) ; _locked = true  ; _mutex->lock_shared  (_lvl) ; }
	void unlock() requires(!S) { SWEAR( _locked) ; _locked = false ; _mutex->unlock       (_lvl) ; }
	void unlock() requires( S) { SWEAR( _locked) ; _locked = false ; _mutex->unlock_shared(_lvl) ; }
	// data
	M*       _mutex = nullptr                   ; // must be !=nullptr when _locked
	MutexLvl _lvl   = MutexLvl::None/*garbage*/ ; // valid when _locked
	bool    _locked = false                     ;
} ;

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

namespace Time {

	struct Date : TimeBase<uint64_t> {
		using Base = TimeBase<uint64_t> ;
		friend ::string& operator+=( ::string& , Date const ) ;
		friend Delay ;
		friend Ddate ;
		friend Pdate ;
		// cxtors & casts
		using Base::Base ;
		Date(::string_view) ; // read a reasonable approximation of ISO8601
		// services
		using Base::operator+ ;
		constexpr Date  operator+ (Delay other) const {                     return Date(New,_val+other._val) ; }
		constexpr Date  operator- (Delay other) const {                     return Date(New,_val-other._val) ; }
		constexpr Date& operator+=(Delay other)       { self = self+other ; return self                      ; }
		constexpr Date& operator-=(Delay other)       { self = self-other ; return self                      ; }
		//
		::string str    ( uint8_t prec=0 , bool in_day=false ) const ;
		::string day_str(                                    ) const ;
		size_t   hash   (                                    ) const { return _val ; }
	} ;

	//
	// We implement a complete separation between wall-clock time (Pdate) a short for process date) and time seen from the disk (Ddate) which may be on a server with its own view of time.
	// Care has been taken so that you cannot compare and more generally inter-operate between these 2 times.
	// Getting current Pdate-time is very cheap (few ns), so no particular effort is made to cache or otherwise optimize it.
	// But it is the contrary for Ddate current time : you must create or write to a file, very expensive (some fraction of ms).
	// So we keep a lazy evaluated cached value that is refreshed once per loop (after we have waited) in each thread :
	// - in terms of precision, this is enough, we just want correct relative order
	// - in terms of cost, needing current disk time is quite rare (actually, we just need it to put a date on when a file is known to not exist, else we have a file date)
	// - so in case of exceptional heavy use, cached value is used and in case of no use, we do not pay at all.
	//

	struct Pdate : Date {
		friend ::string& operator+=( ::string& , Pdate const ) ;
		friend Delay ;
		static const Pdate Future ;
		// static data
	private :
		#if __cplusplus<202600L
			static Mutex<MutexLvl::PdateNew> _s_mutex_new ; // ensure serialization to _s_last before c++26
			static Tick                      _s_min_next  ; // time returned by last call, used to ensure strict monotonicity on systems that have unreliable clocks
		#else
			static ::atomic<Tick> _s_min_next ;
		#endif
		// cxtors & casts
	public :
		using Date::Date ;
		Pdate(NewType) ;
		// services
		constexpr bool              operator== (Pdate const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Pdate const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Pdate  operator+ (Delay other) const {                     return Pdate( New , _val+other._val ) ; }
		constexpr Pdate  operator- (Delay other) const {                     return Pdate( New , _val-other._val ) ; }
		constexpr Pdate& operator+=(Delay other)       { self = self+other ; return self                           ; }
		constexpr Pdate& operator-=(Delay other)       { self = self-other ; return self                           ; }
		constexpr Delay  operator- (Pdate      ) const ;
		//
		constexpr Pdate round_sec () const {                       return Pdate( New , _val-_val% TicksPerSecond           ) ; }
		constexpr Pdate round_msec() const { static_assert(IsNs) ; return Pdate( New , _val-_val%(TicksPerSecond/1000    ) ) ; }
		constexpr Pdate round_usec() const { static_assert(IsNs) ; return Pdate( New , _val-_val%(TicksPerSecond/1000'000) ) ; }
		//
		bool/*slept*/ sleep_until( ::stop_token , bool flush=true ) const ;                               // if flush, consider we slept if asked to stop but we do not have to wait
		void          sleep_until(                                ) const ;
		//
		::string str( uint8_t prec=0 , bool in_day=false ) const { if (self<Future) return Date::str(prec,in_day) ; else return "Future" ; }
	} ;

	// DDate represents the date of a file, together with its tag (as the lsb's of _val)
	// we lose a few bits of precision, but real disk dates have around ms precision anyway, so we have around 20 bits of margin
	struct Ddate : Date {
		friend ::string& operator+=( ::string& , Ddate const ) ;
		friend Delay ;
		static const Ddate Future ;
	private :
		static constexpr Tick _TagMsk = (1<<NBits<FileTag>)-1 ;
		// cxtors & casts
	public :
		using Date::Date ;
		constexpr Ddate(                           FileTag tag=FileTag::None )                    {                    _val  = +tag ; }
		constexpr Ddate( struct ::stat const& st , FileTag tag               ) : Date{st.st_mtim} { _val &= ~_TagMsk ; _val |= +tag ; }
		// accesses
		constexpr bool    operator+() const { return _date()               ; }
		constexpr FileTag tag      () const { return FileTag(_val&_TagMsk) ; }
	private :
		constexpr Tick _date() const { return _val&~_TagMsk ; }
		// services
	public :
		constexpr bool operator==(Ddate const& other) const { return _val==other._val          ; }        // if only differ by tag bits, all comparisons return false
		constexpr bool operator< (Ddate const& other) const { return _date()<other._date()     ; }        // .
		constexpr bool operator> (Ddate const& other) const { return _date()>other._date()     ; }        // .
		constexpr bool operator<=(Ddate const& other) const { return self<other || self==other ; }        // .
		constexpr bool operator>=(Ddate const& other) const { return self>other || self==other ; }        // .
		//
		using Base::operator+ ;
		constexpr Ddate& operator+=(Delay other)       { _val += other._val&~_TagMsk ;    return self ; } // do not modify tag bits
		constexpr Ddate& operator-=(Delay other)       { _val -= other._val&~_TagMsk ;    return self ; } // .
		constexpr Ddate  operator+ (Delay other) const { Ddate res{self} ; res += other ; return res  ; }
		constexpr Ddate  operator- (Delay other) const { Ddate res{self} ; res -= other ; return res  ; }
		constexpr Delay  operator- (Ddate      ) const ;
		//
		constexpr bool avail_at( Pdate pd , Delay prec ) const {
			Tick d1 = _date()        ;
			Tick d2 = d1 + prec._val ;
			return d1<=d2 && d2<pd._val ;                                                                 // take care of overflow
		}
		//
		::string str( uint8_t prec=0 , bool in_day=false ) const { if (self<Future) return Date(New,_date()).str(prec,in_day) ; else return "Future" ; }
	} ;

}

	//
	// implementation
	//

namespace Time {

	//
	// Delay
	//
	inline constexpr Date Delay::operator+(Date d) const {
		return Date(New,_val+d._val) ;
	}
	inline bool/*slept*/ Delay::_s_sleep( ::stop_token tkn , Delay sleep , Pdate until , bool flush ) { // if flush, consider we slept if asked to stop but we do not have to wait
		if (sleep<=Delay()) return flush || !tkn.stop_requested() ;
		Mutex<MutexLvl::Time>       m   ;
		Lock<Mutex<MutexLvl::Time>> lck { m } ;
		::condition_variable_any cv  ;
		bool res = cv.wait_for( lck , tkn , ::chrono::nanoseconds(sleep.nsec()) , [until](){ return Pdate(New)>=until ; } ) ;
		return res ;
	}
	inline bool/*slept*/ Delay::sleep_for( ::stop_token tkn , bool flush ) const {                      // if flush, consider we slept if asked to stop but we do not have to wait
		return _s_sleep( tkn , self , Pdate(New)+self , flush ) ;
	}
	inline void Delay::sleep_for() const {
		if (_val<=0) return ;
		TimeSpec ts(self) ;
		::nanosleep(&ts,nullptr) ;
	}
	template<class T> requires(::is_arithmetic_v<T>) constexpr Delay Delay::operator*(T f) const { return Delay(New,int64_t(_val*                   f )) ; }
	template<class T> requires(::is_signed_v    <T>) constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/                   f )) ; }
	template<class T> requires(::is_unsigned_v  <T>) constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/::make_signed_t<T>(f))) ; }

	//
	// Pdate
	//

	constexpr Pdate Pdate::Future { New , Pdate::Tick(-1) } ;

	inline constexpr Delay Pdate::operator-(Pdate other) const { return Delay(New,Delay::Tick(_val   -other._val   )) ; }
	inline constexpr Delay Ddate::operator-(Ddate other) const { return Delay(New,Delay::Tick(_date()-other._date())) ; }
	//
	inline bool/*slept*/ Pdate::sleep_until( ::stop_token tkn , bool flush ) const { return Delay::_s_sleep( tkn , self-Pdate(New) , self , flush ) ; } // if flush, consider we slept if asked ...
	inline void          Pdate::sleep_until(                               ) const { (self-Pdate(New)).sleep_for()                                  ; } // ... to stop but we do not have to wait

	//
	// Ddate
	//

	constexpr Ddate Ddate::Future { New , Ddate::Tick(-1) } ;

}

// must be outside Engine namespace as it specializes ::hash
namespace std {
	template<> struct hash<Time::Date       > { size_t operator()(Time::Date        d) const { return d.hash() ; } } ;
	template<> struct hash<Time::Delay      > { size_t operator()(Time::Delay       d) const { return d.hash() ; } } ;
	template<> struct hash<Time::CoarseDelay> { size_t operator()(Time::CoarseDelay d) const { return d.hash() ; } } ;
}
