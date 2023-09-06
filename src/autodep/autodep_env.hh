// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "serialize.hh"

struct AutodepEnv {
	friend ::ostream& operator<<( ::ostream& , AutodepEnv const& ) ;
	// cxtors & casts
	AutodepEnv() = default ;
	// env format : server:port:options:source_dirs:root_dir
	//if port is empty, server is considered a file to log deps to (which defaults to stderr if empty)
	AutodepEnv( ::string const& env ) ;
	operator ::string() const ;
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,service    ) ;
		::serdes(s,root_dir   ) ;
		::serdes(s,src_dirs_s ) ;
		::serdes(s,auto_mkdir ) ;
		::serdes(s,ignore_stat) ;
		::serdes(s,lnk_support) ;
	}
	// data
	::string   service     ;
	::string   root_dir    ;
	::vector_s src_dirs_s  ;
	bool       auto_mkdir  = false            ;            // if true <=> auto mkdir in case of chdir
	bool       ignore_stat = false            ;            // if true <=> stat-like syscalls do not trigger dependencies
	LnkSupport lnk_support = LnkSupport::Full ;
} ;
