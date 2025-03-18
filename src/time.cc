// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "time.hh"

namespace Time {

	using namespace Disk ;

	static void _add_frac( ::string& res , uint32_t ns , uint8_t prec ) {
		if (!prec) return ;                                               // no decimal point if no sub-second part
		uint32_t sub1  = 1'000'000'000 + ns ;                             // avoid formatting efforts : sub1 is now in the range 1.000.000.000-1.999.999.999
		uint8_t  point = res.size()         ;                             // position of the decimal point
		res.reserve(point+1+prec) ;                                       // 1 to account for the decimal point
		SWEAR(prec<=9,prec) ;
		switch (prec) {
			case 1 : sub1 /= 100'000'000 ; break ;
			case 2 : sub1 /= 10'000'000  ; break ;
			case 3 : sub1 /= 1'000'000   ; break ;
			case 4 : sub1 /= 100'000     ; break ;
			case 5 : sub1 /= 10'000      ; break ;
			case 6 : sub1 /= 1'000       ; break ;
			case 7 : sub1 /= 100         ; break ;
			case 8 : sub1 /= 10          ; break ;
			case 9 :                     ; break ;
		DF}
		res        += sub1 ;
		res[point]  = '.'  ;                                              // replace inital 1 (to avoid formatting efforts) with the decimal point
	}

	//
	// Delay
	//

	::string& operator+=( ::string& os , Delay const d ) {
		int64_t  s  =       d.sec      ()  ;
		uint32_t ns = ::abs(d.nsec_in_s()) ;
		/**/                  os << "D:"                                                  ;
		if ( !s && d._val<0 ) os << '-'                                                   ;
		return                os << cat(s,'.',widen(cat(ns),9,true/*right*/,'0'/*fill*/)) ;
	}

	::string Delay::str(uint8_t prec) const {
		Tick     s   = sec      () ;
		int32_t  ns  = nsec_in_s() ;
		::string res ;
		if (self<Delay()) { res+='-' ; s = -s ; ns = -ns ; }
		res += s ;
		_add_frac(res,ns,prec) ;
		return res ;
	}

	::string Delay::short_str() const {
		Tick        v    = msec()     ;
		const char* sign = v<0?"-":"" ;
		if (v<0) v = -v ;
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wrestrict"                                                             // seems to be a gcc bug in some versions
		//                                                       right                         right fill
		/**/      if (v< 10*1000) return sign+      cat(v/1000)        +'.'+widen(cat(v%1000),3,true,'0')+'s' ;
		v /= 10 ; if (v< 60* 100) return sign+widen(cat(v/ 100),2,true)+'.'+widen(cat(v% 100),2,true,'0')+'s' ;
		v /=100 ; if (v< 60*  60) return sign+widen(cat(v/  60),2,true)+'m'+widen(cat(v%  60),2,true,'0')+'s' ;
		v /= 60 ; if (v<100*  60) return sign+widen(cat(v/  60),2,true)+'h'+widen(cat(v%  60),2,true,'0')+'m' ;
		v /= 60 ; if (v<100'000 ) return sign+widen(cat(v     ),5,true)+'h'                                   ;
		v /= 24 ; if (v<100'000 ) return sign+widen(cat(v     ),5,true)+'j'                                   ;
		#pragma GCC diagnostic pop
		/**/                      return "forevr"                                                             ; // ensure  size is 6
	}

	//
	// CoarseDelay
	//

	::string& operator+=( ::string& os , CoarseDelay const cd ) { return os<<Delay(cd) ; }

	//
	// Date
	//

	::string& operator+=( ::string& os , Ddate    const  d ) { return os <<"DD:" << d.str(9) <<':'<< d.tag() ; }
	::string& operator+=( ::string& os , Pdate    const  d ) { return os <<"PD:" << d.str(9)                 ; }

	::string Date::str( uint8_t prec , bool in_day ) const {
		if (!self) return "None" ;
		time_t   s   = sec()                 ;
		::string res ( (in_day?0:11)+8 , 0 ) ;                               // time in seconds : YYYY-MM-DD hh:mm:ss
		::tm     t   ;
		::localtime_r(&s,&t) ;
		::strftime( res.data() , res.size()+1 , in_day?"%T":"%F %T" , &t ) ; // +1 to account for terminating null
		_add_frac(res,nsec_in_s(),prec) ;                                    // then add sub-second part
		return res ;
	}

	::string Date::day_str() const {
		if (!self) return "None" ;
		time_t   s   = sec()    ;
		::string res ( 10 , 0 ) ;                             // time in seconds : YYYY-MM-DD hh:mm:ss
		::tm     t   ;
		::localtime_r(&s,&t) ;
		::strftime( res.data() , res.size()+1 , "%F" , &t ) ; // +1 to account for terminating null
		return res ;
	}

	Date::Date(::string_view s) {
		{	struct tm   t    = {}                              ;                                                                    // zero out all fields
			const char* end  = ::strptime(s.data(),"%F %T",&t) ; throw_unless(end              , "cannot read date & time : ",s ) ;
			time_t      secs = ::mktime(&t)                    ; throw_unless(secs!=time_t(-1) , "cannot read date & time : ",s ) ;
			self = Date(secs) ;
			if (*end=='.') {
				end++ ;
				uint64_t ns = 0 ;
				for( uint32_t m=1'000'000'000 ; *end>='0'&&*end<='9' ; m/=10,end++ ) ns += (*end-'0')*m ;
				_val += ns*TicksPerSecond/1'000'000'000 ;
			}
			switch (*end) {
				case '+' :
				case '-' : {
					end++ ;
					const char* col = ::strchr(end,':')                                ;
					int         h   =       from_string<int>(end,true/*empty_ok*/)     ;
					int         m   = col ? from_string<int>(col,true/*empty_ok*/) : 0 ;
					if (*end=='+') _val += (h*3600+m*60)*TicksPerSecond ;
					else           _val -= (h*3600+m*60)*TicksPerSecond ;
				} break ;
			}
		}
	}

	//
	// Pdate
	//

	#if __cplusplus<202600L
		Mutex<MutexLvl::PdateNew> Pdate::_s_mutex_new ;
		Pdate::Tick               Pdate::_s_min_next  ;
	#else
		Atomic<Pdate::Tick> Pdate::_s_min_next ;
	#endif

	Pdate::Pdate(NewType) {
		TimeSpec now_ts ;            ::clock_gettime(CLOCK_REALTIME,&now_ts) ;
		Pdate    now    { now_ts } ;
		#if __cplusplus<202600L
			Lock lock { _s_mutex_new } ;                     // ::atomic::fetch_max is not available, so use a mutex to ensure atomic fetch and max
			_s_min_next = ::max( _s_min_next , now.val() ) ;
		#else
			_s_min_next.fetch_max(now.val()) ;
		#endif
		self = Pdate(New,_s_min_next++) ;
	}

}
