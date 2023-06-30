// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "lib.hh"

using namespace Disk ;

::string* g_root_dir = nullptr ;
::string* g_tmp_dir  = nullptr ;

::pair_ss search_root_dir(::string const& cwd_) {
	::string root_s_dir    = cwd_ ;
	::string startup_dir_s ;
	if (root_s_dir.empty()     ) root_s_dir =           cwd()           ;
	if (root_s_dir.front()!='/') root_s_dir = to_string(cwd(),'/',cwd_) ;
	while (!is_target(root_s_dir+"/Lmakefile.py")) {
		if (root_s_dir.empty()) throw "cannot find root dir"s ;
		root_s_dir = dir_name(root_s_dir) ;
	}
	if (root_s_dir.size()<cwd_.size()) startup_dir_s = cwd_.substr(root_s_dir.size()+1)+'/' ;
	return {root_s_dir,startup_dir_s} ;
}
::pair_ss search_root_dir() { return search_root_dir(cwd()) ; }

void lib_init(::string const& root_dir) {
	SWEAR( is_abs_path(root_dir) ) ;                                           // root_dir is a successtion of components prefixed by /, if at root, it must be empty
	if (!g_tmp_dir) g_tmp_dir   = new ::string{get_env("TMPDIR",DfltTmp)} ;
	g_root_dir = new ::string{root_dir} ;
}
