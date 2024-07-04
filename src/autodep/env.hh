// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "serialize.hh"

struct AutodepEnv : Disk::RealPathEnv {
	friend ::ostream& operator<<( ::ostream& , AutodepEnv const& ) ;
	// cxtors & casts
	AutodepEnv() = default ;
	// env format : server:port:options:source_dirs:tmp_dir_s:root_dir_s
	// if port is empty, server is considered a file to log deps to (which defaults to stderr if empty)
	// if tmp_dir_s is empty, there is no tmp dir
	AutodepEnv(::string const& env) ;
	AutodepEnv(NewType            ) : AutodepEnv{get_env("LMAKE_AUTODEP_ENV")} {}
	operator ::string() const ;
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,static_cast<RealPathEnv&>(*this)) ;
		::serdes(s,auto_mkdir                      ) ;
		::serdes(s,disabled                        ) ;
		::serdes(s,ignore_stat                     ) ;
		::serdes(s,service                         ) ;
		::serdes(s,views                           ) ;
	}
	// data
	bool                 auto_mkdir  = false ; // if true <=> auto mkdir in case of chdir
	bool                 disabled    = false ; // if true <=> no automatic report
	bool                 ignore_stat = false ; // if true <=> stat-like syscalls do not trigger dependencies
	::string             service     ;
	::vmap_s<::vector_s> views       ;
} ;
