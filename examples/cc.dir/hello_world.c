// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <stdio.h>
#include <stdlib.h>

#include "hello.h"
#include "world.h"

int main( int argc , const char* argv[] ) {
	if (argc>2) {
		fprintf(stderr,"%s accepts at most a single argument, %d given\n",argv[0],argc-1) ;
		exit(2) ;
	}
	printf("%s %s\n",hello(),world(argv[1])) ;
	return 0 ;
}
