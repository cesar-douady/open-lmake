// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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
	,	Valid = None                   // >=Valid means value represent file content, >Val means that in addition, file exists
	,	Unknown_                       // file is completely unknown
	,	Lnk                            // file is a link pointing to an unknown location
	,	Reg                            // file is regular with unknown content
	,	None                           // file does not exist or is a dir
	,	Empty                          // file is the regular empty file
	,	Plain
	)

	struct Crc {
		friend ::ostream& operator<<( ::ostream& , Crc const ) ;
		static constexpr uint8_t NChkBits = 8 ;                                // as Crc may be used w/o protection against collision, ensure we have some margin
		//
		static constexpr uint64_t ChkMsk = ~lsb_msk<uint64_t>(NChkBits) ;

		static const Crc Unknown ;
		static const Crc Lnk     ;
		static const Crc Reg     ;
		static const Crc None    ;
		static const Crc Empty   ;
		// statics
		static bool s_sense( Accesses a , FileTag t ) { // return whether accesses a can see the difference between files with tag t
			Crc crc{t} ;
			return !crc.match(crc,a) ;
		}
		// cxtors & casts
		constexpr Crc(                                                           ) = default ;
		constexpr Crc( uint64_t v , bool is_lnk                                  ) : _val{v}                                        { if (is_lnk) _val |= 0x1 ; else _val &= ~0x1 ; }
		/**/      Crc(                         ::string const& filename , Algo a ) : Crc{ Disk::FileInfo(filename) , filename , a } {}
		/**/      Crc( Time::Ddate&/*out*/ d , ::string const& filename , Algo a ) {
			Disk::FileInfo fi{filename} ;
			d     = fi.date            ;
			*this = Crc(fi,filename,a) ;
		}
		Crc(FileTag tag) {
			switch (tag) {
				case FileTag::Reg  :
				case FileTag::Exe  : *this = Crc::Reg     ; break ;
				case FileTag::Lnk  : *this = Crc::Lnk     ; break ;
				case FileTag::None :
				case FileTag::Dir  : *this = Crc::None    ; break ;
				default            : *this = Crc::Unknown ; break ;
			}
		}
	private :
		constexpr Crc( CrcSpecial special                                      ) : _val{+special} {}
		/**/      Crc( Disk::FileInfo const& , ::string const& filename , Algo ) ;
		//
		operator CrcSpecial() const { return _val>=+CrcSpecial::Plain ? CrcSpecial::Plain : CrcSpecial(_val) ; }
	public :
		explicit operator ::string() const ;
		// accesses
		constexpr bool              operator== (Crc const& other) const = default ;
		constexpr ::strong_ordering operator<=>(Crc const& other) const = default ;
		constexpr uint64_t          operator+  (                ) const { return  _val                                             ; }
		constexpr bool              operator!  (                ) const { return !+*this                                           ; }
		constexpr bool              valid      (                ) const { return _val>=+CrcSpecial::Valid                          ; }
		constexpr bool              exists     (                ) const { return +*this && *this!=None                             ; }
		/**/      void              clear      (                )       { *this = {}                                               ; }
		constexpr bool              is_lnk     (                ) const { return _plain() ?   _val&0x1  : *this==Lnk               ; }
		constexpr bool              is_reg     (                ) const { return _plain() ? !(_val&0x1) : *this==Reg||*this==Empty ; }
	private :
		constexpr bool _plain() const { return _val>=+CrcSpecial::N ; }
		// services
	public :
		bool     match        ( Crc other , Accesses a=Accesses::All ) const { return !( diff_accesses(other) & a ) ; } ;
		Accesses diff_accesses( Crc other                            ) const ;
	private :
		uint64_t _val = +CrcSpecial::Unknown_ ;
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
		static constexpr size_t HashSz =  4 ;
		static constexpr size_t BlkSz  = 16 ;
		// cxtors & cast
	protected :
		_Md5(           ) ;
		_Md5(FileTag tag) : is_lnk{tag==FileTag::Lnk} { if (tag!=FileTag::Reg) _salt = to_string(tag) ; }
		// services
		void _update( const void* p , size_t sz ) ;
		Crc  digest (                           ) && ;                         // context is no more usable once digest has been asked
	private :
		void     _update64(const uint32_t _data[BlkSz]) ;
		void     _update64(                           ) { _update64(_blk) ;                         }
		uint8_t* _bblk    (                           ) { return reinterpret_cast<uint8_t*>(_blk) ; }
		// data
	public :
		bool is_lnk = false ;
	private :
		alignas(uint64_t) uint32_t _hash[HashSz] ;         // alignment to allow direct appending of _cnt<<3 (message length in bits)
		uint32_t                   _blk [BlkSz ] ;
		uint64_t                   _cnt          ;
		::string                   _salt         ;
		bool                       _closed       = false ;
	} ;

	struct _Xxh {
	private :
		static void _s_init_lnk() { ::unique_lock lock{_s_inited_mutex} ; if (_s_lnk_inited) return ; XXH3_generateSecret(_s_lnk_secret,sizeof(_s_lnk_secret),"lnk",3) ; _s_lnk_inited = true ; }
		static void _s_init_exe() { ::unique_lock lock{_s_inited_mutex} ; if (_s_exe_inited) return ; XXH3_generateSecret(_s_exe_secret,sizeof(_s_exe_secret),"exe",3) ; _s_exe_inited = true ; }
		// static data
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
		Crc  digest (                           ) && ;                         // context is no more usable once digest has been asked
		// data
	public :
		bool is_lnk = false ;
	private :
		XXH3_state_t _state ;
	} ;

	template<class T> concept _AutoCooked = sizeof(T)==1 || ::is_integral_v<T> ;
	template<class H> struct _Cooked : H {
		//cxtors & casts
		_Cooked(         ) = default ;
		_Cooked(FileTag t) : H{t} {}
		template<class... As> _Cooked( As&&... args) { update(::forward<As>(args)...) ; }
		// services
		using H::digest ;
		//
		template<_AutoCooked T> _Cooked& update( T const* p , size_t sz ) {
			H::_update( p , sizeof(*p)*sz ) ;
			return *this ;
		}
		template<_AutoCooked T> _Cooked& update(T const& x) {
			::array<char,sizeof(x)> buf = ::bit_cast<array<char,sizeof(x)>>(x) ;
			H::_update( &buf , sizeof(x) ) ;
			return *this ;
		}
		/**/                                                                    _Cooked& update(::string const& s ) { update(s.size()) ; update(s.data(),s.size()) ; return *this ; }
		template<class T> requires( !_AutoCooked<T> && !IsUnstableIterable<T> ) _Cooked& update( T       const& x ) { update(serialize(x)) ;                         return *this ; }
		template<class T> requires(                     IsUnstableIterable<T> ) _Cooked& update( T       const& x ) = delete ;
	} ;

	//
	// implementation
	//

	constexpr Crc Crc::Unknown{CrcSpecial::Unknown_} ;
	constexpr Crc Crc::Lnk    {CrcSpecial::Lnk     } ;
	constexpr Crc Crc::Reg    {CrcSpecial::Reg     } ;
	constexpr Crc Crc::None   {CrcSpecial::None    } ;
	constexpr Crc Crc::Empty  {CrcSpecial::Empty   } ;

}
// must be outside Engine namespace as it specializes std::hash
namespace std {
	template<> struct hash<Hash::Crc> { size_t operator()(Hash::Crc c) const { return +c ; } } ;
}
