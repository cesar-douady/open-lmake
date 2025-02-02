// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <grp.h>

#include "rpc_job.hh"

namespace Caches {

	struct DirCache : Cache {     // PER_CACHE : inherit from Cache and provide implementation
		static constexpr char HeadS[] = ADMIN_DIR_S ;
		// services
		virtual void config(::vmap_ss const& ) ;
		virtual Tag  tag   (                 ) { return Tag::Dir ; }
		virtual void serdes(::string     & os) { _serdes(os) ;     } // serialize
		virtual void serdes(::string_view& is) { _serdes(is) ;     } // deserialize
		//
		virtual Match                        sub_match   ( ::string const& /*job*/ , ::vmap_s<DepDigest> const&                 ) ;
		virtual ::pair<JobInfo,AcFd>         sub_download( ::string const& /*match_key*/                                        ) ;
		virtual ::pair_s/*upload_key*/<AcFd> sub_upload  ( Sz /*max_sz*/                                                        ) ;
		virtual bool/*ok*/                   sub_commit  ( ::string const& /*upload_key*/ , ::string const& /*job*/ , JobInfo&& ) ;
		virtual void                         sub_dismiss ( ::string const& /*upload_key*/                                       ) ;
		//
		void chk(ssize_t delta_sz=0) const ;
	private :
		::string _lru_file   ( ::string const& entry_s                           ) const { return dir_s+entry_s+"lru" ; }
		Sz       _lru_remove ( ::string const& entry_s         , Disk::NfsGuard& ) ;
		void     _lru_first  ( ::string const& entry_s    , Sz , Disk::NfsGuard& ) ;
		void     _mk_room    ( Sz old_sz , Sz new_sz           , Disk::NfsGuard& ) ;
		Sz       _reserved_sz( ::string const& upload_key      , Disk::NfsGuard& ) ;
		void     _dismiss    ( ::string const& upload_key , Sz , Disk::NfsGuard& ) ;
        //
		template<IsStream T> void _serdes(T& s) {
			::serdes(s,repo_s       ) ;
			::serdes(s,dir_s        ) ;
			::serdes(s,sz           ) ;
			::serdes(s,reliable_dirs) ;
		}
		// data
		::string repo_s        ;
		::string dir_s         ;
		Sz       sz            = 0     ;
		bool     reliable_dirs = false ;
	} ;

}
