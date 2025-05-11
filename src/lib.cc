// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "lib.hh"

using namespace Disk ;

#pragma GCC visibility push(default) // force visibility of functions defined hereinafter, until the corresponding pop
extern "C" {
	const char* __asan_default_options () { return "verify_asan_link_order=0,detect_leaks=0" ; }
	const char* __ubsan_default_options() { return "halt_on_error=1"                         ; }
	const char* __tsan_default_options () { return "report_signal_unsafe=0"                  ; }
}
#pragma GCC visibility pop

SearchRootResult search_root(::string const& cwd_s_) {
	::string   from_dir_s  = cwd_s_[0]=='/' ? cwd_s_ : cwd_s()+cwd_s_ ;
	::string   repo_root_s = from_dir_s                               ;
	::vector_s candidates  ;
	for(;; repo_root_s=dir_name_s(repo_root_s) ) {
		if ( is_target(repo_root_s+"Lmakefile.py") || is_target(repo_root_s+"Lmakefile/__init__.py") ) candidates.push_back(repo_root_s) ;
		if ( repo_root_s.size()==1                                                                   ) break ;
	}
	switch (candidates.size()) {
		case 0 : throw "cannot find root dir"s ;
		case 1 : repo_root_s = candidates[0] ; break ;
		default : {
			::vector_s candidates2 ;
			for( ::string const& c : candidates ) if (is_dir(no_slash(c+AdminDirS))) candidates2.push_back(c) ;
			switch (candidates2.size()) {
				case 0 : {
					::string msg = "ambiguous root dir, to disambiguate, consider one of :\n" ;
					for( ::string const& c : candidates ) msg << "\tmkdir " << no_slash(c+AdminDirS) <<'\n' ;
					throw msg ;
				}
				case 1 : repo_root_s = ::move(candidates2[0]) ; break ;
				default : {
					::string msg = cat("ambiguous root dir, to disambiguate, consider ",candidates2.size()-1," of :\n") ;
					for( ::string const& c : candidates2 ) msg << "\trm -r " << no_slash(c+AdminDirS) <<'\n' ;
					throw msg ;
				}
			}
		}
	}
	return { .top_s=repo_root_s , .sub_s=candidates[0].substr(repo_root_s.size()) , .startup_s=from_dir_s.substr(repo_root_s.size()) } ;
}
