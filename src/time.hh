// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <chrono>
namespace chrono = std::chrono ;

#include <ctime>

#include "utils.hh"

// START_OF_VERSIONING CACHE JOB REPO
enum class FileTag : uint8_t { // FileTag is defined here as it is used for Ddate and disk.hh includes this file anyway
	None
,	Unknown
,	Dir
,	Lnk
,	Reg                        // >=Reg means file is a regular file
,	Empty                      // empty and not executable
,	Exe                        // a regular file with exec permission
//
// aliases
,	Target = Lnk               // >=Target means file can be generated as a target
} ;
// END_OF_VERSIONING
using FileTags = BitMap<FileTag> ;
constexpr FileTags TargetTags = []()->FileTags {
	FileTags res ;
	for( FileTag t : iota(All<FileTag>) ) if (t>=FileTag::Target) res |= t ;
	return res ;
}() ;

namespace Time {

	using TimeSpec = struct ::timespec ;
	using TimeVal  = struct ::timeval  ;

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
		// cxtors & casts
		constexpr          TimeBase() = default ;
		constexpr explicit TimeBase(int             v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(long            v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(uint            v ) requires(IsUnsigned) : _val{   v*TicksPerSecond                           } {                                     }
		constexpr explicit TimeBase(ulong           v ) requires(IsUnsigned) : _val{   v*TicksPerSecond                           } {                                     }
		constexpr explicit TimeBase(double          v )                      : _val{ T(v*TicksPerSecond)                          } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(float           v )                      : _val{ T(v*TicksPerSecond)                          } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(TimeSpec const& ts)                      : _val{   ts.tv_sec*TicksPerSecond + ts.tv_nsec      } { static_assert(IsNs) ; if (IsUnsigned) SWEAR(ts.tv_sec>=0) ; }
		constexpr explicit TimeBase(TimeVal  const& tv)                      : _val{   tv.tv_sec*TicksPerSecond + tv.tv_usec*1000 } { static_assert(IsNs) ; if (IsUnsigned) SWEAR(tv.tv_sec>=0) ; }
		constexpr explicit TimeBase(NewType,Tick    v )                      : _val{   v                                          } {}
		//
		constexpr explicit operator TimeSpec() const { TimeSpec ts{ .tv_sec=time_t(sec()) , .tv_nsec=int32_t    (nsec_in_s()) } ; return ts                          ; }
		constexpr explicit operator TimeVal () const { TimeVal  tv{ .tv_sec=time_t(sec()) , .tv_usec=suseconds_t(usec_in_s()) } ; return tv                          ; }
		constexpr explicit operator double  () const {                                                                            return double(_val)/TicksPerSecond ; }
		constexpr explicit operator float   () const {                                                                            return float (_val)/TicksPerSecond ; }
		// accesses
		constexpr bool operator+ (   ) const { return _val ;                     }
		constexpr Tick val       (   ) const { return _val ;                     }
		/**/      void operator++(   )       { SWEAR(_val!=Max<Tick>) ; _val++ ; }
		/**/      void operator++(int)       { SWEAR(_val!=Max<Tick>) ; _val++ ; }
		/**/      void operator--(   )       { SWEAR(_val!=Min<Tick>) ; _val-- ; }
		/**/      void operator--(int)       { SWEAR(_val!=Min<Tick>) ; _val-- ; }
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
		// services
		size_t hash() const { return _val ; }
		// data
	protected :
		T _val = 0 ;
	} ;

	struct Delay : TimeBase<int64_t> {
		using Base = TimeBase<int64_t> ;
		friend ::string& operator+=( ::string& , Delay const ) ;
		friend Date        ;
		friend Ddate       ;
		friend Pdate       ;
		friend CoarseDelay ;
		static constexpr size_t ShortStrSz = 6 ;
		static const     Delay  Lowest     ;
		static const     Delay  Highest    ;
		static const     Delay  Forever    ;
		// statics
	private :
		static bool/*slept*/ _s_sleep( ::stop_token , Delay sleep , Pdate until , bool flush=true ) ;     // if flush, consider we slept if asked to stop but we do not have to wait
		// cxtors & casts
	public :
		using Base::Base ;
		constexpr Delay(Base v         ) : Base(v) {}
		/**/      Delay(::string const&) ;                                                                // format is same as short_str
		operator ::chrono::nanoseconds() const { return ::chrono::nanoseconds(nsec()) ; }
		// services
		constexpr bool              operator== (Delay const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Delay const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Delay  operator- (       ) const {                 return Delay(New,-_val      )  ; }
		constexpr Delay  operator+ (Delay d) const {                 return Delay(New,_val+d._val)  ; }
		constexpr Delay  operator- (Delay d) const {                 return Delay(New,_val-d._val)  ; }
		constexpr Delay& operator+=(Delay d)       { self = self+d ; return self                    ; }
		constexpr Delay& operator-=(Delay d)       { self = self-d ; return self                    ; }
		constexpr Date   operator+ (Date   ) const ;
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
	} ;
	constexpr Delay Delay::Lowest  { New , Min<Tick> } ;
	constexpr Delay Delay::Highest { New , Max<Tick> } ;
	constexpr Delay Delay::Forever { New , Max<Tick> } ;
	template<class T> requires(::is_arithmetic_v<T>) constexpr Delay operator*( T f , Delay d ) { return d*f ; }

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
		constexpr CoarseDelay() = default ;
		constexpr CoarseDelay(Delay d) {
			if (!d._val) {
				_val = 0 ;
			} else {
				uint32_t t = ::ldexpf(::logf(d._val),Mantissa)+0.5 ;
				if      ( t >= (1<<NBits<Val>)+Scale ) _val = -1      ;
				else if ( t <                  Scale ) _val =  0      ;
				else                                   _val = t-Scale ;
			}
		}
		constexpr operator Delay() const {
			if (!_val) return Delay() ;
			else       return Delay(New,int64_t(::expf(::ldexpf(_val+Scale,-Mantissa)))) ;
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
		static const Pdate Future  ;                        // highest date, used as infinity
		static const Pdate Future1 ;                        // last date before Future
		// static data
	private :
		#if __cplusplus<202600L
			static Mutex<MutexLvl::PdateNew> _s_mutex_new ; // ensure serialization to _s_last before c++26
			static Tick                      _s_min_next  ; // time returned by last call, used to ensure strict monotonicity on systems that have unreliable clocks
		#else
			static Atomic<Tick> _s_min_next ;
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
		static const Ddate Future  ;                                                                      // highest date, used as infinity
		static const Ddate Future1 ;                                                                      // last date before Future
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
	inline bool/*slept*/ Delay::_s_sleep( ::stop_token stkn , Delay sleep , Pdate until , bool flush ) { // if flush, consider we slept if asked to stop but we do not have to wait
		if (sleep<=Delay()) return flush || !stkn.stop_requested() ;
		Mutex<> m   ;
		Lock    lck { m } ;
		::condition_variable_any cv  ;
		bool slept = cv.wait_for( lck , stkn , ::chrono::nanoseconds(sleep.nsec()) , [until](){ return Pdate(New)>=until ; } ) ;
		return slept ;
	}
	inline bool/*slept*/ Delay::sleep_for( ::stop_token stkn , bool flush ) const {                      // if flush, consider we slept if asked to stop but we do not have to wait
		return _s_sleep( stkn , self , Pdate(New)+self , flush ) ;
	}
	inline void Delay::sleep_for() const {
		if (_val<=0) return ;
		TimeSpec ts(self) ;
		::nanosleep(&ts,nullptr/*rem*/) ;
	}
	template<class T> requires(::is_arithmetic_v<T>) constexpr Delay Delay::operator*(T f) const { return Delay(New,int64_t(_val*                   f )) ; }
	template<class T> requires(::is_signed_v    <T>) constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/                   f )) ; }
	template<class T> requires(::is_unsigned_v  <T>) constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/::make_signed_t<T>(f))) ; }

	//
	// Pdate
	//

	constexpr Pdate Pdate::Future  { New , Pdate::Tick(-1) } ;
	constexpr Pdate Pdate::Future1 { New , Pdate::Tick(-2) } ;

	inline constexpr Delay Pdate::operator-(Pdate other) const { return Delay(New,Delay::Tick(_val   -other._val   )) ; }
	inline constexpr Delay Ddate::operator-(Ddate other) const { return Delay(New,Delay::Tick(_date()-other._date())) ; }
	//
	inline bool/*slept*/ Pdate::sleep_until( ::stop_token stkn , bool flush ) const { return Delay::_s_sleep( stkn , self-Pdate(New) , self , flush ) ; } // if flush, consider we slept if asked ...
	inline void          Pdate::sleep_until(                                ) const { (self-Pdate(New)).sleep_for()                                   ; } // ... to stop but we do not have to wait

	//
	// Ddate
	//

	constexpr Ddate Ddate::Future  { New , Ddate::Tick(-1) } ;
	constexpr Ddate Ddate::Future1 { New , Ddate::Tick(-2) } ;

}
