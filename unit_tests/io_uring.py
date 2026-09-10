# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule

	import gxx

	ld_library_path = lmake._find_cc_ld_library_path(gxx.gxx)

	lmake.manifest = (
		'Lmakefile.py'
	,	'gxx.py'
	,	'dut.cc'
	)

	class Compile(Rule) :
		targets = { 'EXE' : r'{File:.*}.exe' }
		deps    = { 'SRC' :  '{File   }.cc'  }
		autodep = 'ld_preload'                                                                                                # clang seems to be hostile to ld_audit
		cmd     = f'PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -O0 -fdiagnostics-color=always -std=c++20 -pthread -o {{EXE}} {{SRC}}'

	for ad in lmake.autodeps :
		for sfx in ('ok','ko') :
			class Dut(Rule) :
				name        = f'dut {ad} {sfx}'
				target      = rf'{{File:.*}}.{ad}.{sfx}'
				deps        = { 'EXE':'{File}.exe'                  }
				environ     = { 'LD_LIBRARY_PATH' : ld_library_path }
				io_uring_ok = sfx=='ok'
				autodep     = ad
				cmd         = './{EXE}'

else :

	import os

	import ut

	ut.mk_gxx_module('gxx')

	print('''
		#include <errno.h>
		#include <linux/io_uring.h>
		#include <sys/syscall.h>
		#include <unistd.h>

		#include <iostream>
		using namespace std ;

		int main(void) {
			struct ::io_uring_params params  = {}                                            ;
			int                      ring_fd = ::syscall( SYS_io_uring_setup , 8 , &params ) ;
			//
			if      (ring_fd>=0   ) return 0 ;
			else if (errno!=ENOSYS) return 0 ; // io_uring is disabled
			else                    return 1 ; // io_uring is forbidden by autodep
		}
	''',file=open('dut.cc','w'))

	try :
		ut.lmake( 'dut.exe' , new=1 , done=1 )
	except RuntimeError :
		print('io_uring not available',file=open('skipped','w'))
		exit()

	for ad in lmake.autodeps :
		ut.lmake( f'dut.{ad}.ok' , f'dut.{ad}.ko' , done=1 , failed=1 , rc=1 )
