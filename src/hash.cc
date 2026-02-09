// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "fd.hh"
#include "hash.hh"

namespace Hash {
	using namespace Disk ;

	//
	// Xxh
	//

	template<uint8_t Sz> char    _Xxh<Sz>::_s_lnk_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	template<uint8_t Sz> char    _Xxh<Sz>::_s_exe_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	template<uint8_t Sz> Mutex<> _Xxh<Sz>::_s_salt_init_mutex                  ;
	template<uint8_t Sz> bool    _Xxh<Sz>::_s_salt_inited                      = false ;
	//                   Sz
	template struct _Xxh<64> ; // explicit instanciation of static variables
	template struct _Xxh<96> ; // .

	template<uint8_t Sz> _Xxh<Sz>::_Xxh() {
		/**/    XXH3_INITSTATE    (&_state) ;
		if (Sz) XXH3_128bits_reset(&_state) ;
		else    XXH3_64bits_reset (&_state) ;
	} //!         Sz
	template _Xxh<64>::_Xxh() ; // explicit instanciation
	template _Xxh<96>::_Xxh() ; // .

	template<uint8_t Sz> _Xxh<Sz>::_Xxh(FileTag tag) : is_lnk{No|(tag==FileTag::Lnk)} {
		XXH3_INITSTATE(&_state) ;
		if (Sz>64)
			switch (tag) {
				case FileTag::Reg :                  XXH3_128bits_reset           ( &_state                                         ) ; break ;
				case FileTag::Lnk : _s_init_salt() ; XXH3_128bits_reset_withSecret( &_state , _s_lnk_secret , sizeof(_s_lnk_secret) ) ; break ;
				case FileTag::Exe : _s_init_salt() ; XXH3_128bits_reset_withSecret( &_state , _s_exe_secret , sizeof(_s_exe_secret) ) ; break ;
			DF}                                                                                                                                 // NO_COV
		else
			switch (tag) {
				case FileTag::Reg :                  XXH3_64bits_reset           ( &_state                                         ) ; break ;
				case FileTag::Lnk : _s_init_salt() ; XXH3_64bits_reset_withSecret( &_state , _s_lnk_secret , sizeof(_s_lnk_secret) ) ; break ;
				case FileTag::Exe : _s_init_salt() ; XXH3_64bits_reset_withSecret( &_state , _s_exe_secret , sizeof(_s_exe_secret) ) ; break ;
			DF}                                                                                                                                 // NO_COV
	} //!         Sz
	template _Xxh<64>::_Xxh(FileTag tag) ;                                                                                                      // explicit instanciation
	template _Xxh<96>::_Xxh(FileTag tag) ;                                                                                                      // .

	template<uint8_t Sz> _Crc<Sz> _Xxh<Sz>::digest() const {
		if                ( is_lnk==Maybe && !seen_data ) return {}                                       ;
		else if constexpr ( Sz<=64                      ) return { XXH3_64bits_digest(&_state) , is_lnk } ;
		else {
			if ( is_lnk==Maybe && !seen_data ) return {} ;
			XXH128_hash_t d = XXH3_128bits_digest(&_state) ;
			typename _Crc<Sz>::Val v = d.low64 ; // stay in 64 bits, prevent compiler from complaining if we kept 64
			#if HAS_UINT128
				v |= uint128_t(d.high64)<<64 ;
			#endif
			return { v&_Crc<Sz>::Msk , is_lnk } ;
		}
	} //!         Sz       Sz
	template _Crc<64> _Xxh<64>::digest() const ;                    // explicit instanciation
	template _Crc<96> _Xxh<96>::digest() const ;                    // .

	template<uint8_t Sz> _Xxh<Sz>& _Xxh<Sz>::operator+=(::string_view sv) {
		seen_data |= sv.size() ;
		if (Sz>64) XXH3_128bits_update( &_state , sv.data() , sv.size() ) ;
		else       XXH3_64bits_update ( &_state , sv.data() , sv.size() ) ;
		return self ;
	} //!         Sz        Sz
	template _Xxh<64>& _Xxh<64>::operator+=(::string_view sv) ; // explicit instanciation
	template _Xxh<96>& _Xxh<96>::operator+=(::string_view sv) ; // .

	//
	// Crc
	//

