// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "lib.hh"

using namespace Disk ;

#pragma GCC visibility push(default)                                           // force visibility of functions defined hereinafter, until the corresponding pop
extern "C" {
	const char* __asan_default_options () { return "verify_asan_link_order=0,detect_leaks=0" ; }
	const char* __ubsan_default_options() { return "halt_on_error=1"                         ; }
	const char* __tsan_default_options () { return "report_signal_unsafe=0"                  ; }
}
#pragma GCC visibility pop

::pair_ss search_root_dir(::string const& cwd_) {
	::string root_s_dir = cwd_ ;
	if (root_s_dir.empty()     ) root_s_dir =           cwd()           ;
	if (root_s_dir.front()!='/') root_s_dir = to_string(cwd(),'/',cwd_) ;
	::vector_s candidates ;
	for(; !root_s_dir.empty() ; root_s_dir = dir_name(root_s_dir) ) if (is_target(root_s_dir+"/Lmakefile.py")) candidates.push_back(root_s_dir) ;
	switch (candidates.size()) {
		case 0 : throw "cannot find root dir"s ;
		case 1 : root_s_dir = candidates[0] ; break ;
		default : {
			::vector_s candidates2 ;
			for( ::string const& c : candidates ) if (is_dir(c+"/LMAKE")) candidates2.push_back(c) ;
			switch (candidates2.size()) {
				case 0 : {
					::string msg = "ambiguous root dir, disambiguate by executing one of :\n" ;
					for( ::string const& c : candidates ) msg += to_string("\tmkdir ",c,"/LMAKE\n") ;
					throw msg ;
				}
				case 1 : root_s_dir = candidates2[0] ; break ;
				default : {
					::string msg = to_string("ambiguous root dir, disambiguate by executing ",candidates2.size()-1," of :\n") ;
					for( ::string const& c : candidates2 ) msg += to_string("\trm -r ",c,"/LMAKE\n") ;
					throw msg ;
				}
			}
		}
	}
	::string startup_dir_s ;
	if (root_s_dir.size()<cwd_.size()) startup_dir_s = cwd_.substr(root_s_dir.size()+1)+'/' ;
	return {root_s_dir,startup_dir_s} ;
}
::pair_ss search_root_dir() { return search_root_dir(cwd()) ; }
