// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "serialize.hh"

struct AutodepEnv : Disk::RealPathEnv {
	friend ::string& operator+=( ::string& , AutodepEnv const& ) ;
	// cxtors & casts
	AutodepEnv() = default ;
	// env format : server:port:options:fast_host:fast_report_pipe:tmp_dir_s:repo_root_s:sub_repo_s:src_dirs_s:views
	// if port is empty, server is considered a file to log deps to (which defaults to stderr if empty)
	// if tmp_dir_s is empty, there is no tmp dir
	AutodepEnv(::string const& env) ;
	AutodepEnv(NewType            ) : AutodepEnv{get_env("LMAKE_AUTODEP_ENV")} {}
	operator ::string() const ;
	// accesses
	bool operator+ () const { return +service                        ; }
	bool has_server() const { return +service && service.back()!=':' ; }
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,static_cast<RealPathEnv&>(self)) ;
		::serdes(s,auto_mkdir                     ) ;
		::serdes(s,enable                         ) ;
		::serdes(s,ignore_stat                    ) ;
		::serdes(s,fast_report_pipe               ) ;
		::serdes(s,service                        ) ;
		::serdes(s,sub_repo_s                     ) ;
		::serdes(s,views                          ) ;
	}
	Fd fast_report_fd() const ;
	Fd report_fd     () const ;
	Fd repo_root_fd  () const ;
	// data
	bool                 auto_mkdir       = false ; // if true  <=> auto mkdir in case of chdir
	bool                 enable           = true  ; // if false <=> no automatic report
	bool                 ignore_stat      = false ; // if true  <=> stat-like syscalls do not trigger dependencies
	::string             fast_host        ;         // host on which fast_report_pipe can be used
	::string             fast_report_pipe ;         // pipe to report accesses, faster than sockets, but does not allow replies
	::string             service          ;
	::string             sub_repo_s       ;         // relative to repo_root_s
	::vmap_s<::vector_s> views            ;
} ;
