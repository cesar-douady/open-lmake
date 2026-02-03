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

	template<bool Is128> char    _Xxh<Is128>::_s_lnk_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	template<bool Is128> char    _Xxh<Is128>::_s_exe_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	template<bool Is128> Mutex<> _Xxh<Is128>::_s_salt_init_mutex                  ;
	template<bool Is128> bool    _Xxh<Is128>::_s_salt_inited                      = false ;
	//                   Is128
	template struct _Xxh<false> ; // explicit instanciation of static variables
	template struct _Xxh<true > ; // .

	template<bool Is128> _Xxh<Is128>::_Xxh() {
		/**/       XXH3_INITSTATE    (&_state) ;
		if (Is128) XXH3_128bits_reset(&_state) ;
		else       XXH3_64bits_reset (&_state) ;
	} //!         Is128
	template _Xxh<false>::_Xxh() ; // explicit instanciation
	template _Xxh<true >::_Xxh() ; // .

	template<bool Is128> _Xxh<Is128>::_Xxh(FileTag tag) : is_lnk{No|(tag==FileTag::Lnk)} {
		XXH3_INITSTATE(&_state) ;
		if (Is128)
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
	} //!         Is128
	template _Xxh<false>::_Xxh(FileTag tag) ;                                                                                                   // explicit instanciation
	template _Xxh<true >::_Xxh(FileTag tag) ;                                                                                                   // .

	template<bool Is128> _Crc<Is128> _Xxh<Is128>::digest() const {
		if                ( is_lnk==Maybe && !seen_data ) return {}                                        ;
		else if constexpr ( !Is128                      ) return { XXH3_64bits_digest(&_state) , is_lnk } ;
		else {
			if ( is_lnk==Maybe && !seen_data ) return {} ;
			XXH128_hash_t d = XXH3_128bits_digest(&_state) ;
			#if HAS_UINT128
				uint128_t v = (uint128_t(d.high64)<<64) | d.low64 ;
			#else
				uint64_t  v =            d.high64       ^ d.low64 ; // stay in 64 bits, prevent compiler from complaining if we kept 64
			#endif
			return { v , is_lnk } ;
		}
	} //!         Is128       Is128
	template _Crc<false> _Xxh<false>::digest() const ;              // explicit instanciation
	template _Crc<true > _Xxh<true >::digest() const ;              // .

	template<bool Is128> _Xxh<Is128>& _Xxh<Is128>::operator+=(::string_view sv) {
		seen_data |= sv.size() ;
		if (Is128) XXH3_128bits_update( &_state , sv.data() , sv.size() ) ;
		else       XXH3_64bits_update ( &_state , sv.data() , sv.size() ) ;
		return self ;
	} //!         Is128        Is128
	template _Xxh<false>& _Xxh<false>::operator+=(::string_view sv) ; // explicit instanciation
	template _Xxh<true >& _Xxh<true >::operator+=(::string_view sv) ; // .

	//
	// Crc
	//

	template<bool Is128> ::string& operator+=( ::string& os , _Crc<Is128> const crc ) { // START_OF_NO_COV
		CrcSpecial special { crc } ;
		/**/                            os << "Crc"         ;
		if (Is128/**/                 ) os << "128"         ;
		/**/                            os << '('           ;
		if (special==CrcSpecial::Plain) os << ::string(crc) ;
		else                            os << special       ;
		return                          os << ')'           ;
	}                                                                                   // END_OF_NO_COV
	//!                                                Is128
	template ::string& operator+=( ::string& os , _Crc<false> const crc ) ;             // explicit instanciation
	template ::string& operator+=( ::string& os , _Crc<true > const crc ) ;             // .

	// START_OF_VERSIONING
	template<bool Is128> _Crc<Is128>::_Crc(::string const& filename) {
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
					_Xxh<Is128> ctx { fi.tag() }                   ;
					::string    buf ( ::min(DiskBufSz,fi.sz) , 0 ) ;
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
			_Xxh<Is128> ctx { FileTag::Lnk } ;
			ctx += ::string_view( lnk_target.data() , lnk_target.size() ) ;     // no need to compute crc on size as would be the case with ctx += lnk_target
			self = ctx.digest() ;
		}
	}
	// END_OF_VERSIONING
	//            Is128
	template _Crc<false>::_Crc(::string const& filename) ;                      // explicit instanciation
	template _Crc<true >::_Crc(::string const& filename) ;                      // .

	template<bool Is128> _Crc<Is128>::operator ::string() const {
		switch (CrcSpecial(self)) {
			case CrcSpecial::Unknown : return "unknown"                  ;
			case CrcSpecial::Lnk     : return "unknown-L"                ;
			case CrcSpecial::Reg     : return "unknown-R"                ;
			case CrcSpecial::None    : return "none"                     ;
			case CrcSpecial::Empty   : return "empty-R"                  ;
			case CrcSpecial::Plain   : return hex()+(is_lnk()?"-L":"-R") ;
		DF}                                                                // NO_COV
	} //!         Is128
	template _Crc<false>::operator ::string() const ;                      // explicit instanciation
	template _Crc<true >::operator ::string() const ;                      // .

	template<bool Is128> ::string _Crc<Is128>::hex() const {
		::string res ; res.reserve(sizeof(_val)*2+2) ;       // +2 to allow suffix addition
		for( size_t i : iota(sizeof(_val)) ) {
			uint8_t b = _val>>(i*8) ;
			{ uint8_t d = b>>4  ; res += char( d<10 ? '0'+d : 'a'+d-10 ) ; }
			{ uint8_t d = b&0xf ; res += char( d<10 ? '0'+d : 'a'+d-10 ) ; }
		}
		return res ;
	} //!                  Is128
	template ::string _Crc<false>::hex() const ;             // explicit instanciation
	template ::string _Crc<true >::hex() const ;             // .

	template<bool Is128> _Crc<Is128> _Crc<Is128>::s_from_hex(::string_view sv) {
		throw_unless( sv.size()==HexSz , "bad size : ",sv.size(),"!=",HexSz ) ;
		_Crc res { Val(0) } ;
		for( size_t i : iota(sizeof(res._val)) ) {
			char msb = sv[2*i  ] ;
			char lsb = sv[2*i+1] ;
			uint8_t b =
				( '0'<=msb && msb<='9' ? msb-'0' : 'a'<=msb && msb<='f' ? 10+msb-'a' : throw cat("bad hex digit : ",msb) )<<4
			|	( '0'<=lsb && lsb<='9' ? lsb-'0' : 'a'<=lsb && lsb<='f' ? 10+lsb-'a' : throw cat("bad hex digit : ",lsb) )
			;
			res._val |= Val(b)<<(8*i) ;
		}
		return res ;
	} //!         Is128       Is128
	template _Crc<false> _Crc<false>::s_from_hex(::string_view sv) ; // explicit instanciation
	template _Crc<true > _Crc<true >::s_from_hex(::string_view sv) ; // .

	template<bool Is128> Accesses _Crc<Is128>::diff_accesses(_Crc<Is128> crc) const {
		if ( valid() && crc.valid() ) {                                               // if either does not represent a precise content, assume contents are different
			uint64_t diff = _val ^ crc._val ;
			if (! diff                                     ) return {} ;                                                                  // crc's are identical, cannot perceive difference
			if (!(diff&ChkMsk) && (_plain()||crc._plain()) ) fail_prod("near checksum clash, must increase CRC size",self,"versus",crc) ;
		}
		// qualify the accesses that can perceive the difference
		Accesses res = FullAccesses ;
		if (is_reg()) {
			if      (crc.is_reg()   ) res =  Access::Reg  ;           // regular accesses see modifications of regular files
			else if (crc.is_lnk()   ) res = ~Access::Stat ;           // both exist, Stat does not see the difference
			else if (crc==_Crc::None) res = ~Access::Lnk  ;           // readlink accesses cannot see the difference between no file and a regular file
		} else if (is_lnk()) {
			if      (crc.is_reg()   ) res = ~Access::Stat ;           // both exist, Stat does not see the difference
			else if (crc.is_lnk()   ) res =  Access::Lnk  ;           // only readlink accesses see modifications of links
			else if (crc==_Crc::None) res = ~Access::Reg  ;           // regular accesses cannot see the difference between no file and a link
		} else if (self==_Crc::None) {
			if      (crc.is_reg()   ) res = ~Access::Lnk  ;           // readlink accesses cannot see the difference between no file and a regular file
			else if (crc.is_lnk()   ) res = ~Access::Reg  ;           // regular  accesses cannot see the difference between no file and a link
		}
		return res ;
	} //!                  Is128                      Is128
	template Accesses _Crc<false>::diff_accesses(_Crc<false>) const ; // explicit instanciation
	template Accesses _Crc<true >::diff_accesses(_Crc<true >) const ; // .

}