	template<uint8_t Sz> ::string& operator+=( ::string& os , _Crc<Sz> const crc ) { // START_OF_NO_COV
		CrcSpecial special { crc } ;
		/**/                            os << "Crc"         ;
		if (Sz!=64                    ) os << Sz            ;
		/**/                            os << '('           ;
		if (special==CrcSpecial::Plain) os << ::string(crc) ;
		else                            os << special       ;
		return                          os << ')'           ;
	}                                                                                // END_OF_NO_COV
	//!                                                Sz
	template ::string& operator+=( ::string& os , _Crc<64> const crc ) ;             // explicit instanciation
	template ::string& operator+=( ::string& os , _Crc<96> const crc ) ;             // .

	// START_OF_VERSIONING CACHE JOB REPO
	template<uint8_t Sz> _Crc<Sz>::_Crc(::string const& filename) {
		// use low level operations to ensure no time-of-check-to time-of-use hasards as crc may be computed on moving files
		self = None ;
		if ( AcFd fd{filename,{.flags=O_RDONLY|O_NOFOLLOW,.err_ok=true}} ; +fd ) {
			FileInfo fi { fd } ;
			switch (fi.tag()) {
				case FileTag::Empty :
					self = Empty ;
				break ;
				case FileTag::Reg :
				case FileTag::Exe : {
					_Xxh<Sz> ctx { fi.tag() }                   ;
					::string buf ( ::min(DiskBufSz,fi.sz) , 0 ) ;
					for( size_t sz=fi.sz ;;) {
						ssize_t cnt = ::read( fd , buf.data() , buf.size() ) ;
						if      (cnt> 0) ctx += ::string_view(buf.data(),cnt) ;
						else if (cnt==0) break ;                                // file could change while crc is being computed
						else switch (errno) {
							#if EWOULDBLOCK!=EAGAIN
								case EWOULDBLOCK :
							#endif
							case EAGAIN :
							case EINTR  : continue                                       ;
							default     : throw "I/O error while reading file "+filename ;
						}
						SWEAR( cnt>0 , cnt ) ;
						if (size_t(cnt)>=sz) break ;
						sz -= cnt ;
					}
					self = ctx.digest() ;
				} break ;
			DN}
		} else if ( ::string lnk_target=read_lnk(filename) ; +lnk_target ) {
			_Xxh<Sz> ctx { FileTag::Lnk } ;
			ctx += ::string_view( lnk_target.data() , lnk_target.size() ) ;     // no need to compute crc on size as would be the case with ctx += lnk_target
			self = ctx.digest() ;
		}
	}
	// END_OF_VERSIONING
	//            Sz
	template _Crc<64>::_Crc(::string const& filename) ;                         // explicit instanciation
	template _Crc<96>::_Crc(::string const& filename) ;                         // .

	template<uint8_t Sz> _Crc<Sz>::operator ::string() const {
		switch (CrcSpecial(self)) {
			case CrcSpecial::Unknown : return "unknown"                  ;
			case CrcSpecial::Lnk     : return "unknown-L"                ;
			case CrcSpecial::Reg     : return "unknown-R"                ;
			case CrcSpecial::None    : return "none"                     ;
			case CrcSpecial::Empty   : return "empty-R"                  ;
			case CrcSpecial::Plain   : return hex()+(is_lnk()?"-L":"-R") ;
		DF}                                                                // NO_COV
	} //!         Sz
	template _Crc<64>::operator ::string() const ;                         // explicit instanciation
	template _Crc<96>::operator ::string() const ;                         // .

	template<uint8_t Sz> ::string _Crc<Sz>::hex() const {
		static_assert( HexSz%2==0 ) ;                     // else handle last digit
		::string res ;        res.reserve(HexSz) ;
		Val      v   = _val ;
		for( [[maybe_unused]] uint8_t i : iota(HexSz/2) ) {
			{ uint8_t d = (v>>4)&0xf ; res << char( d<10 ? '0'+d : 'a'+d-10 ) ; }
			{ uint8_t d =  v    &0xf ; res << char( d<10 ? '0'+d : 'a'+d-10 ) ; }
			v >>= 8 ;
		}
		return res ;
	} //!                  Sz
	template ::string _Crc<64>::hex() const ;             // explicit instanciation
	template ::string _Crc<96>::hex() const ;             // .

