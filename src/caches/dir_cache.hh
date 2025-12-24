// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <grp.h>

#include "rpc_job.hh"
#include "time.hh"

enum class DirCacheRepairTag : uint8_t {
	SzData
,	Info
,	Lru
} ;
using DirCacheRepairTags = BitMap<DirCacheRepairTag> ;

namespace Caches {

	struct DirCache : Cache {                                                                           // PER_CACHE : inherit from Cache and provide implementation
		using RepairTag  = DirCacheRepairTag  ;
		using RepairTags = DirCacheRepairTags ;
		static constexpr char HeadS[] = ADMIN_DIR_S ;
		// START_OF_VERSIONING DIR_CACHE
		struct Lru {
			// accesses
			bool operator==(Lru const&) const = default ;
			// data
			::string    newer_s     = HeadS ;                                                           // newer        , or oldest       for head
			::string    older_s     = HeadS ;                                                           // older        , or newest       for head
			Sz          sz          = 0     ;                                                           // size of entry, or overall size for head
			Time::Pdate last_access = {}    ;
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
		// services
		void      config( ::vmap_ss const& , bool may_init=false )       override ;
		::vmap_ss descr (                                        ) const override ;
		void      repair( bool dry_run                           )       override ;
		Tag       tag   (                                        )       override { return Tag::Dir ; }
		void      serdes( ::string     & os                      )       override { _serdes(os) ;     } // serialize  , cannot be a template as it is a virtual method
		void      serdes( ::string_view& is                      )       override { _serdes(is) ;     } // deserialize, .
		//
		::pair<DownloadDigest,AcFd> sub_download( ::string const& job , MDD const&                          ) override ;
		SubUploadDigest             sub_upload  ( Time::Delay exe_time , Sz max_sz                          ) override ;
		void                        sub_commit  ( uint64_t upload_key , ::string const& /*job*/ , JobInfo&& ) override ;
		void                        sub_dismiss ( uint64_t upload_key                                       ) override ;
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
		// START_OF_VERSIONING REPO
		template<IsStream S> void _serdes(S& s) {
			::serdes(s,dir_s    ) ;
			::serdes(s,file_sync) ;
			::serdes(s,repo_key ) ;
			::serdes(s,max_sz   ) ;
			::serdes(s,perm_ext ) ;
			if (IsIStream<S>) _compile() ;
		}
		void _compile() {
			root_fd        = AcFd( dir_s , {.flags=O_RDONLY|O_DIRECTORY} ) ;
			reserved_dir_s = ADMIN_DIR_S "reserved/"                       ;
			lock_file      = ADMIN_DIR_S "lock"                            ;
		}
		// END_OF_VERSIONING
		// data
	public :
		::string  dir_s     ;
		FileSync  file_sync = FileSync::Dflt  ;
		Hash::Crc repo_key  = Hash::Crc::None ;
		Sz        max_sz    = 0               ;
		PermExt   perm_ext  = {}              ;
		// derived
		AcFd      root_fd        ;
		::string  reserved_dir_s ;
		::string  lock_file      ;
	} ;

}
