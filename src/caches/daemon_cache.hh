// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_job.hh"

namespace Caches {

	struct DaemonCache : Cache {                                                                                               // PER_CACHE : inherit from Cache and provide implementation
		// services
		void      config( ::vmap_ss const& , bool may_init=false ) override ;
		::vmap_ss descr (                                        ) override { return { {"key_checksum",key_crc.hex()} } ; }
		void      repair( bool dry_run                           ) override ;
		Tag       tag   (                                        ) override { return Tag::Daemon                        ; }
		void      serdes( ::string     & os                      ) override { _serdes(os) ;                               } // serialize  , cannot be a template as it is a virtual method
		void      serdes( ::string_view& is                      ) override { _serdes(is) ;                               } // deserialize, .
		//
		::pair<DownloadDigest,AcFd>         sub_download( ::string const& job , MDD const&                          ) override ;
		::pair<uint64_t/*upload_key*/,AcFd> sub_upload  ( Sz max_sz                                                 ) override ;
		void                                sub_commit  ( uint64_t upload_key , ::string const& /*job*/ , JobInfo&& ) override ;
		void                                sub_dismiss ( uint64_t upload_key                                       ) override ;
		//
		void chk(ssize_t delta_sz=0) const ;
	private :
		template<IsStream S> void _serdes(S& s) {
			::serdes(s,key_crc) ;
		}
		// data
	public :
		Hash::Crc     key_crc = Hash::Crc::None ;
		KeyedService  service ;
	} ;

}
