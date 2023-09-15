// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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
#include "xxhash.patched.h"

#include "disk.hh"
#include "serialize.hh"

namespace Hash {

	using Access   = Disk::Access   ;
	using Accesses = Disk::Accesses ;
	using FileTag  = Disk::FileTag  ;

	struct _Md5 ;
	struct _Xxh ;

	template<class H> struct _Cooked ;

	using Md5 = _Cooked<_Md5> ;
	using Xxh = _Cooked<_Xxh> ;

	ENUM(Algo
	,	Md5
	,	Xxh
	)

	//
	// Crc
	//

	ENUM_1( CrcSpecial
	,	Valid = None                                                           // >=Valid means value represent file content
	,	Unknown_
	,	None
	)

	struct Crc {
		friend ::ostream& operator<<( ::ostream& , Crc const ) ;
		static constexpr uint8_t NChkBits = 8 ;                                // as Crc may be used w/o protection against collision, ensure we have some margin
		//
		static constexpr uint64_t ChkMsk = ~lsb_msk<uint64_t>(NChkBits) ;

		static const Crc Unknown ;                                             // crc has not been computed
		static const Crc None    ;                                             // file does not exist
		// cxtors & casts
		constexpr          Crc(                                  ) = default ;
		constexpr explicit Crc( uint64_t v                       ) : _val{v} {}
		/**/               Crc( ::string const& file_name , Algo ) ;
		// accesses
	public :
		constexpr bool              operator== (Crc const& other) const { return +*this== +other            ; }
		constexpr ::strong_ordering operator<=>(Crc const& other) const { return +*this<=>+other            ; }
		constexpr uint64_t          operator+  (                ) const { return  _val                      ; }
		constexpr bool              operator!  (                ) const { return !+*this                    ; }
		constexpr bool              valid      (                ) const { return +*this>=+CrcSpecial::Valid ; }
		/**/      void              clear      (                )       { *this = Crc()                     ; }
		constexpr bool              is_lnk     (                ) const { return _val & uint64_t(1)         ; }
		// services
		bool match( Crc other , Accesses a=Accesses::All ) const {
			if ( !a                         ) return true  ;                   // dont even care about validity if there was no access at all
			if ( !valid() || !other.valid() ) return false ;                   // Unknown & Err never match as they cannot be guaranteed
			uint64_t t = +*this ;
			uint64_t o = +other ;
			if (!a[Access::Stat]) {
				if      (!a[Access::Lnk]) { if (*this==None) t = ~uint64_t(0) ; if (other==None) o = ~uint64_t(0) ; } // if no stat & no lnk accesses, then no file is like a lnk
				else if (!a[Access::Reg]) { if (*this==None) t = ~uint64_t(1) ; if (other==None) o = ~uint64_t(1) ; } // if no stat & no reg accesses, then no file is like a reg
			}
			// XXX : suppress this test on Stat when there is a working paradigm with pyc files (python stat the py and accesses the pyc, but actually needs the py semantic)
			if (!a[Access::Stat]) {
				if (!a[Access::Lnk]) { if (  is_lnk() && *this!=None ) t |= ~uint64_t(1) ; if (  other.is_lnk() && other!=None ) o |= ~uint64_t(1) ; } // if no lnk access, ignore lnk value
				if (!a[Access::Reg]) { if ( !is_lnk() && *this!=None ) t |= ~uint64_t(1) ; if ( !other.is_lnk() && other!=None ) o |= ~uint64_t(1) ; } // if no reg access, ignore reg value
			}
			uint64_t diff = t ^ o ;
			if (!diff          ) return true  ;                                           // crc are identical
			if ( diff & ChkMsk ) return false ;                                           // crc are different
			fail_prod("near crc match, must increase CRC size ",*this," versus ",other) ;
		} ;
		explicit operator ::string() const {
			::string res ; res.reserve(sizeof(_val)*2) ;
			for( size_t i=0 ; i<sizeof(_val) ; i++ ) {
				uint8_t b = _val>>(i*8) ;
				for( uint8_t d : {b>>4,b&0xf} ) res.push_back( d<10 ? '0'+d : 'a'+d-10 ) ;
			}
			return res ;
		}
	private :
		uint64_t _val = 0 ;
	} ;

	//
	// Md5
	//

