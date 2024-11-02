// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <grp.h>

#include "core.hh" // must be first to include Python.h first

namespace Caches {

	struct DirCache : Cache {     // PER_CACHE : inherit from Cache and provide implementation
		using Sz = Disk::DiskSz ;
		static constexpr char HeadS[] = ADMIN_DIR_S ;
		// services
		virtual void config(Config::Cache const&) ;
		//
		virtual Match      match   ( Job , Req                                                   ) ;
		virtual JobInfo    download( Job , Id        const& , JobReason const& , Disk::NfsGuard& ) ;
		virtual bool/*ok*/ upload  ( Job , JobDigest const& ,                    Disk::NfsGuard& ) ;
		//
		void chk(ssize_t delta_sz=0) const ;
	private :
		::string _lru_file  ( ::string const& entry_s             ) const { return dir_s+entry_s+"lru" ; }
		Sz       _lru_remove( ::string const& entry_s             ) ;
		void     _lru_first ( ::string const& entry_s , Sz sz     ) ;
		void     _mk_room   ( Sz old_sz               , Sz new_sz ) ;
		// data
		::string repo_s ;
		::string dir_s  ;
		Fd       dir_fd ;
		Sz       sz     = 0 ;
	} ;

}
