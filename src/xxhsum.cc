// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/resource.h>

#include "app.hh"
#include "disk.hh"
#include "hash.hh"

using namespace Disk ;

using namespace Hash ;

int main( int argc , char* argv[] ) {
	app_init(true/*read_only_ok*/,No/*chk_version*/,No/*cd_root*/) ;
	#if PROFILING
		::string gmon_dir_s ; if (g_repo_root_s) gmon_dir_s = *g_repo_root_s+AdminDirS+"gmon.out/" ;
		set_env( "GMON_OUT_PREFIX" , dir_guard(gmon_dir_s+"xxhsum") ) ;                              // in case profiling is used, ensure unique gmon.out
	#endif
	::string out ;
	for( int i : iota(1,argc) ) {
		/**/        out << ::string(Crc(argv[i])) ;
		if (argc>2) out <<' '<< argv[i]           ;
		/**/        out <<'\n'                    ;
	}
	Fd::Stdout.write(out) ;
	return 0 ;
}
