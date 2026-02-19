// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "fd.hh"
#include "real_path.hh"
#include "serialize.hh"
#include "time.hh"

namespace Codec {
	struct CodecRemoteSide {
		friend ::string& operator+=( ::string& , CodecRemoteSide const& ) ;
		// cxtors & casts
		CodecRemoteSide() = default ;
		CodecRemoteSide(           ::string const& descr ) ; // used when read from $LMAKE_AUTODEP_ENV
		CodecRemoteSide( NewType , ::string const& dir_s ) ; // used when directly using and external dir
		operator ::string() const ;
		// accesses
		bool operator==(CodecRemoteSide const&) const = default ;
		bool is_dir() const { return Disk::is_dir_name(tab) ; }
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,tab      ) ;
			::serdes(s,file_sync) ;
			::serdes(s,umask    ) ;
		}
		// data
		::string tab       ;                         // source file if is_lcl(tab), else external dir
		FileSync file_sync = {} ;                    // valid if external dir
		mode_t   umask     = -1 ;                    // .
	} ;
}

struct AutodepEnv : RealPathEnv {
	friend ::string& operator+=( ::string& , AutodepEnv const& ) ;
	// cxtors & casts
	AutodepEnv() = default ;
	// env format : server:port:fast_mail:fast_report_pipe:options:fqdn:tmp_dir_s:repo_root_s:sub_repo_s:src_dirs_s:codecs:views_s
	// if tmp_dir_s is empty, there is no tmp dir
	AutodepEnv(::string const& env) ;
	AutodepEnv(NewType            ) : AutodepEnv{get_env("LMAKE_AUTODEP_ENV")} {}
	operator ::string() const ;
	// accesses
	bool operator+() const { return +service ; }
	// services
	template<IsStream S> void serdes(S& s) {
		/**/                        ::serdes(s,static_cast<RealPathEnv&>(self)) ;
		/**/                        ::serdes(s,auto_mkdir                     ) ;
		/**/                        ::serdes(s,deps_in_system                 ) ;
		/**/                        ::serdes(s,disabled                       ) ;
		/**/                        ::serdes(s,ignore_stat                    ) ;
		/**/                        ::serdes(s,mount_chroot_ok                ) ;
		/**/                        ::serdes(s,readdir_ok                     ) ;
		/**/                        ::serdes(s,fast_report_pipe               ) ;
		/**/                        ::serdes(s,service                        ) ;
		/**/                        ::serdes(s,sub_repo_s                     ) ;
		if constexpr (IsIStream<S>) ::serdes(s,       codecs                  ) ;
		else                        ::serdes(s,mk_map(codecs)                 ) ; // serialization does not support umap to ensure stability
		/**/                        ::serdes(s,views_s                        ) ;
	}
	Fd           repo_root_fd   (                    ) const ;
	bool         can_fast_report(                    ) const ;
	AcFd         fast_report_fd (                    ) const ;
	ClientSockFd slow_report_fd (                    ) const ;
	void         chk            (bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING CACHE REPO JOB
	bool                             auto_mkdir       = false ;                   // if true  <=> auto mkdir in case of chdir
	bool                             deps_in_system   = false ;                   // if false <=> system files are simple and considered as deps
	bool                             disabled         = false ;                   // if false <=> no automatic report
	bool                             ignore_stat      = false ;                   // if true  <=> stat-like syscalls do not trigger dependencies
	bool                             mount_chroot_ok  = false ;
	bool                             readdir_ok       = false ;                   // if true  <=> allow reading local non-ignored dirs
	::string                         fast_report_pipe ;                           // pipe to report accesses, faster than sockets, but does not allow replies
	KeyedService                     service          ;
	::string                         sub_repo_s       ;                           // relative to repo_root_s
	::umap_s<Codec::CodecRemoteSide> codecs           ;
	::vmap_s<::vector_s>             views_s          ;
	// END_OF_VERSIONING
	// not transported
	::string fqdn      ;
	::string fast_mail ;                                                          // host on which fast_report_pipe can be used
} ;