	// class to compute Crc's (md5)
	// Construct without arg.
	// Call update with :
	// - An arg
	//   - STL objects are supported but not distinguished from one another.
	//     For example, a vector and an array with same content will provide the same crc.
	//     unordered_map and unordered_set are not supported as there is no way to guarantee order
	//   - if arg is a STL containers, components are split and pushed.
	// - A pointer and a size (number of elements). Elements are pushed without being split.
	//
	// call hash object to retrieve Crc
	template<class T        > struct IsUnstableIterableHelper ;                                                       // unable to generate hash of unordered containers
	template<class K,class V> struct IsUnstableIterableHelper<::umap<K,V>> { static constexpr bool value = true ; } ; // cannot specialize a concept, so use a struct
	template<class K        > struct IsUnstableIterableHelper<::uset<K  >> { static constexpr bool value = true ; } ; // .
	template<class T> concept IsUnstableIterable = IsUnstableIterableHelper<T>::value ;

	struct _Md5 {
	protected :
		static constexpr size_t HashSz =  4 ;
		static constexpr size_t BlkSz  = 16 ;
		// cxtors & cast
		_Md5(           ) ;
		_Md5(FileTag tag) { if (tag!=FileTag::Reg) _salt = to_string(tag) ; }
		// services
		Crc  digest (                           ) ;
		void _update( const void* p , size_t sz ) ;
	private :
		void     _update64(const uint32_t _data[BlkSz]) ;
		void     _update64(                           ) { _update64(_blk) ;                         }
		uint8_t* _bblk    (                           ) { return reinterpret_cast<uint8_t*>(_blk) ; }
		// data
		alignas(uint64_t) uint32_t _hash[HashSz] ;                           // alignment to allow direct appending of _cnt<<3 (message length in bits)
		uint32_t                   _blk [BlkSz ] ;
		uint64_t                   _cnt          ;
		::string                   _salt         ;
		bool                       _closed       = false ;
	} ;

	struct _Xxh {
		static void _s_init_lnk() { ::unique_lock lock{_s_inited_mutex} ; if (_s_lnk_inited) return ; XXH3_generateSecret(_s_lnk_secret,sizeof(_s_lnk_secret),"lnk",3) ; _s_lnk_inited = true ; }
		static void _s_init_exe() { ::unique_lock lock{_s_inited_mutex} ; if (_s_exe_inited) return ; XXH3_generateSecret(_s_exe_secret,sizeof(_s_exe_secret),"exe",3) ; _s_exe_inited = true ; }
		// static data
	private :
		static char    _s_lnk_secret[XXH3_SECRET_SIZE_MIN] ;
		static char    _s_exe_secret[XXH3_SECRET_SIZE_MIN] ;
		static ::mutex _s_inited_mutex                     ;
		static bool    _s_lnk_inited                       ;
		static bool    _s_exe_inited                       ;
	protected :
		// cxtors & cast
		_Xxh(       ) ;
		_Xxh(FileTag) ;
		// services
		void _update( const void* p , size_t sz ) ;
		Crc  digest (                           ) ;
		// data
	private :
		XXH3_state_t _state ;
	} ;

	template<class H> struct _Cooked : H {
		//cxtors & casts
		_Cooked(         ) : H{ } {}
		_Cooked(FileTag t) : H{t} {}
		// services
		using H::digest ;
		template<class T> requires(::is_trivially_copyable_v<T>) void update( T const* p , size_t sz ) { H::_update( p , sizeof(*p)*sz ) ; }
		template<class T> requires(::is_trivially_copyable_v<T>) void update( T const& x             ) {
			::array<char,sizeof(x)> buf = ::bit_cast<array<char,sizeof(x)>>(x) ;
			H::_update( &buf , sizeof(x) ) ;
		}
		//
		/**/                                                                                  void update(::string const& s ) { update(s.size()) ; update(s.data(),s.size()) ; }
		template<class T> requires( !::is_trivially_copyable_v<T> && !IsUnstableIterable<T> ) void update( T       const& x ) { update(serialize(x)) ;                         }
		template<class T> requires(                                   IsUnstableIterable<T> ) void update( T       const& x ) = delete ;
	} ;

	//
	// implementation
	//

	constexpr Crc Crc::Unknown{+CrcSpecial::Unknown_} ;                        // crc has not been computed
	constexpr Crc Crc::None   {+CrcSpecial::None    } ;                        // file does not exist

}
// must be outside Engine namespace as it specializes std::hash
namespace std {
	template<> struct hash<Hash::Crc> { size_t operator()(Hash::Crc c) const { return +c; } } ;
}
