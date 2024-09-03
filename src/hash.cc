// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "fd.hh"
#include "hash.hh"

namespace Hash {
	using namespace Disk ;

	//
	// Crc
	//

	::ostream& operator<<( ::ostream& os , Crc const crc ) {
		CrcSpecial special{crc} ;
		if (special==CrcSpecial::Plain  ) return os << "Crc("<<::string(crc)<<','<<(crc.is_lnk()?'L':'R')<<')' ;
		else                              return os << "Crc("<<special<<')'                                    ;
	}

	Crc::Crc(::string const& filename) {
		// use low level operations to ensure no time-of-check-to time-of-use hasards as crc may be computed on moving files
		*this = None ;
		if ( AutoCloseFd fd = ::open(filename.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC) ; +fd ) {
			FileTag tag       = FileInfo(fd).tag() ;
			char    buf[4096] ;
			switch (tag) {
				case FileTag::Empty :
					*this = Empty ;
				break ;
				case FileTag::Reg :
				case FileTag::Exe : {
					Xxh ctx { tag } ;
					for(;;) {
						ssize_t cnt = ::read( fd , buf , sizeof(buf) ) ;
						if      (cnt> 0) ctx.update(buf,cnt) ;
						else if (cnt==0) break ;
						else switch (errno) {
							case EAGAIN :
							case EINTR  : continue ;
							default     : throw "I/O error while reading file "+filename ;
						}
					}
					*this = ctx.digest() ;
				} break ;
				default : ;
			}
		} else if ( ::string lnk_target = read_lnk(filename) ; +lnk_target ) {
			Xxh ctx{FileTag::Lnk} ;
			ctx.update(lnk_target) ;
			*this = ctx .digest() ;
		}
	}

	Crc::operator ::string() const {
		if ( CrcSpecial special=CrcSpecial(*this) ; special<CrcSpecial::Plain ) return ::string(snake(special)) ;
		::string res ; res.reserve(sizeof(_val)*2) ;
		for( size_t i=0 ; i<sizeof(_val) ; i++ ) {
			uint8_t b = _val>>(i*8) ;
			for( uint8_t d : {b>>4,b&0xf} ) res.push_back( d<10 ? '0'+d : 'a'+d-10 ) ;
		}
		return res ;
	}

	Accesses Crc::diff_accesses( Crc other ) const {
		if ( valid() && other.valid() ) {            // if either does not represent a precise content, assume contents are different
			uint64_t diff = _val ^ other._val ;
			if (! diff                                       ) return {} ;                                                                   // crc's are identical, cannot perceive difference
			if (!(diff&ChkMsk) && (_plain()||other._plain()) ) fail_prod("near crc match, must increase CRC size",*this,"versus",other) ;
		}
		// qualify the accesses that can perceive the difference
		Accesses res = ~Accesses() ;
		if (is_reg()) {
			if      (other.is_reg()  ) res =  Access::Reg ; // regular accesses see modifications of regular files
			else if (other==Crc::None) res = ~Access::Lnk ; // readlink accesses cannot see the difference between no file and a regular file
		} else if (is_lnk()) {
			if      (other.is_lnk()  ) res =  Access::Lnk ; // only readlink accesses see modifications of links
			else if (other==Crc::None) res = ~Access::Reg ; // regular accesses cannot see the difference between no file and a link
		} else if (*this==Crc::None) {
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
	Xxh::Xxh(FileTag tag) : is_lnk{tag==FileTag::Lnk} {
		XXH3_INITSTATE(&_state) ;
		switch (tag) {
			case FileTag::Reg :                  XXH3_64bits_reset           ( &_state                                         ) ; break ;
			case FileTag::Lnk : _s_init_salt() ; XXH3_64bits_reset_withSecret( &_state , _s_lnk_secret , sizeof(_s_lnk_secret) ) ; break ;
			case FileTag::Exe : _s_init_salt() ; XXH3_64bits_reset_withSecret( &_state , _s_exe_secret , sizeof(_s_exe_secret) ) ; break ;
		DF}
	}

	Crc  Xxh::digest  (                           ) const { return { XXH3_64bits_digest(&_state     ) , is_lnk } ; }
	void Xxh::_update ( const void* p , size_t sz )       {          XXH3_64bits_update(&_state,p,sz) ;            }

}
