// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

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

	Crc::Crc( FileInfo const& fi , ::string const& filename , Algo algo ) {
		FileTag tag = fi.tag() ;
		switch (tag) {
			case FileTag::Reg :
			case FileTag::Exe :
				if (!fi.sz) {
					*this = Empty ;
				} else {
					FileMap map{filename} ;
					if (!map) return ;
					switch (algo) { //!                   vvvvvvvvvvvvvvvvvvvvvvvvvvv           vvvvvvvvvvvvvvvvvvvv
						case Algo::Md5 : { Md5 ctx{tag} ; ctx.update(map.data,map.sz) ; *this = ::move(ctx).digest() ; } break ;
						case Algo::Xxh : { Xxh ctx{tag} ; ctx.update(map.data,map.sz) ; *this =        ctx .digest() ; } break ;
					DF} //!                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^           ^^^^^^^^^^^^^^^^^^^^
				}
			break ;
			case FileTag::Lnk : {
				::string lnk_target = read_lnk(filename) ;
				switch (algo) { //!                   vvvvvvvvvvvvvvvvvvvvvv           vvvvvvvvvvvvvvvvvvvv
					case Algo::Md5 : { Md5 ctx{tag} ; ctx.update(lnk_target) ; *this = ::move(ctx).digest() ; } break ; // ensure CRC is distinguished from a regular file with same content
					case Algo::Xxh : { Xxh ctx{tag} ; ctx.update(lnk_target) ; *this =        ctx .digest() ; } break ; // .
				DF} //!                               ^^^^^^^^^^^^^^^^^^^^^^           ^^^^^^^^^^^^^^^^^^^^
			} break ;
			case FileTag::None :
			case FileTag::Dir  : *this = None ; break ;                                                                 // directories are deemed not to exist
			default : ;
		}
		if (file_date(filename)!=fi.date) *this = Crc(tag)  ;                                                           // file was moving, crc is unreliable
	}

	Crc::operator ::string() const {
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
			if (!(diff&ChkMsk) && (_plain()||other._plain()) ) fail_prod("near crc match, must increase CRC size ",*this," versus ",other) ;
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
	// _Md5
	// reimplement md5 as crypt lib crashes when freeing resources at exit
	//

	_Md5::_Md5() : _hash{0x67452301,0xefcdab89,0x98badcfe,0x10325476} , _cnt{0} {}

	void _Md5::_update( const void* p , size_t sz ) {
		FAIL() ;                                             // XXX : suppress md5 code altogether
		const uint8_t* pi = static_cast<const uint8_t*>(p) ;
		SWEAR(!_closed) ;
		if (!sz) return ;                                    // memcpy is declared with non-null pointers and p may be nullptr if sz==0, otherwise fast path
		// if first block is already partially filled, it must be handled specially
		uint32_t offset = _cnt & (sizeof(_blk)-1) ;
		_cnt += sz ;
		if (offset) {
			uint32_t avail = sizeof(_blk) - offset   ;
			uint32_t cnt   = ::min(sz,size_t(avail)) ;
			memcpy( _bblk()+offset , pi , cnt ) ;
			if (sz<avail) return ;
			//vvvvvvv
			_update64() ;
			//^^^^^^^
			sz -= cnt ;
			pi += cnt ;
		}
		// core loop : 2 versions whether we are lucky with properly aligned pi or not
		// note that when used with a mapped file (the practical case where we have big data), pi *is* properly aligned
		const uint8_t* sentinel     = pi + sz - sizeof(_blk) ;
		bool           p_is_aligned = (reinterpret_cast<uintptr_t>(pi)&(sizeof(uint32_t)-1))==0 ;
		//                                                                                            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (p_is_aligned) for(; pi<=sentinel ; pi+=sizeof(_blk) ) {                                   _update64(reinterpret_cast<const uint32_t*>(pi)) ; }
		else              for(; pi<=sentinel ; pi+=sizeof(_blk) ) { memcpy(_bblk(),pi,sizeof(_blk)) ; _update64(                                     ) ; }
		//                                                                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		// fill last partial block if necessary
		sz = sentinel + sizeof(_blk) - pi ;
		if (sz) memcpy( _bblk() , pi , sz ) ;
	}

	Crc _Md5::digest() && {                                        // XXX : suppress md5 code altogether
		if (!_closed) {                                            // this way, operator() can be called several times (but then no update possible)
			if (+_salt) _update( _salt.c_str() , _salt.size() ) ;
			uint32_t offset = _cnt & (sizeof(_blk)-1)   ;
			uint8_t* bp     = _bblk() + offset          ;
			uint32_t avail  = sizeof(_blk) - (offset+1) ;          // _blk is never full, so this is always >=0
			SWEAR( _salt.size()<0x80 , _salt.size() ) ;
			*bp++ = 0x80 + _salt.size() ;                          // if _salt is empty, it is plain md5, else it is guaranteed different than plain md5
			if (avail<sizeof(uint64_t)) {
				memset( bp , 0 , avail ) ;
				//vvvvvvvvv
				_update64() ;
				//^^^^^^^^^
				bp    = _bblk()      ;
				avail = sizeof(_blk) ;
			}
			memset( bp , 0 , avail-sizeof(uint64_t) ) ;
			reinterpret_cast<uint64_t&>(_blk[BlkSz-2]) = _cnt<<3 ; // append message length in bits
			//vvvvvvvvv
			_update64() ;
			//^^^^^^^^^
			_closed = true ;
		}
		return { *reinterpret_cast<uint64_t const*>(_hash) , is_lnk } ;
	}

	static inline void _round1( uint32_t& a , uint32_t b , uint32_t c , uint32_t d , uint32_t m , int s ) { a = b + ::rotl( ((b&c)|((~b)&d)) + a + m , s ) ; }
	static inline void _round2( uint32_t& a , uint32_t b , uint32_t c , uint32_t d , uint32_t m , int s ) { a = b + ::rotl( ((d&b)|((~d)&c)) + a + m , s ) ; }
	static inline void _round3( uint32_t& a , uint32_t b , uint32_t c , uint32_t d , uint32_t m , int s ) { a = b + ::rotl( (b^c^d         ) + a + m , s ) ; }
	static inline void _round4( uint32_t& a , uint32_t b , uint32_t c , uint32_t d , uint32_t m , int s ) { a = b + ::rotl( (c^(b|(~d))    ) + a + m , s ) ; }
	//
	void _Md5::_update64(const uint32_t _data[BlkSz]) {
		uint32_t a = _hash[0] ;
		uint32_t b = _hash[1] ;
		uint32_t c = _hash[2] ;
		uint32_t d = _hash[3] ;
		//
		_round1( a , b , c , d , _data[ 0] + 0xd76aa478 ,  7 ) ;
		_round1( d , a , b , c , _data[ 1] + 0xe8c7b756 , 12 ) ;
		_round1( c , d , a , b , _data[ 2] + 0x242070db , 17 ) ;
		_round1( b , c , d , a , _data[ 3] + 0xc1bdceee , 22 ) ;
		_round1( a , b , c , d , _data[ 4] + 0xf57c0faf ,  7 ) ;
		_round1( d , a , b , c , _data[ 5] + 0x4787c62a , 12 ) ;
		_round1( c , d , a , b , _data[ 6] + 0xa8304613 , 17 ) ;
		_round1( b , c , d , a , _data[ 7] + 0xfd469501 , 22 ) ;
		_round1( a , b , c , d , _data[ 8] + 0x698098d8 ,  7 ) ;
		_round1( d , a , b , c , _data[ 9] + 0x8b44f7af , 12 ) ;
		_round1( c , d , a , b , _data[10] + 0xffff5bb1 , 17 ) ;
		_round1( b , c , d , a , _data[11] + 0x895cd7be , 22 ) ;
		_round1( a , b , c , d , _data[12] + 0x6b901122 ,  7 ) ;
		_round1( d , a , b , c , _data[13] + 0xfd987193 , 12 ) ;
		_round1( c , d , a , b , _data[14] + 0xa679438e , 17 ) ;
		_round1( b , c , d , a , _data[15] + 0x49b40821 , 22 ) ;
		//
		_round2( a , b , c , d , _data[ 1] + 0xf61e2562 ,  5 ) ;
		_round2( d , a , b , c , _data[ 6] + 0xc040b340 ,  9 ) ;
		_round2( c , d , a , b , _data[11] + 0x265e5a51 , 14 ) ;
		_round2( b , c , d , a , _data[ 0] + 0xe9b6c7aa , 20 ) ;
		_round2( a , b , c , d , _data[ 5] + 0xd62f105d ,  5 ) ;
		_round2( d , a , b , c , _data[10] + 0x02441453 ,  9 ) ;
		_round2( c , d , a , b , _data[15] + 0xd8a1e681 , 14 ) ;
		_round2( b , c , d , a , _data[ 4] + 0xe7d3fbc8 , 20 ) ;
		_round2( a , b , c , d , _data[ 9] + 0x21e1cde6 ,  5 ) ;
		_round2( d , a , b , c , _data[14] + 0xc33707d6 ,  9 ) ;
		_round2( c , d , a , b , _data[ 3] + 0xf4d50d87 , 14 ) ;
		_round2( b , c , d , a , _data[ 8] + 0x455a14ed , 20 ) ;
		_round2( a , b , c , d , _data[13] + 0xa9e3e905 ,  5 ) ;
		_round2( d , a , b , c , _data[ 2] + 0xfcefa3f8 ,  9 ) ;
		_round2( c , d , a , b , _data[ 7] + 0x676f02d9 , 14 ) ;
		_round2( b , c , d , a , _data[12] + 0x8d2a4c8a , 20 ) ;
		//
		_round3( a , b , c , d , _data[ 5] + 0xfffa3942 ,  4 ) ;
		_round3( d , a , b , c , _data[ 8] + 0x8771f681 , 11 ) ;
		_round3( c , d , a , b , _data[11] + 0x6d9d6122 , 16 ) ;
		_round3( b , c , d , a , _data[14] + 0xfde5380c , 23 ) ;
		_round3( a , b , c , d , _data[ 1] + 0xa4beea44 ,  4 ) ;
		_round3( d , a , b , c , _data[ 4] + 0x4bdecfa9 , 11 ) ;
		_round3( c , d , a , b , _data[ 7] + 0xf6bb4b60 , 16 ) ;
		_round3( b , c , d , a , _data[10] + 0xbebfbc70 , 23 ) ;
		_round3( a , b , c , d , _data[13] + 0x289b7ec6 ,  4 ) ;
		_round3( d , a , b , c , _data[ 0] + 0xeaa127fa , 11 ) ;
		_round3( c , d , a , b , _data[ 3] + 0xd4ef3085 , 16 ) ;
		_round3( b , c , d , a , _data[ 6] + 0x04881d05 , 23 ) ;
		_round3( a , b , c , d , _data[ 9] + 0xd9d4d039 ,  4 ) ;
		_round3( d , a , b , c , _data[12] + 0xe6db99e5 , 11 ) ;
		_round3( c , d , a , b , _data[15] + 0x1fa27cf8 , 16 ) ;
		_round3( b , c , d , a , _data[ 2] + 0xc4ac5665 , 23 ) ;
		//
		_round4( a , b , c , d , _data[ 0] + 0xf4292244 ,  6 ) ;
		_round4( d , a , b , c , _data[ 7] + 0x432aff97 , 10 ) ;
		_round4( c , d , a , b , _data[14] + 0xab9423a7 , 15 ) ;
		_round4( b , c , d , a , _data[ 5] + 0xfc93a039 , 21 ) ;
		_round4( a , b , c , d , _data[12] + 0x655b59c3 ,  6 ) ;
		_round4( d , a , b , c , _data[ 3] + 0x8f0ccc92 , 10 ) ;
		_round4( c , d , a , b , _data[10] + 0xffeff47d , 15 ) ;
		_round4( b , c , d , a , _data[ 1] + 0x85845dd1 , 21 ) ;
		_round4( a , b , c , d , _data[ 8] + 0x6fa87e4f ,  6 ) ;
		_round4( d , a , b , c , _data[15] + 0xfe2ce6e0 , 10 ) ;
		_round4( c , d , a , b , _data[ 6] + 0xa3014314 , 15 ) ;
		_round4( b , c , d , a , _data[13] + 0x4e0811a1 , 21 ) ;
		_round4( a , b , c , d , _data[ 4] + 0xf7537e82 ,  6 ) ;
		_round4( d , a , b , c , _data[11] + 0xbd3af235 , 10 ) ;
		_round4( c , d , a , b , _data[ 2] + 0x2ad7d2bb , 15 ) ;
		_round4( b , c , d , a , _data[ 9] + 0xeb86d391 , 21 ) ;
		//
		_hash[0] += a ;
		_hash[1] += b ;
		_hash[2] += c ;
		_hash[3] += d ;
	}

	//
	// _Xxh
	//

	char    _Xxh::_s_lnk_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	char    _Xxh::_s_exe_secret[XXH3_SECRET_SIZE_MIN] = {}    ;
	::mutex _Xxh::_s_inited_mutex                     ;
	bool    _Xxh::_s_lnk_inited                       = false ;
	bool    _Xxh::_s_exe_inited                       = false ;

	_Xxh::_Xxh() {
		XXH3_INITSTATE   (&_state) ;
		XXH3_64bits_reset(&_state) ;
	}
	_Xxh::_Xxh(FileTag tag) : is_lnk{tag==FileTag::Lnk} {
		XXH3_INITSTATE(&_state) ;
		switch (tag) {
			case FileTag::Reg :                 XXH3_64bits_reset           ( &_state                                         ) ; break ;
			case FileTag::Lnk : _s_init_lnk() ; XXH3_64bits_reset_withSecret( &_state , _s_lnk_secret , sizeof(_s_lnk_secret) ) ; break ;
			case FileTag::Exe : _s_init_exe() ; XXH3_64bits_reset_withSecret( &_state , _s_exe_secret , sizeof(_s_exe_secret) ) ; break ;
		DF}
	}

	void _Xxh::_update ( const void* p , size_t sz )       {          XXH3_64bits_update(&_state,p,sz) ;            }
	Crc  _Xxh::digest  (                           ) const { return { XXH3_64bits_digest(&_state     ) , is_lnk } ; }

}
