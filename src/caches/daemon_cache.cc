// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#if !CACHE_LIGHT     // the major purpose of light implementation is to avoid loading python
	#include "py.hh" // /!\ must be included first as Python.h must be included first
#endif

#include "disk.hh"
#include "hash.hh"

// cache format :
#include "daemon_cache.hh"

using namespace Disk ;
using namespace Hash ;
#if !CACHE_LIGHT         // the major purpose of light implementation is to avoid loading python
	using namespace Py ;
#endif

namespace Caches {

	void DaemonCache::chk(ssize_t /*delta_sz*/) const {}

	#if CACHE_LIGHT
		void DaemonCache::config( ::vmap_ss const& /*dct*/ , bool /*may_init*/ ) { FAIL() ; }
	#else
		void DaemonCache::config( ::vmap_ss const& dct , bool may_init ) {
			Trace trace(CacheChnl,"DaemonCache::config",dct.size(),STR(may_init)) ;
			::string dir_s ;
			for( auto const& [key,val] : dct ) {
				try {
					switch (key[0]) {
						case 'd' : if (key=="dir") { dir_s   = with_slash(val) ; continue ; } break ; // dir is necessary to access cache
						case 'k' : if (key=="key") { key_crc = Crc(New   ,val) ; continue ; } break ; // key cannot be shared as it identifies repo
					DN}
				} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry "    ,key,": ",val) ; }
				trace("bad_repo_key",key) ;
				throw cat("wrong key (",key,") in lmake.config") ;
			}
			throw_unless( +dir_s , "dir must be specified for dir_cache") ;
			throw_unless( is_abs(dir_s) , "dir must be absolute for dir_cache"  ) ;
		}
	#endif

	void                                DaemonCache::repair      ( bool /*dry_run*/                                              ) { FAIL() ; }
	::pair<Cache::DownloadDigest,AcFd>  DaemonCache::sub_download( ::string const& /*job*/ , MDD const& /*repo_deps*/            ) { FAIL() ; }
	::pair<uint64_t/*upload_key*/,AcFd> DaemonCache::sub_upload  ( Sz /*reserved_sz*/                                            ) { FAIL() ; }
	void                                DaemonCache::sub_commit  ( uint64_t /*upload_key*/ , ::string const& /*job*/ , JobInfo&& ) { FAIL() ; }
	void                                DaemonCache::sub_dismiss ( uint64_t /*upload_key*/                                       ) { FAIL() ; }

}
