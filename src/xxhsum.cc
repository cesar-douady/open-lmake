// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"

using namespace Hash ;

int main( int argc , char* argv[] ) {
	::cout << ::string(Crc(0xff0001L))<<endl ;
	for( int i=1 ; i<argc ; i++ ) {
		::cout << ::string(Crc(argv[i],Algo::Xxh)) ;
		if (argc>2) ::cout <<' '<< argv[i] ;
		::cout <<'\n' ;
	}
	return 0 ;
}
