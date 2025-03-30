// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <ranges>

#define XXH_INLINE_ALL
#ifdef NDEBUG
	#define XXH_DEBUGLEVEL 0
#else
	#define XXH_DEBUGLEVEL 1
#endif
#include "xxhash.h"

#include "disk.hh"
#include "serialize.hh"

// ENUM macro does not work inside namespace's

ENUM_1( CrcSpecial // use non-abbreviated names as it is used for user
,	Valid = None   // >=Valid means value represent file content, >Val means that in addition, file exists
,	Unknown        // file is completely unknown
,	Lnk            // file is a link pointing to an unknown location
,	Reg            // file is regular with unknown content
,	None           // file does not exist or is a dir
,	Empty          // file is the regular empty file
,	Plain
)

namespace Hash {

	//
	// Crc
	//

	struct Crc {
		friend ::string& operator+=( ::string& , Crc const ) ;
		using Val = uint64_t ;
		static constexpr uint8_t NChkBits = 8 ;                                       // as Crc may be used w/o protection against collision, ensure we have some margin
		//
		static constexpr Val ChkMsk = ~lsb_msk<Val>(NChkBits) ;
		//
		static const Crc Unknown ;
		static const Crc Lnk     ;
		static const Crc Reg     ;
		static const Crc None    ;
		static const Crc Empty   ;
		// statics
		static bool s_sense( Accesses a , FileTag t ) {                               // return whether accesses a can see the difference between files with tag t
			Crc crc{t} ;
			return !crc.match(crc,a) ;
		}
		// cxtors & casts
		constexpr Crc(                            ) = default ;
		constexpr Crc( Val v , Bool3 is_lnk=Maybe ) : _val{v} {
			switch (is_lnk) {
				case No    : _val &= ~Val(1) ; break ;
				case Maybe :                 ; break ;
				case Yes   : _val |=  Val(1) ; break ;
			}
		}
		constexpr Crc(FileTag tag) {
			switch (tag) {
				case FileTag::None  :
				case FileTag::Dir   : self = Crc::None  ; break ;
				case FileTag::Lnk   : self = Crc::Lnk   ; break ;
				case FileTag::Reg   :
				case FileTag::Exe   : self = Crc::Reg   ; break ;
				case FileTag::Empty : self = Crc::Empty ; break ;
			DF}
		}
		Crc( ::string const& filename                             ) ;
		Crc( ::string const& filename , Disk::FileInfo&/*out*/ fi ) {
			fi  = Disk::FileInfo(filename) ;
			self = Crc(filename)           ;
			Disk::FileSig sig_before = fi.sig()                ;
			Disk::FileSig sig_after  = Disk::FileSig(filename) ;
			if (sig_after!=sig_before) {                                              // file was moving, association date<=>crc is not reliable
				if (sig_after.tag()==sig_before.tag()) self = Crc(sig_before.tag()) ; // file type is reliable
				else                                   self = {}                    ; // nothing is reliable
			}
		}
		Crc( ::string const& filename , Disk::FileSig&/*out*/ sig ) {
			Disk::FileInfo fi ;
			self = Crc(filename,/*out*/fi) ;
			sig  = fi.sig()                ;
		}
	private :
		constexpr Crc( CrcSpecial special ) : _val{+special} {}
		//
		constexpr operator CrcSpecial() const { return _val>=+CrcSpecial::Plain ? CrcSpecial::Plain : CrcSpecial(_val) ; }
		// accesses
	public :
		explicit operator ::string() const ;
		::string hex              () const ;
		//
		constexpr bool              operator== (Crc const&) const = default ;
		constexpr ::strong_ordering operator<=>(Crc const&) const = default ;
		constexpr Val               operator+  (          ) const { return  _val                                           ; }
		constexpr bool              valid      (          ) const { return _val>=+CrcSpecial::Valid                        ; }
		constexpr bool              exists     (          ) const { return +self && self!=None                             ; }
		/**/      void              clear      (          )       { self = {}                                              ; }
		constexpr bool              is_lnk     (          ) const { return _plain() ?   _val&0x1  : self==Lnk              ; }
		constexpr bool              is_reg     (          ) const { return _plain() ? !(_val&0x1) : self==Reg||self==Empty ; }
	private :
		constexpr bool _plain() const { return _val>=N<CrcSpecial> ; }
		// services
	public :
		bool     match        ( Crc other , Accesses a=~Accesses() ) const { return !( diff_accesses(other) & a ) ; } ;
		Accesses diff_accesses( Crc other                          ) const ;
		bool     never_match  (             Accesses a=~Accesses() ) const ;
	private :
		Val _val = +CrcSpecial::Unknown ;
	} ;

