// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"

enum class FileLoc : uint8_t {
	Repo
,	SrcDir       // file has been found in source dirs
,	RepoRoot     // file is repo root
,	Tmp
,	Proc         // file is in /proc
,	Admin
,	Ext          // all other cases
,	Unknown
//
// aliases
,	Dep = SrcDir // <=Dep means that file must be reported as a dep
} ;

// START_OF_VERSIONING
enum class LnkSupport : uint8_t {
	None
,	File
,	Full
} ;
// END_OF_VERSIONING

struct RealPathEnv {
	friend ::string& operator+=( ::string& , RealPathEnv const& ) ;
	// services
	FileLoc file_loc(::string const& file) const ;
	void    chk     (bool for_cache=false) const ;
	// data
	LnkSupport lnk_support = LnkSupport::Full ; // by default, be pessimistic
	FileSync   file_sync   = FileSync::Dflt   ;
	::string   repo_root_s = {}               ;
	::string   tmp_dir_s   = {}               ;
	::vector_s src_dirs_s  = {}               ;
} ;

struct RealPath {
	friend ::string& operator+=( ::string& , RealPath const& ) ;
	struct SolveReport {
		friend ::string& operator+=( ::string& , SolveReport const& ) ;
		// data
		::string   real          = {}           ;                     // real path relative to root if in_repo or in a relative src_dir or absolute if in an absolute src_dir, else empty
		::vector_s lnks          = {}           ;                     // links followed to get to real
		Bool3      file_accessed = No           ;                     // if True, file was accessed as sym link, if Maybe file dir was accessed as sym link
		FileLoc    file_loc      = FileLoc::Ext ;                     // do not process awkard files
	} ;
private :
	// helper class to help recognize when we are in repo or in tmp
	struct _Dvg {
		// cxtors & casts
		_Dvg( ::string_view domain_s , ::string const& chk ) { update(domain_s,chk) ; }
		// accesses
		bool operator+() const { return ok ; }
		// services
		void update( ::string_view domain_s , ::string const& chk ) ; // udpate after domain_s & chk have been lengthened or shortened, but not modified internally
		// data
		bool   ok  = false ;
		size_t dvg = 0     ;
	} ;
	// cxtors & casts
public :
	// src_dirs_s may be either absolute or relative, but must be canonic
	// tmp_dir_s must be absolute and canonic
	RealPath ( RealPathEnv const& rpe , pid_t p=0 ) ;
	// services
	FileLoc file_loc(::string const& real) const { return _env->file_loc(real) ; }
	//
	SolveReport solve( FileView , bool no_follow=false ) ;
	//
	::vmap_s<Accesses> exec(SolveReport&&) ;                          // arg is updated to reflect last interpreter
	//
	void chdir() ;
	::string cwd() {
		if ( !pid && ::getpid()!=_cwd_pid ) chdir() ;                 // refresh _cwd if it was updated in the child part of a clone
		return _cwd ;
	}
private :
	size_t _find_src_idx(::string const& real) const ;
	// data
public :
	pid_t pid = 0 ;
private :
	RealPathEnv const* _env            = nullptr ;
	::vector_s         _abs_src_dirs_s ;                              // this is an absolute version of src_dirs
	size_t             _repo_root_sz   ;
	::string           _cwd            ;
	pid_t              _cwd_pid        = 0       ;                    // pid for which _cwd is valid if pid==0
	NfsGuard           _nfs_guard      ;
} ;
::string& operator+=( ::string& , RealPath::SolveReport const& ) ;
