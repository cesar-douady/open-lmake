// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <grp.h>

#include "rpc_job.hh"
#include "time.hh"

enum class CacheRepairTag : uint8_t {
	SzData
,	Info
,	Lru
} ;
using CacheRepairTags = BitMap<CacheRepairTag> ;

namespace Caches {

	using RepairTag  = CacheRepairTag  ;
	using RepairTags = CacheRepairTags ;

	struct DirCache : Cache {                                                                                                // PER_CACHE : inherit from Cache and provide implementation
		// START_OF_VERSIONING
		struct Lru {
			// accesses
			bool operator==(Lru const&) const = default ;
			// data
			::string     newer_s     = DirCache::HeadS ;                                                                     // newer        , or oldest       for head
			::string     older_s     = DirCache::HeadS ;                                                                     // older        , or newest       for head
			DirCache::Sz sz          = 0               ;                                                                     // size of entry, or overall size for head
			Time::Pdate  last_access = {}              ;
		} ;
		// END_OF_VERSIONING
		struct RepairEntry {
			// accesses
			bool operator+() const { return sz ; }
			// data
			RepairTags tags    ;
			Sz         sz      = 0 ;
			Lru        old_lru ;
		} ;
		static constexpr char HeadS[] = ADMIN_DIR_S ;
		// services
		void      config( ::vmap_ss const&  , bool may_init=false ) override ;
		::vmap_ss descr (                                         ) override { return { {"key_checksum",key_crc.hex()} } ; }
		void      repair( bool dry_run                            ) override ;
		Tag       tag   (                                         ) override { return Tag::Dir                           ; }
		void      serdes( ::string     & os                       ) override { _serdes(os) ;                               } // serialize  , cannot be a template as it is a virtual method
		void      serdes( ::string_view& is                       ) override { _serdes(is) ;                               } // deserialize, .
		//
		::pair<DownloadDigest,AcFd>         sub_download( ::string const& job , MDD const&                          ) override ;
		::pair<uint64_t/*upload_key*/,AcFd> sub_upload  ( Sz max_sz                                                 ) override ;
		void                                sub_commit  ( uint64_t upload_key , ::string const& /*job*/ , JobInfo&& ) override ;
		void                                sub_dismiss ( uint64_t upload_key                                       ) override ;
		//
		void chk(ssize_t delta_sz=0) const ;
	private :
		void                            _qualify_entry( RepairEntry&/*inout*/ , ::string const& entry_s                      ) const ;
		::string                        _lru_file     (                         ::string const& entry_s                      ) const { return cat(entry_s,"lru"                               ) ; }
		::string                        _reserved_file( uint64_t upload_key                                                  ) const { return cat(reserved_dir_s,to_hex(upload_key),".sz_data") ; }
		Sz                              _reserved_sz  ( uint64_t upload_key                                  , NfsGuardLock& ) const ;
		Sz                              _lru_remove   (                         ::string const& entry_s      , NfsGuardLock& )       ;
		void                            _lru_mk_newest(                         ::string const& entry_s , Sz , NfsGuardLock& )       ;
		void                            _mk_room      ( Sz old_sz , Sz new_sz                                , NfsGuardLock& )       ;
		void                            _dismiss      ( uint64_t upload_key                             , Sz , NfsGuardLock& )       ;
		::pair_s/*key*/<DownloadDigest> _sub_match    ( ::string const& job , MDD const& , bool for_commit   , NfsGuardLock& ) const ;
		//
		template<IsStream S> void _serdes(S& s) {
			::serdes(s,key_crc  ) ;
			::serdes(s,dir_s    ) ;
			::serdes(s,max_sz   ) ;
			::serdes(s,file_sync) ;
			::serdes(s,perm_ext ) ;
			if (IsIStream<S>) _compile() ;
		}
		void _compile() {
			root_fd        = AcFd( dir_s , {.flags=O_RDONLY|O_DIRECTORY} ) ;
			reserved_dir_s = ADMIN_DIR_S "reserved/"                       ;
			lock_file      = ADMIN_DIR_S "lock"                            ;
		}
		// data
	public :
		Hash::Crc key_crc   = Hash::Crc::None ;
		::string  dir_s     ;
		Sz        max_sz    = 0               ;
		FileSync  file_sync = FileSync::Dflt  ;
		PermExt   perm_ext  = {}              ;
		// derived
		AcFd      root_fd        ;
		::string  reserved_dir_s ;
		::string  lock_file      ;
	} ;

}