	template<class T        > struct IsUnstableIterableHelper ;                                                       // unable to generate hash of unordered containers
	template<class K,class V> struct IsUnstableIterableHelper<::umap<K,V>> { static constexpr bool value = true ; } ; // cannot specialize a concept, so use a struct
	template<class K        > struct IsUnstableIterableHelper<::uset<K  >> { static constexpr bool value = true ; } ; // .
	template<class T> concept IsUnstableIterable = IsUnstableIterableHelper<T>::value ;

	template<class T> concept _SimpleUpdate = (sizeof(T)==1&&!::is_empty_v<T>) || ::is_integral_v<T> ;
	struct Xxh {
		// statics
	private :
		static void _s_init_salt() {
			if (_s_salt_inited) return ;                                       // fast path : avoid taking lock
			Lock lock{_s_salt_init_mutex} ;
			if (_s_salt_inited) return ;                                       // repeat test after lock in case of contention
			XXH3_generateSecret(_s_lnk_secret,sizeof(_s_lnk_secret),"lnk",3) ;
			XXH3_generateSecret(_s_exe_secret,sizeof(_s_exe_secret),"exe",3) ;
			_s_salt_inited = true ;
		}
		// static data
		static char                  _s_lnk_secret[XXH3_SECRET_SIZE_MIN] ;
		static char                  _s_exe_secret[XXH3_SECRET_SIZE_MIN] ;
		static Mutex<MutexLvl::Hash> _s_salt_init_mutex                  ;
		static bool                  _s_salt_inited                      ;
		//cxtors & casts
	public :
		Xxh(         ) ;
		Xxh(FileTag t) ;
		template<class T> Xxh( T const& x) : Xxh{} { self += x ; }
		// services
		Crc digest() const ;
		//
		Xxh& operator+=(::string_view) ;                                    // low level interface compatible with serialization
		//
		template<_SimpleUpdate T> Xxh& operator+=(T const& x) {
			self += ::string_view( ::launder(reinterpret_cast<const char*>(&x)) , sizeof(x) ) ;
			return self ;
		}
		/**/                                                                      Xxh& operator+=(::string const& s) { self += s.size() ; self +=::string_view(s) ; return self ; }
		template<class T> requires( !_SimpleUpdate<T> && !IsUnstableIterable<T> ) Xxh& operator+=(T        const& x) { serialize(self,x) ;                          return self ; }
		template<class T> requires(                       IsUnstableIterable<T> ) Xxh& operator+=(T        const& x) = delete ;
		// data
	public :
		Bool3 is_lnk    = Maybe ;
		bool  seen_data = false ;
	private :
		XXH3_state_t _state ;
	} ;

	//
	// implementation
	//

	constexpr Crc Crc::Unknown{CrcSpecial::Unknown} ;
	constexpr Crc Crc::Lnk    {CrcSpecial::Lnk    } ;
	constexpr Crc Crc::Reg    {CrcSpecial::Reg    } ;
	constexpr Crc Crc::None   {CrcSpecial::None   } ;
	constexpr Crc Crc::Empty  {CrcSpecial::Empty  } ;

	inline bool Crc::never_match(Accesses a) const {
		switch (self) {
			case Unknown : return +a              ;
			case Lnk     : return  a[Access::Lnk] ;
			case Reg     : return  a[Access::Reg] ;
			default      : return false           ;
		}
	}

}
// must be outside Engine namespace as it specializes std::hash
namespace std {
	template<> struct hash<Hash::Crc> { size_t operator()(Hash::Crc c) const { return +c ; } } ;
}
