// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <grp.h>

#include "core.hh"

namespace Caches {

	struct DirCache : Cache {                                                  // PER_CACHE : inherit from Cache and provide implementation
		using Sz = Disk::DiskSz ;
		// services
		virtual void config(Config::Cache const&) ;
		//
		virtual Match      match   ( Job , Req              ) ;
		virtual JobDigest  download( Job , Id        const& ) ;
		virtual bool/*ok*/ upload  ( Job , JobDigest const& ) ;
	private :
		Sz   _lru_remove( ::string const& entry             ) ;
		void _lru_first ( ::string const& entry , Sz sz     ) ;
		void _mk_room   ( Sz old_sz             , Sz new_sz ) ;
		// data
		::string repo   ;
		::string dir    ;
		Fd       dir_fd ;
		Sz       sz     = 0 ;
	} ;

}
