// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <cmath>
#include <ctime>

#include <chrono>
#include <condition_variable>

#include "utils.hh"

// FileTag is defined here as it is used for Ddate and disk.hh includes this file anyway
ENUM( FileTag
,	None
,	Reg
,	Exe // a regular file with exec permission
,	Lnk
,	Dir
,	Err
)

namespace Time {

	struct Delay       ;
	struct CoarseDelay ;
	struct Date        ;
	struct Ddate       ;
	struct Pdate       ;
	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase ;

	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase {
		friend CoarseDelay ;
		static constexpr T    TicksPerSecond = 1'000'000'000l                 ; // if modified some methods have to be rewritten, as indicated by static asserts
		static constexpr bool IsUnsigned     = ::is_unsigned_v<T>             ;
		static constexpr bool IsNs           = TicksPerSecond==1'000'000'000l ;
		//
		using Tick     = T                                            ;
		using T32      = ::conditional_t<IsUnsigned,uint32_t,int32_t> ;
		using TimeSpec = struct ::timespec                            ;
		using TimeVal  = struct ::timeval                             ;
		// cxtors & casts
		constexpr          TimeBase(                  )                      = default ;
		constexpr explicit TimeBase(int             v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(long            v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(unsigned int    v ) requires(IsUnsigned) : _val{   v*TicksPerSecond                           } {                                     }
		constexpr explicit TimeBase(unsigned long   v ) requires(IsUnsigned) : _val{   v*TicksPerSecond                           } {                                     }
		constexpr explicit TimeBase(double          v )                      : _val{ T(v*TicksPerSecond)                          } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(float           v )                      : _val{   v*TicksPerSecond                           } { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(TimeSpec const& ts)                      : _val{   ts.tv_sec*TicksPerSecond + ts.tv_nsec      } { static_assert(IsNs) ; if (IsUnsigned) SWEAR(ts.tv_sec>=0) ; }
		constexpr explicit TimeBase(TimeVal  const& tv)                      : _val{   tv.tv_sec*TicksPerSecond + tv.tv_usec*1000 } { static_assert(IsNs) ; if (IsUnsigned) SWEAR(tv.tv_sec>=0) ; }
	protected :
		constexpr explicit TimeBase(NewType,T v) : _val{v} {}
		//
	public :
		constexpr explicit operator TimeSpec() const { TimeSpec ts{ .tv_sec=sec() , .tv_nsec=nsec_in_s() } ; return ts                          ; }
		constexpr explicit operator TimeVal () const { TimeVal  tv{ .tv_sec=sec() , .tv_usec=usec_in_s() } ; return tv                          ; }
		constexpr explicit operator double  () const {                                                       return double(_val)/TicksPerSecond ; }
		constexpr explicit operator float   () const {                                                       return float (_val)/TicksPerSecond ; }
		// accesses
		constexpr bool operator+() const { return  _val ; }
		constexpr bool operator!() const { return !_val ; }
		//
		constexpr T   sec      () const {                       return _val/TicksPerSecond   ; }
		constexpr T   nsec     () const { static_assert(IsNs) ; return _val                  ; }
		constexpr T32 nsec_in_s() const { static_assert(IsNs) ; return _val%TicksPerSecond   ; }
		constexpr T   usec     () const {                       return nsec     ()/1000      ; }
		constexpr T32 usec_in_s() const {                       return nsec_in_s()/1000      ; }
		constexpr T   msec     () const {                       return nsec     ()/1000'000l ; }
		constexpr T32 msec_in_s() const {                       return nsec_in_s()/1000'000  ; }
		//
		void clear() { _val = 0 ; }
		// data
	protected :
		T _val = 0 ;
	} ;

	struct Delay : TimeBase<int64_t> {
		using Base = TimeBase<int64_t> ;
		friend ::ostream& operator<<( ::ostream& , Delay const ) ;
		friend Date  ;
		friend Ddate ;
		friend Pdate ;
		friend CoarseDelay ;
		static const Delay Lowest  ;
		static const Delay Highest ;
		// statics
	private :
		static bool/*slept*/ _s_sleep( ::stop_token tkn , Delay sleep , Pdate until ) ;
		// cxtors & casts
	public :
		using Base::Base ;
		constexpr Delay(Base v) : Base(v) {}
		// services
		constexpr bool              operator== (Delay const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Delay const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Delay  operator+ (Delay other) const {                         return Delay(New,_val+other._val) ; }
		constexpr Delay  operator- (Delay other) const {                         return Delay(New,_val-other._val) ; }
		constexpr Delay& operator+=(Delay other)       { *this = *this + other ; return *this                      ; }
		constexpr Delay& operator-=(Delay other)       { *this = *this - other ; return *this                      ; }
		constexpr Date   operator+ (Date       ) const ;
		//
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay  operator* (T f) const ;
		template<class T> requires(::is_signed_v    <T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_unsigned_v  <T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator*=(T f)       { *this = *this*f ; return *this ; }
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator/=(T f)       { *this = *this/f ; return *this ; }
		//
		bool/*slept*/ sleep_for(::stop_token) const ;
		void          sleep_for(            ) const ;
		//
		::string short_str() const ;
		size_t   hash     () const { return _val ; }
	} ;
	constexpr Delay Delay::Lowest  { New , ::numeric_limits<Tick>::min() } ;
	constexpr Delay Delay::Highest { New , ::numeric_limits<Tick>::max() } ;

	// short float representation of time (positive)
	// when exp<=0, representation is linear after TicksPerSecond
	// else       , it is floating point
	struct CoarseDelay {
		friend ::ostream& operator<<( ::ostream& , CoarseDelay const ) ;
		using Val = uint16_t ;
		static constexpr int64_t  TicksPerSecond = 1000                 ;      // this may be freely modified
		static constexpr uint8_t  Mantissa       = 11                   ;      // .
		static constexpr uint32_t Scale          = 28294                ;      // (::logf(Delay::TicksPerSecond)-::logf(TicksPerSecond))*(1<<Mantissa) ;
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
		constexpr CoarseDelay(Delay d) { *this = d ; }
		constexpr CoarseDelay& operator=(Delay d) {
			uint32_t t = ::logf(d._val)*(1<<Mantissa)+0.5 ;
			if      ( t >= (1<<NBits<Val>)+Scale ) _val = -1      ;
			else if ( t <                  Scale ) _val =  0      ;
			else                                   _val = t-Scale ;
			return *this ;
		}
		constexpr operator Delay() const {
			if (!_val) return Delay() ;
			else       return Delay(New,int64_t(::expf(float(_val+Scale)/(1<<Mantissa)))) ;
		}
		constexpr explicit operator double() const { return double(Delay(*this)) ; }
		constexpr explicit operator float () const { return float (Delay(*this)) ; }
		// services
		constexpr bool              operator+  (                    ) const { return _val ;                      }
		constexpr bool              operator!  (                    ) const { return !_val ;                     }
		constexpr CoarseDelay       operator+  (Delay              d) const { return Delay(*this) + d ;          }
		constexpr CoarseDelay&      operator+= (Delay              d)       { *this = *this + d ; return *this ; }
		constexpr bool              operator== (CoarseDelay const& d) const { return _val== d._val ;             }
		constexpr ::strong_ordering operator<=>(CoarseDelay const& d) const { return _val<=>d._val ;             }
		//
		CoarseDelay scale_up  (uint32_t percent) const { return CoarseDelay( New , _val>=Val(-1)-_Factor(percent) ? Val(-1) : Val(_val+_Factor(percent)) ) ; }
		CoarseDelay scale_down(uint32_t percent) const { return CoarseDelay( New , _val<=        _Factor(percent) ? Val( 0) : Val(_val-_Factor(percent)) ) ; }
		//
		::string short_str() const { return Delay(*this).short_str() ; }
		size_t   hash     () const { return _val                     ; }
		// data
	private :
		Val _val = 0 ;
	} ;
	constexpr CoarseDelay CoarseDelay::Lowest  { New , Val(1)                       } ;
	constexpr CoarseDelay CoarseDelay::Highest { New , ::numeric_limits<Val>::max() } ;

	struct Date : TimeBase<uint64_t> {
		using Base = TimeBase<uint64_t> ;
		friend ::ostream& operator<<( ::ostream& , Date const ) ;
		friend Delay ;
		friend Ddate ;
		friend Pdate ;
		// cxtors & casts
		using Base::Base ;
		Date(::string_view const&) ;                                           // read a reasonable approximation of ISO8601
		// services
		using Base::operator+ ;
		constexpr Date  operator+ (Delay other) const {                         return Date(New,_val+other._val) ; }
		constexpr Date  operator- (Delay other) const {                         return Date(New,_val-other._val) ; }
		constexpr Date& operator+=(Delay other)       { *this = *this + other ; return *this                     ; }
		constexpr Date& operator-=(Delay other)       { *this = *this - other ; return *this                     ; }
		//
		::string str ( uint8_t prec=0 , bool in_day=false ) const ;
		size_t   hash(                                    ) const { return _val ; }
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
		friend ::ostream& operator<<( ::ostream& , Pdate const ) ;
		friend Delay ;
		static const Pdate Future ;
		// cxtors & casts
		using Date::Date ;
		Pdate(NewType) ;
		// services
		constexpr bool              operator== (Pdate const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Pdate const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Pdate  operator+ (Delay other) const {                         return Pdate(New,_val+other._val) ; }
		constexpr Pdate  operator- (Delay other) const {                         return Pdate(New,_val-other._val) ; }
		constexpr Pdate& operator+=(Delay other)       { *this = *this + other ; return *this                      ; }
		constexpr Pdate& operator-=(Delay other)       { *this = *this - other ; return *this                      ; }
		constexpr Delay  operator- (Pdate      ) const ;
		//
		bool/*slept*/ sleep_until(::stop_token) const ;
		void          sleep_until(            ) const ;
		//
		::string str ( uint8_t prec=0 , bool in_day=false ) const { if (*this<Future) return Date::str(prec,in_day) ; else return "Future" ; }
	} ;

	// DDate represents the date of a file, together with its tag (as the lsb's of _val)
	// we lose a few bits of precision, but real disk dates have around ms precision anyway, so we have around 20 bits of margin
	struct Ddate : Date {
		friend ::ostream& operator<<( ::ostream& , Ddate const ) ;
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
		constexpr bool    operator!() const { return !+*this               ; }
		constexpr FileTag tag      () const { return FileTag(_val&_TagMsk) ; }
	private :
		constexpr Tick _date() const { return _val&~_TagMsk ; }
		// services
	public :
		constexpr bool operator==(Ddate const& other) const { return _val==other._val            ; } // if only differ by tag bits, all comparisons return false
		constexpr bool operator< (Ddate const& other) const { return _date()<other._date()       ; } // .
		constexpr bool operator> (Ddate const& other) const { return _date()>other._date()       ; } // .

		constexpr bool operator<=(Ddate const& other) const { return *this<other || *this==other ; } // .
		constexpr bool operator>=(Ddate const& other) const { return *this>other || *this==other ; } // .
		//
		using Base::operator+ ;
		constexpr Ddate& operator+=(Delay other)       { _val += other._val&~_TagMsk ;     return *this ; } // do not modify tag bits
		constexpr Ddate& operator-=(Delay other)       { _val -= other._val&~_TagMsk ;     return *this ; } // .
		constexpr Ddate  operator+ (Delay other) const { Ddate res{*this} ; res += other ; return res   ; }
		constexpr Ddate  operator- (Delay other) const { Ddate res{*this} ; res -= other ; return res   ; }
		constexpr Delay  operator- (Ddate      ) const ;
		//
		::string str( uint8_t prec=0 , bool in_day=false ) const { if (*this<Future) return Date(New,_date()).str(prec,in_day) ; else return "Future" ; }
	} ;

	struct FullDate {
		friend ::ostream& operator<<( ::ostream& , FullDate const& ) ;
		// cxtors & casts
		FullDate(                     ) = default ;
		FullDate( NewType             ) : d{  } , p{New} {}
		FullDate( Ddate d_            ) : d{d_} , p{New} {}
		FullDate(            Pdate p_ ) : d{  } , p{p_ } {}
		FullDate( Ddate d_ , Pdate p_ ) : d{d_} , p{p_ } {}
		// accesses
		bool operator==(FullDate const&) const = default ;
		bool operator+ (               ) const { return +p || +d ; }
		bool operator! (               ) const { return !+*this  ; }
		// data
		Ddate d ;
		Pdate p ;
	} ;

	//
	// implementation
	//

	//
	// Delay
	//
	inline constexpr Date Delay::operator+(Date d) const {
		return Date(New,_val+d._val) ;
	}
	inline bool/*slept*/ Delay::_s_sleep( ::stop_token tkn , Delay sleep , Pdate until ) {
		if (sleep<=Delay()) return !tkn.stop_requested() ;
		::mutex                  m   ;
		::unique_lock<mutex>     lck { m } ;
		::condition_variable_any cv  ;
		bool res = cv.wait_for( lck , tkn , ::chrono::nanoseconds(sleep.nsec()) , [until](){ return Pdate(New)>=until ; } ) ;
		return res ;
	}
	inline bool/*slept*/ Delay::sleep_for(::stop_token tkn) const {
		return _s_sleep( tkn , *this , Pdate(New)+*this ) ;
	}
	inline void Delay::sleep_for() const {
		if (_val<=0) return ;
		TimeSpec ts(*this) ;
		::nanosleep(&ts,nullptr) ;
	}
	template<class T> requires(::is_arithmetic_v<T>) constexpr Delay Delay::operator*(T f) const { return Delay(New,int64_t(_val*                   f )) ; }
	template<class T> requires(::is_signed_v    <T>) constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/                   f )) ; }
	template<class T> requires(::is_unsigned_v  <T>) constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/::make_signed_t<T>(f))) ; }

	//
	// Pdate
	//

	constexpr Pdate Pdate::Future { New , Pdate::Tick(-1) } ;

	inline Pdate::Pdate(NewType) {
		TimeSpec now ;
		::clock_gettime(CLOCK_REALTIME,&now) ;
		*this = Pdate(now) ;
	}
	inline constexpr Delay Pdate::operator-(Pdate other) const { return Delay(New,Delay::Tick(_val   -other._val   )) ; }
	inline constexpr Delay Ddate::operator-(Ddate other) const { return Delay(New,Delay::Tick(_date()-other._date())) ; }
	//
	inline bool/*slept*/ Pdate::sleep_until(::stop_token tkn) const { return Delay::_s_sleep( tkn , *this-Pdate(New) , *this ) ; }
	inline void          Pdate::sleep_until(                ) const { (*this-Pdate(New)).sleep_for()                           ; }

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
