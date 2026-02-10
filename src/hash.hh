// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#define XXH_INLINE_ALL
#ifdef NDEBUG
	#define XXH_DEBUGLEVEL 0
#else
	#define XXH_DEBUGLEVEL 1
#endif
#include "xxhash.h"

#include "disk.hh"
#include "serialize.hh"

enum class CrcSpecial : uint8_t { // use non-abbreviated names as it is used for user
	Unknown                       // file is completely unknown
,	Lnk                           // file is a link pointing to an unknown location
,	Reg                           // file is regular with unknown content
,	None                          // file does not exist or is a dir
,	Empty                         // file is the regular empty file
,	Plain
//
// alises
,	Valid = None                  // >=Valid means value represent file content, >Val means that in addition, file exists
} ;

namespace Hash {

	template<class T        > struct IsUnstableIterableHelper ;                                                       // unable to generate hash of unordered containers
	template<class K,class V> struct IsUnstableIterableHelper<::umap<K,V>> { static constexpr bool value = true ; } ; // cannot specialize a concept, so use a struct
	template<class K        > struct IsUnstableIterableHelper<::uset<K  >> { static constexpr bool value = true ; } ; // .
	template<class T> concept IsUnstableIterable = IsUnstableIterableHelper<T>::value ;

	template<uint8_t Sz> struct _Crc ;

	//
	// Xxh
	//

	template<class T> concept _SimpleUpdate = sizeof(T)==alignof(T) && !::is_empty_v<T> && ::is_trivially_copyable_v<T> ; // if sizeof==alignof, there is a single field and hence no padding
	template<uint8_t Sz> struct _Xxh {
		// statics
	private :
		static void _s_init_salt() {
			if (_s_salt_inited) return ;                                                                                  // fast path : avoid taking lock
			Lock lock{_s_salt_init_mutex} ;
			if (_s_salt_inited) return ;                                                                                  // repeat test after lock in case of contention
			XXH3_generateSecret(_s_lnk_secret,sizeof(_s_lnk_secret),"lnk",3) ;
			XXH3_generateSecret(_s_exe_secret,sizeof(_s_exe_secret),"exe",3) ;
			_s_salt_inited = true ;
		}
		// static data
		static char    _s_lnk_secret[XXH3_SECRET_SIZE_MIN] ;
		static char    _s_exe_secret[XXH3_SECRET_SIZE_MIN] ;
		static Mutex<> _s_salt_init_mutex                  ;
		static bool    _s_salt_inited                      ;
		//cxtors & casts
	public :
		_Xxh(         ) ;
		_Xxh(FileTag t) ;
		template<class T> _Xxh( NewType , T const& x ) : _Xxh{} { self += x ; }
		// services
		_Crc<Sz> digest() const ;
		//
		_Xxh& operator+=(::string_view) ;                                                                                 // low level interface compatible with serialization
		//
		template<_SimpleUpdate T> _Xxh& operator+=(T const& x) {
			self += ::string_view( ::launder(reinterpret_cast<const char*>(&x)) , sizeof(x) ) ;
			return self ;
		}
		/**/                                                                      _Xxh& operator+=(::string const& s) { self += s.size() ; self += ::string_view(s) ; return self ; }
		template<class T> requires( !_SimpleUpdate<T> && !IsUnstableIterable<T> ) _Xxh& operator+=(T        const& x) { serialize(self,x) ;                           return self ; }
		template<class T> requires(                       IsUnstableIterable<T> ) _Xxh& operator+=(T        const& x) = delete ;
		// data
	public :
		Bool3 is_lnk    = Maybe ;
		bool  seen_data = false ;
	private :
		XXH3_state_t _state ;
	} ; //!            Sz
	using Xxh   = _Xxh<64> ;
	using Xxh96 = _Xxh<96> ;

	template<class T> concept IsHash = ::is_base_of_v<Xxh,T> ;

	//
	// Crc
	//

