// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "fd.hh"
#include "hash.hh"

namespace Hash {
	using namespace Disk ;

	//
	// Crc
	//

	::string& operator+=( ::string& os , Crc const crc ) {
		CrcSpecial special{crc} ;
		if (special==CrcSpecial::Plain) return os << "Crc("<<::string(crc)<<')' ;
		else                            return os << "Crc("<<special      <<')' ;
	}

	// START_OF_VERSIONING
	Crc::Crc(::string const& file_name) {
		// use low level operations to ensure no time-of-check-to time-of-use hasards as crc may be computed on moving files
		self = None ;
		if ( AcFd fd = ::open(file_name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC) ; +fd ) {
			FileInfo fi  { fd }                         ;
			::string buf ( ::min(DiskBufSz,fi.sz) , 0 ) ;
			switch (fi.tag()) {
				case FileTag::Empty :
					self = Empty ;
				break ;
				case FileTag::Reg :
				case FileTag::Exe : {
					Xxh ctx { fi.tag() } ;
					for( size_t sz=fi.sz ;;) {
						ssize_t cnt = ::read( fd , buf.data() , buf.size() ) ;
						if      (cnt> 0) ctx += ::string_view(buf.data(),cnt) ;
						else if (cnt==0) break ;                                // file could change while crc is being computed
						else switch (errno) {
							case EAGAIN :
							case EINTR  : continue                                        ;
							default     : throw "I/O error while reading file "+file_name ;
						}
						SWEAR(cnt>0,cnt) ;
						if (size_t(cnt)>=sz) break ;
						sz -= cnt ;
					}
					self = ctx.digest() ;
				} break ;
			DN}
		} else if ( ::string lnk_target = read_lnk(file_name) ; +lnk_target ) {
			Xxh ctx { FileTag::Lnk } ;
			ctx += ::string_view( lnk_target.data() , lnk_target.size() ) ;     // no need to compute crc on size as would be the case with ctx += lnk_target
			self = ctx.digest() ;
		}
	}
	// END_OF_VERSIONING

	Crc::operator ::string() const {
		switch (CrcSpecial(self)) {
			case CrcSpecial::Unknown : return "unknown"                  ;
			case CrcSpecial::Lnk     : return "unknown-L"                ;
			case CrcSpecial::Reg     : return "unknown-R"                ;
			case CrcSpecial::None    : return "none"                     ;
			case CrcSpecial::Empty   : return "empty-R"                  ;
			case CrcSpecial::Plain   : return hex()+(is_lnk()?"-L":"-R") ;
		DF}
	}

	::string Crc::hex() const {
		::string res ; res.reserve(sizeof(_val)*2+2) ; // +2 to allow suffix addition
		for( size_t i : iota(sizeof(_val)) ) {
			uint8_t b = _val>>(i*8) ;
			{ uint8_t d = b>>4  ; res += char( d<10 ? '0'+d : 'a'+d-10 ) ; }
			{ uint8_t d = b&0xf ; res += char( d<10 ? '0'+d : 'a'+d-10 ) ; }
		}
		return res ;
	}

	Accesses Crc::diff_accesses( Crc other ) const {
		if ( valid() && other.valid() ) {            // if either does not represent a precise content, assume contents are different
			uint64_t diff = _val ^ other._val ;
			if (! diff                                       ) return {} ;                                                                    // crc's are identical, cannot perceive difference
			if (!(diff&ChkMsk) && (_plain()||other._plain()) ) fail_prod("near checksum clash, must increase CRC size",self,"versus",other) ;
		}
		// qualify the accesses that can perceive the difference
		Accesses res = ~Accesses() ;
		if (is_reg()) {
			if      (other.is_reg()  ) res =  Access::Reg ; // regular accesses see modifications of regular files
			else if (other==Crc::None) res = ~Access::Lnk ; // readlink accesses cannot see the difference between no file and a regular file
		} else if (is_lnk()) {
			if      (other.is_lnk()  ) res =  Access::Lnk ; // only readlink accesses see modifications of links
			else if (other==Crc::None) res = ~Access::Reg ; // regular accesses cannot see the difference between no file and a link
		} else if (self==Crc::None) {
			if      (other.is_reg()  ) res = ~Access::Lnk ; // readlink accesses cannot see the difference between no file and a regular file
			else if (other.is_lnk()  ) res = ~Access::Reg ; // regular  accesses cannot see the difference between no file and a link
		}
		return res ;
	}

	//
	// Xxh
	//

	char                  Xxh::_s_lnk_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	char                  Xxh::_s_exe_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	Mutex<MutexLvl::Hash> Xxh::_s_salt_init_mutex                  ;
	bool                  Xxh::_s_salt_inited                      = false ;

	Xxh::Xxh() {
		XXH3_INITSTATE   (&_state) ;
		XXH3_64bits_reset(&_state) ;
	}
	Xxh::Xxh(FileTag tag) : is_lnk{No|(tag==FileTag::Lnk)} {
		XXH3_INITSTATE(&_state) ;
		switch (tag) {
			case FileTag::Reg :                  XXH3_64bits_reset           ( &_state                                         ) ; break ;
			case FileTag::Lnk : _s_init_salt() ; XXH3_64bits_reset_withSecret( &_state , _s_lnk_secret , sizeof(_s_lnk_secret) ) ; break ;
			case FileTag::Exe : _s_init_salt() ; XXH3_64bits_reset_withSecret( &_state , _s_exe_secret , sizeof(_s_exe_secret) ) ; break ;
		DF}
	}

	Crc Xxh::digest() const {
		if ( is_lnk==Maybe && !seen_data ) return Crc(0)                                   ; // reserve 0 as unknown
		else                               return { XXH3_64bits_digest(&_state) , is_lnk } ;
	}
	Xxh& Xxh::operator+=(::string_view sv) {
		seen_data |= sv.size() ;
		XXH3_64bits_update( &_state , sv.data() , sv.size() ) ;
		return self ;
	}

}
