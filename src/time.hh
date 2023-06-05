// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <chrono>
#include <thread>

#include <ctime>

#include <iomanip>

#include "utils.hh"

namespace Time {

	struct Delay       ;
	struct Date        ;
	struct DiskDate    ;
	struct ProcessDate ;
	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase ;

	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase {
		static constexpr T    TicksPerSecond = 1'000'000'000l     ;
		static constexpr bool IsUnsigned     = ::is_unsigned_v<T> ;
		//
		using Tick     = T                                            ;
		using T32      = ::conditional_t<IsUnsigned,uint32_t,int32_t> ;
		using TimeSpec = struct ::timespec                            ;
		using TimeVal  = struct ::timeval                             ;
		// cxtors & casts
		constexpr          TimeBase(                  ) = default ;
		constexpr explicit TimeBase(T               v ) : _val( v                                          ) {}
		constexpr explicit TimeBase(double          v ) : _val( v*TicksPerSecond                           ) { if (IsUnsigned) SWEAR(v        >=0) ; }
		constexpr explicit TimeBase(float           v ) : _val( v*TicksPerSecond                           ) { if (IsUnsigned) SWEAR(v        >=0) ; }
		constexpr explicit TimeBase(TimeSpec const& ts) : _val( ts.tv_sec*TicksPerSecond + ts.tv_nsec      ) { static_assert(TicksPerSecond==1'000'000'000l) ; if (IsUnsigned) SWEAR(ts.tv_sec>=0) ; }
		constexpr explicit TimeBase(TimeVal  const& tv) : _val( tv.tv_sec*TicksPerSecond + tv.tv_usec*1000 ) { static_assert(TicksPerSecond==1'000'000'000l) ; if (IsUnsigned) SWEAR(tv.tv_sec>=0) ; }
		//
		constexpr explicit operator TimeSpec() const { TimeSpec ts{ .tv_sec=sec() , .tv_nsec=nsec_in_s() } ; return ts                          ; }
		constexpr explicit operator T       () const {                                                       return _val                        ; }
		constexpr explicit operator double  () const {                                                       return double(_val)/TicksPerSecond ; }
		constexpr explicit operator float   () const {                                                       return float (_val)/TicksPerSecond ; }
		// accesses
		constexpr T    operator+() const { return  _val ; }
		constexpr bool operator!() const { return !_val ; }
		//
		constexpr T   sec      () const {                                                 return _val/TicksPerSecond   ; }
		constexpr T   nsec     () const { static_assert(TicksPerSecond==1'000'000'000l) ; return _val                  ; }
		constexpr T32 nsec_in_s() const { static_assert(TicksPerSecond==1'000'000'000l) ; return _val%TicksPerSecond   ; }
		constexpr T   usec     () const {                                                 return nsec     ()/1000      ; }
		constexpr T32 usec_in_s() const {                                                 return nsec_in_s()/1000      ; }
		constexpr T   msec     () const {                                                 return nsec     ()/1000'000l ; }
		constexpr T32 msec_in_s() const {                                                 return nsec_in_s()/1000'000  ; }
		//
		void clear() { _val = 0 ; }
		// data
	protected :
		T _val = 0 ;
	} ;

	struct Delay : TimeBase<int64_t> {
		using Base = TimeBase<int64_t> ;
		friend ::ostream& operator<<( ::ostream& , Delay const ) ;
		friend Date        ;
		friend DiskDate    ;
		friend ProcessDate ;
		// statics
	private :
		static bool/*slept*/ _s_sleep( ::stop_token tkn , Delay sleep , ProcessDate until ) ;
		// cxtors & casts
	public :
		using Base::Base ;
		constexpr Delay(Base v) : Base(v) {}
		// services
		constexpr bool              operator== (Delay const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Delay const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Delay  operator+ (Delay other) const {                         return Delay(_val+other._val) ; }
		constexpr Delay  operator- (Delay other) const {                         return Delay(_val-other._val) ; }
		constexpr Delay& operator+=(Delay other)       { *this = *this + other ; return *this                  ; }
		constexpr Delay& operator-=(Delay other)       { *this = *this - other ; return *this                  ; }
		constexpr Date   operator+ (Date       ) const ;
		//
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay  operator* (T f) const ;
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator*=(T f)       { *this = *this*f ; return *this ; }
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator/=(T f)       { *this = *this/f ; return *this ; }
		//
		bool/*slept*/ sleep_for(::stop_token) const ;
		void          sleep_for(            ) const ;
		//
		::string short_str() const ;
	} ;

	// short float representation of time (positive)
	// when exp<=0, representation is linear after TicksPerSecond
	// else       , it is floating point
	struct CoarseDelay {
		friend ::ostream& operator<<( ::ostream& , CoarseDelay const ) ;
		using Val = uint16_t ;
		static constexpr int64_t  TicksPerSecond = 1000                 ;      // this may be freely modified
		static constexpr uint8_t  Mantissa       = 11                   ;      // .
		static constexpr uint32_t Scale          = 28294                ;      // (::logf(Delay::TicksPerSecond)-::logf(TicksPerSecond))*(1<<Mantissa) ;
		static constexpr Val      Factor         = (1<<Mantissa)*10/100 ;      // adding factor means multiply by 1.1
		//
		static const CoarseDelay MinPositive ;
		// cxtors & casts
		constexpr CoarseDelay() = default ;
		constexpr CoarseDelay(Delay d) { *this = d ; }
		constexpr CoarseDelay& operator=(Delay d) {
			uint32_t t = ::logf(+d)*(1<<Mantissa)+0.5 ;
			if      ( t >= (1<<NBits<Val>)+Scale ) _val = -1      ;
			else if ( t <                  Scale ) _val =  0      ;
			else                                   _val = t-Scale ;
			return *this ;
		}
		explicit constexpr CoarseDelay(Val v) : _val(v) {}
		constexpr operator Delay() const {
			if (!_val) return Delay() ;
			else       return Delay(int64_t(::expf(float(_val+Scale)/(1<<Mantissa)))) ;
		}
		constexpr explicit operator double() const { return double(Delay(*this)) ; }
		constexpr explicit operator float () const { return float (Delay(*this)) ; }
		// services
		constexpr uint16_t          operator+  (                    ) const { return _val ;                      }
		constexpr bool              operator!  (                    ) const { return !+*this ;                   }
		constexpr CoarseDelay       operator+  (Delay              d) const { return Delay(*this) + d ;          }
		constexpr CoarseDelay&      operator+= (Delay              d)       { *this = *this + d ; return *this ; }
		constexpr bool              operator== (CoarseDelay const& d) const { return _val== d._val ;             }
		constexpr ::strong_ordering operator<=>(CoarseDelay const& d) const { return _val<=>d._val ;             }
		//
		CoarseDelay scale_up  () const { return CoarseDelay( _val>=Val(-1)-Factor ? Val(-1) : Val(_val+Factor) ) ; }
		CoarseDelay scale_down() const { return CoarseDelay( _val<=        Factor ? Val( 0) : Val(_val-Factor) ) ; }
		//
		::string short_str() const { return Delay(*this).short_str() ; }
		// data
	private :
		Val _val = 0 ;
	} ;
	constexpr CoarseDelay CoarseDelay::MinPositive = CoarseDelay(CoarseDelay::Val(1)) ;

	struct Date : TimeBase<uint64_t> {
		using Base = TimeBase<uint64_t> ;
		friend ::ostream& operator<<( ::ostream& , Date const ) ;
		friend Delay ;
		static const Date None   ;
		static const Date Future ;
		// cxtors & casts
		using Base::Base ;
		// services
		using Base::operator+ ;
		constexpr Date  operator+ (Delay other) const {                         return Date(_val+other._val) ; }
		constexpr Date  operator- (Delay other) const {                         return Date(_val-other._val) ; }
		constexpr Date& operator+=(Delay other)       { *this = *this + other ; return *this                 ; }
		constexpr Date& operator-=(Delay other)       { *this = *this - other ; return *this                 ; }
		//
		::string str(uint8_t prec=0) const ;
	} ;

	//
	// We implement a complete separation between wall-clock time (ProcessDate) and time seen from the disk which may be on a server with its own view of time.
	// Care has been taken so that you cannot compare and more generally inter-operate between these 2 times.
	// Getting current ProcessDate-time is very cheap (few ns), so no particular effort is made to cache or otherwise optimize it.
	// But it is the contrary for DiskDate current time : you must create or write to a file, very expensive (some fraction of ms).
	// So we keep a lazy evaluated cached value that is refreshed once per loop (after we have waited) in each thread :
	// - in terms of precision, this is enough, we just want correct relative order
	// - in terms of cost, needing current disk time is quite rare (actually, we just need it to put a date on when a file is known to not exist, else we have a file date)
	// - so in case of exceptional heavy use, cached value is used and in case of no use, we do not pay at all.
	//

	struct ProcessDate : Date {
		friend ::ostream& operator<<( ::ostream& , ProcessDate const ) ;
		friend Delay ;
		// statics
		static ProcessDate s_now() ;
		// cxtors & casts
		using Date::Date ;
		// services
		constexpr bool              operator== (ProcessDate const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(ProcessDate const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr ProcessDate  operator+ (Delay other) const {                         return ProcessDate(_val+other._val) ; }
		constexpr ProcessDate  operator- (Delay other) const {                         return ProcessDate(_val-other._val) ; }
		constexpr ProcessDate& operator+=(Delay other)       { *this = *this + other ; return *this                        ; }
		constexpr ProcessDate& operator-=(Delay other)       { *this = *this - other ; return *this                        ; }
		constexpr Delay        operator- (ProcessDate) const ;
		//
		bool/*slept*/ sleep_until(::stop_token) const ;
		void          sleep_until(            ) const ;
	} ;

	struct DiskDate : Date {
		friend ::ostream& operator<<( ::ostream& , DiskDate const ) ;
		friend Delay ;
		// statics
		// s_now is rarely but unpredictably used
		// so the idea is that s_refresh_now is called after each wait (i.e. once per loop in each thread)
		// but this is cheap, you only pay if you actually call s_now, and this is the rare event
		static void     s_refresh_now() { _t_now = {} ;                        } // refresh s_now (actual clear cached value)
		static DiskDate s_now        () { return +_t_now ? _t_now : _s_now() ; } // provide a disk view of now
	private :
		static DiskDate _s_now   () ;                                          // update cached value and return it
		// static data
		static thread_local DiskDate _t_now ;                                  // per thread lazy evaluated cached value
		// cxtors & casts
	public :
		using Date::Date ;
		// services
		constexpr bool              operator== (DiskDate const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(DiskDate const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr DiskDate  operator+ (Delay other) const {                         return DiskDate(_val+other._val) ; }
		constexpr DiskDate  operator- (Delay other) const {                         return DiskDate(_val-other._val) ; }
		constexpr DiskDate& operator+=(Delay other)       { *this = *this + other ; return *this                     ; }
		constexpr DiskDate& operator-=(Delay other)       { *this = *this - other ; return *this                     ; }
		constexpr Delay     operator- (DiskDate   ) const ;
		//
		bool/*slept*/ sleep_until(::stop_token) const ;
		void          sleep_until(            ) const ;
	} ;

	//
	// implementation
	//

	//
	// Delay
	//
	inline constexpr Date Delay::operator+(Date d) const {
		return Date(_val+d._val) ;
	}
	inline bool/*slept*/ Delay::_s_sleep( ::stop_token tkn , Delay sleep , ProcessDate until ) {
		if (sleep<=Delay()) return !tkn.stop_requested() ;
		::mutex                  m   ;
		::unique_lock<mutex>     lck { m } ;
		::condition_variable_any cv  ;
		bool res = cv.wait_for( lck , tkn , ::chrono::nanoseconds(sleep.nsec()) , [until](){ return ProcessDate::s_now()>=until ; } ) ;
		return res ;
	}
	inline bool/*slept*/ Delay::sleep_for(::stop_token tkn) const {
		return _s_sleep( tkn , *this , ProcessDate::s_now()+*this ) ;
	}
	inline void Delay::sleep_for() const {
		if (_val<=0) return ;
		TimeSpec ts(*this) ;
		::nanosleep(&ts,nullptr) ;
	}
	template<class T> requires(::is_arithmetic_v<T>) inline constexpr Delay Delay::operator*(T f) const { return Delay(int64_t(_val*f)) ; }
	template<class T> requires(::is_arithmetic_v<T>) inline constexpr Delay Delay::operator/(T f) const { return Delay(int64_t(_val/f)) ; }

	//
	// Date
	//
	constexpr Date Date::None  {uint64_t( 0)} ;
	constexpr Date Date::Future{uint64_t(-1)} ;
	inline ProcessDate ProcessDate::s_now() {
		TimeSpec now ;
		::clock_gettime(CLOCK_REALTIME,&now) ;
		return ProcessDate(now) ;
	}
	inline constexpr Delay ProcessDate::operator-(ProcessDate other) const { return Delay(int64_t(_val-other._val)) ; }
	inline constexpr Delay DiskDate   ::operator-(DiskDate    other) const { return Delay(int64_t(_val-other._val)) ; }
	//
	inline bool/*slept*/ ProcessDate::sleep_until(::stop_token tkn) const { return Delay::_s_sleep( tkn , *this-s_now() , *this ) ; }
	inline void          ProcessDate::sleep_until(                ) const { (*this-s_now()).sleep_for()                           ; }

}

// must be outside Engine namespace as it specializes ::hash
namespace std {
	template<> struct hash<Time::Date       > { size_t operator()(Time::Date        d) const { return +d ; } } ;
	template<> struct hash<Time::Delay      > { size_t operator()(Time::Delay       d) const { return +d ; } } ;
	template<> struct hash<Time::CoarseDelay> { size_t operator()(Time::CoarseDelay d) const { return +d ; } } ;
}