	template<uint8_t Sz> struct _Crc ;
	template<uint8_t Sz> ::string& operator+=( ::string& , _Crc<Sz> const ) ;
	template<uint8_t Sz> struct _Crc {
		static_assert( Sz>0 ) ;                                                                   // meaningless and painful to code
		friend ::string& operator+=<Sz>( ::string& , _Crc<Sz> const ) ;
		#if HAS_UINT128
			using Val = ::conditional_t<(Sz>64),uint128_t,uint64_t> ;
		#else
			using Val = ::conditional_t<(Sz>64),uint64_t,uint64_t> ;                              // XXX/ : work around a bug in g++13 with -Wall : force defer the evaluation of N<CrcSpecial> below
		//	using Val = uint64_t ;                                                                // revert to 64 bits if no 128 bits support (normal code)
		#endif
		static constexpr Val     Msk      = (Val(1)<<(::min(Sz,uint8_t(sizeof(Val)*8))-1)<<1)-1 ; // if Sz is too large, we cant simply take 1<<Sz
		static constexpr uint8_t HexSz    = div_up<8>(Sz)*2                                     ; // hex are processed byte after byte
		static constexpr uint8_t Base64Sz = div_up<6>(Sz)                                       ;
		static constexpr uint8_t NChkBits = 8                                                   ; // as Crc may be used w/o protection against collision, ensure we have some margin
		//
		static constexpr Val ChkMsk = Msk>>NChkBits ;                                             // lsb's are used for various manipulations
		//
		static const _Crc Unknown ;
		static const _Crc Lnk     ;
		static const _Crc Reg     ;
		static const _Crc None    ;
		static const _Crc Empty   ;
		// statics
		static _Crc s_from_hex   (::string_view sv) ;                                             // inverse of hex   ()
		static _Crc s_from_base64(::string_view sv) ;                                             // inverse of base64()
		static bool s_sense( Accesses a , FileTag t ) {                                           // return whether accesses a can see the difference between files with tag t
			_Crc crc{t} ;
			return !crc.match(crc,a) ;
		}
		// cxtors & casts
		constexpr _Crc() = default ;
		//
		constexpr _Crc( Val v , Bool3 is_lnk=Maybe ) : _val{v} {
			_set_is_lnk(is_lnk) ;
		}
		constexpr _Crc(FileTag tag) {
			switch (tag) {
				case FileTag::None  :
				case FileTag::Dir   : self = _Crc::None  ; break ;
				case FileTag::Lnk   : self = _Crc::Lnk   ; break ;
				case FileTag::Reg   :
				case FileTag::Exe   : self = _Crc::Reg   ; break ;
				case FileTag::Empty : self = _Crc::Empty ; break ;
			DF}                                                                                   // NO_COV
		}
		explicit _Crc( ::string const& filename                             ) ;
		explicit _Crc( ::string const& filename , Disk::FileInfo&/*out*/ fi ) {
			for(;;) {                                                                                             // restart if file was moving
				fi   = Disk::FileInfo(filename) ; if (fi.tag()==FileTag::Empty) { self = _Crc::Empty ; return ; } // fast path : minimize stat syscall's
				self = _Crc(filename)           ;
				if (fi.sig()==Disk::FileSig(filename)) return ;                                                   // file was stable, we can return result
			}
		}
		explicit _Crc( ::string const& filename , Disk::FileSig&/*out*/ sig ) {
			Disk::FileInfo fi ;
			self = _Crc(filename,/*out*/fi) ;
			sig  = fi.sig()                 ;
		}
		template<class T> _Crc( NewType , T const& x , Bool3 is_lnk=Maybe ) ;
	private :
		constexpr _Crc(CrcSpecial special) : _val{+special} {}
		//
		constexpr explicit operator CrcSpecial() const { return _val>=+CrcSpecial::Plain ? CrcSpecial::Plain : CrcSpecial(_val) ; }
		// accesses
	public :
		explicit operator ::string() const ;
		::string hex              () const ;
		::string base64           () const ;
		//
		constexpr bool              operator== (_Crc const&) const = default ;
		constexpr ::strong_ordering operator<=>(_Crc const&) const = default ;
		constexpr Val               operator+  (           ) const { return  _val                                           ; }
		constexpr bool              valid      (           ) const { return _val>=+CrcSpecial::Valid                        ; }
		constexpr bool              exists     (           ) const { return +self && self!=None                             ; }
		/**/      void              clear      (           )       { self = {}                                              ; }
		constexpr bool              is_lnk     (           ) const { return _plain() ?   _val&0x1  : self==Lnk              ; }
		constexpr bool              is_reg     (           ) const { return _plain() ? !(_val&0x1) : self==Reg||self==Empty ; }
	private :
		constexpr bool _plain() const { return _val>=N<CrcSpecial> ; }
		//
		constexpr void _set_is_lnk(Bool3 is_lnk) {
			switch (is_lnk) {
				case No    : _val &= ~Val(1) ; break ;
				case Maybe :                   break ;
				case Yes   : _val |=  Val(1) ; break ;
			}
		}
		// services
	public :
		bool     match        ( _Crc crc , Accesses a=FullAccesses ) const { return !( diff_accesses(crc) & a ) ; }
		Accesses diff_accesses( _Crc                               ) const ;
		bool     never_match  (            Accesses a=FullAccesses ) const ;
		size_t   hash         (                                    ) const { return _val                        ; }
		// data
	private :
		Val _val = +CrcSpecial::Unknown ;
	} ;
	using Crc   = _Crc<64> ;
	using Crc96 = _Crc<96> ;                                                                                      // revert to 64 bits is 128 bits is not supported

	// easy, fast and good enough in some situations
	// cf https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	struct Fnv {
		static constexpr size_t Offset = sizeof(size_t)==8 ? 0xcbf29ce484222325 : 0x811c9dc5 ;
		static constexpr size_t Prime  = sizeof(size_t)==8 ? 0x00000100000001b3 : 0x01000193 ;
		size_t operator+ (        ) const {                         return val  ; }
		Fnv&   operator+=(size_t x)       { val = (val^x) * Prime ; return self ; }
		size_t val = Offset ;
	} ;

	//
	// implementation
	//

	template<uint8_t Sz> constexpr _Crc<Sz> _Crc<Sz>::Unknown{CrcSpecial::Unknown} ;
	template<uint8_t Sz> constexpr _Crc<Sz> _Crc<Sz>::Lnk    {CrcSpecial::Lnk    } ;
	template<uint8_t Sz> constexpr _Crc<Sz> _Crc<Sz>::Reg    {CrcSpecial::Reg    } ;
	template<uint8_t Sz> constexpr _Crc<Sz> _Crc<Sz>::None   {CrcSpecial::None   } ;
	template<uint8_t Sz> constexpr _Crc<Sz> _Crc<Sz>::Empty  {CrcSpecial::Empty  } ;

	template<uint8_t Sz> template<class T> _Crc<Sz>::_Crc( NewType , T const& x , Bool3 is_lnk ) : _Crc{_Xxh<Sz>(New,x).digest()} {
		_set_is_lnk(is_lnk) ;
	}

	template<uint8_t Sz> bool _Crc<Sz>::never_match(Accesses a) const {
		switch (CrcSpecial(self)) {
			case CrcSpecial::Unknown : return +a              ;
			case CrcSpecial::Lnk     : return  a[Access::Lnk] ;
			case CrcSpecial::Reg     : return  a[Access::Reg] ;
			default                  : return false           ;
		}
	}

}
