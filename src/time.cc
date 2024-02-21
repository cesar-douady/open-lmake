// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "time.hh"

namespace Time {

	using namespace Disk ;

	//
	// Delay
	//

	::ostream& operator<<( ::ostream& os , Delay const d ) {
		int64_t  s  =       d.sec      ()  ;
		uint32_t ns = ::abs(d.nsec_in_s()) ;
		/**/                  os << "D:"                                                 ;
		if ( !s && d._val<0 ) os << '-'                                                  ;
		return                os << to_string(s,'.',::setfill('0'),::setw(9),::right,ns) ;
	}

	::string Delay::short_str() const {
		Tick     v      = msec() ;
		bool     is_neg = v<0    ;
		::string res    ;
		if (is_neg) v = -v ;
		/**/      if (v< 10*1000) { res = to_string(::right,::setw(1),v/1000,'.',::setfill('0'),::setw(3),v%1000,'s') ; goto Return ; }
		v /= 10 ; if (v< 60* 100) { res = to_string(::right,::setw(2),v/ 100,'.',::setfill('0'),::setw(2),v% 100,'s') ; goto Return ; }
		v /=100 ; if (v< 60*  60) { res = to_string(::right,::setw(2),v/  60,'m',::setfill('0'),::setw(2),v%  60,'s') ; goto Return ; }
		v /= 60 ; if (v< 24*  60) { res = to_string(::right,::setw(2),v/  60,'h',::setfill('0'),::setw(2),v%  60,'m') ; goto Return ; }
		v /= 60 ; if (v<100*  24) { res = to_string(::right,::setw(2),v/  60,'j',::setfill('0'),::setw(2),v%  60,'h') ; goto Return ; }
		v /= 24 ; if (v<100'000l) { res = to_string(::right,::setw(5),v,'j'                                         ) ; goto Return ; }
		/**/                      { res = "forevr"                                                                    ; goto Return ; }
	Return :
		if (is_neg) return '-'+res ;
		else        return     res ;
	}

	//
	// CoarseDelay
	//

	::ostream& operator<<( ::ostream& os , CoarseDelay const cd ) { return os<<Delay(cd) ; }

	//
	// Date
	//

	::ostream& operator<<( ::ostream& os , Ddate const d ) { return os <<"DD:" << d.str(9) ; }
	::ostream& operator<<( ::ostream& os , Pdate const d ) { return os <<"PD:" << d.str(9) ; }

	::string Date::str( uint8_t prec , bool in_day ) const {
		if (!*this) return "None" ;
		time_t        s   = sec      () ;
		uint32_t      ns  = nsec_in_s() ;
		OStringStream out ;
		struct tm     t   ;
		::localtime_r(&s,&t) ;
		out << ::put_time( &t , in_day?"%T":"%F %T" ) ;
		if (prec) {
			for( int i=prec ; i<9 ; i++ ) ns /= 10 ;
			out <<'.'<< ::setfill('0')<<::setw(prec)<<::right<<ns ;
		}
		return out.str() ;
	}

	Date::Date(::string_view const& s) {
		{	struct tm   t    = {}                                                                                                      ; // zero out all fields
			const char* end  = ::strptime(s.data(),"%F %T",&t) ; if (!end            ) throw to_string("cannot read date & time : ",s) ;
			time_t      secs = ::mktime(&t)                    ; if (secs==time_t(-1)) throw to_string("cannot read date & time : ",s) ;
			*this = Date(secs) ;
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

}
