// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"

using namespace Disk ;

using namespace Hash ;

int main( int argc , char* argv[] ) {
	#if PROFILING
		::string gmon_dir_s ; try { gmon_dir_s = search_root_dir_s().first+GMON_DIR_S ; } catch(...) {}
		set_env( "GMON_OUT_PREFIX" , dir_guard(gmon_dir_s+"align_comment") ) ;                          // in case profiling is used, ensure unique gmon.out
	#endif
	for( int i : iota(1,argc) ) {
		::cout << ::string(Crc(argv[i])) ;
		if (argc>2) ::cout <<' '<< argv[i] ;
		::cout <<'\n' ;
	}
	return 0 ;
}