	template<uint8_t Sz> _Crc<Sz> _Crc<Sz>::s_from_hex(::string_view sv) {
		static_assert( HexSz%2==0 ) ;                                           // else handle last digit
		throw_unless( sv.size()==HexSz , "bad size : ",sv.size(),"!=",HexSz ) ;
		_Crc res { Val(0) } ;
		for( uint8_t i : iota(HexSz/2) ) {
			char msb = sv[HexSz-2*i-2] ;
			char lsb = sv[HexSz-2*i-1] ;
			uint8_t b =
				( '0'<=msb && msb<='9' ? msb-'0' : 'a'<=msb && msb<='f' ? 10+msb-'a' : throw cat("bad hex digit : ",msb) )<<4
			|	( '0'<=lsb && lsb<='9' ? lsb-'0' : 'a'<=lsb && lsb<='f' ? 10+lsb-'a' : throw cat("bad hex digit : ",lsb) )
			;
			res._val <<= 8      ;
			res._val |=  Val(b) ;
		}
		return res ;
	} //!         Sz       Sz
	template _Crc<64> _Crc<64>::s_from_hex(::string_view sv) ;                  // explicit instanciation
	template _Crc<96> _Crc<96>::s_from_hex(::string_view sv) ;                  // .

	template<uint8_t Sz> ::string _Crc<Sz>::base64() const {
		::string res ;        res.reserve(Base64Sz) ;
		Val      v   = _val ;
		for( [[maybe_unused]] uint8_t i : iota(Base64Sz) ) {
			uint8_t d = v&0x3f ;
			if      (d< 26) res << char('A'+d   ) ;
			else if (d< 52) res << char('a'+d-26) ;
			else if (d< 62) res << char('0'+d-52) ;
			else if (d==62) res <<      '-'       ;
			else            res <<      '_'       ;
			v >>= 6 ;
		}
		return res ;
	} //!                  Sz
	template ::string _Crc<64>::base64() const ; // explicit instanciation
	template ::string _Crc<96>::base64() const ; // .

	template<uint8_t Sz> _Crc<Sz> _Crc<Sz>::s_from_base64(::string_view sv) {
		throw_unless( sv.size()==Base64Sz , "bad size : ",sv.size(),"!=",Base64Sz ) ;
		_Crc res { Val(0) } ;
		for( uint8_t i : iota(Base64Sz) ) {
			char c = sv[Base64Sz-i-1] ;
			res._val <<= 6 ;
			if      ('A'<=c && c<='Z') res._val |= uint8_t(   c-'A') ;
			else if ('a'<=c && c<='z') res._val |= uint8_t(26+c-'a') ;
			else if ('0'<=c && c<='9') res._val |= uint8_t(52+c-'0') ;
			else if (c=='-'          ) res._val |= uint8_t(62      ) ;
			else if (c=='_'          ) res._val |= uint8_t(63      ) ;
			else                       throw cat("bad base64 digit : ",c) ;
		}
		return res ;
	} //!         Sz       Sz
	template _Crc<64> _Crc<64>::s_from_base64(::string_view sv) ; // explicit instanciation
	template _Crc<96> _Crc<96>::s_from_base64(::string_view sv) ; // .

	template<uint8_t Sz> Accesses _Crc<Sz>::diff_accesses(_Crc<Sz> crc) const {
		if ( valid() && crc.valid() ) {                                         // if either does not represent a precise content, assume contents are different
			uint64_t diff = _val ^ crc._val ;
			if (! diff                                     ) return {} ;                                                                  // crc's are identical, cannot perceive difference
			if (!(diff&ChkMsk) && (_plain()||crc._plain()) ) fail_prod("near checksum clash, must increase CRC size",self,"versus",crc) ;
		}
		// qualify the accesses that can perceive the difference
		Accesses res = FullAccesses ;
		if (is_reg()) {
			if      (crc.is_reg()   ) res =  Access::Reg  ;     // regular accesses see modifications of regular files
			else if (crc.is_lnk()   ) res = ~Access::Stat ;     // both exist, Stat does not see the difference
			else if (crc==_Crc::None) res = ~Access::Lnk  ;     // readlink accesses cannot see the difference between no file and a regular file
		} else if (is_lnk()) {
			if      (crc.is_reg()   ) res = ~Access::Stat ;     // both exist, Stat does not see the difference
			else if (crc.is_lnk()   ) res =  Access::Lnk  ;     // only readlink accesses see modifications of links
			else if (crc==_Crc::None) res = ~Access::Reg  ;     // regular accesses cannot see the difference between no file and a link
		} else if (self==_Crc::None) {
			if      (crc.is_reg()   ) res = ~Access::Lnk  ;     // readlink accesses cannot see the difference between no file and a regular file
			else if (crc.is_lnk()   ) res = ~Access::Reg  ;     // regular  accesses cannot see the difference between no file and a link
		}
		return res ;
	} //!                  Sz                      Sz
	template Accesses _Crc<64>::diff_accesses(_Crc<64>) const ; // explicit instanciation
	template Accesses _Crc<96>::diff_accesses(_Crc<96>) const ; // .

}
