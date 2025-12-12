// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "hash.hh"

using namespace Hash ;

int main( int argc , char* argv[] ) {
	app_init({.chk_version=No}) ;
	//
	::string out ;
	for( int i : iota(1,argc) ) {
		/**/        out << ::string(Crc(argv[i])) ;
		if (argc>2) out <<' '<< argv[i]           ;
		/**/        out <<'\n'                    ;
	}
	Fd::Stdout.write(out) ;
	exit(Rc::Ok) ;
}
