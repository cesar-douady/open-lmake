# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import gxx

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'gxx.py'
	,	'glob.c'
	)

	class Compile(Rule) :
		targets = { 'EXE':'glob'   }
		deps    = { 'SRC':'glob.c' }
		cmd     = f'{gxx.gxx} -o {{EXE}} -xc {{SRC}}'

	class Dut(Rule) :
		target = r'dut.{Dir:a?}.{Ok:ko|ok}'
		deps   = { 'EXE':'glob' }
		cmd    = '''
			[ {Ok} = ok ] && ldepend -D {Dir or '.'}
			./{EXE} {Dir}
		'''

else :

	import os

	import ut

	ut.mk_gxx_module('gxx')

	print('''
		#include <glob.h>
		#include <stddef.h>
		int main( int argc , const char* argv[] ) {
			glob_t g ;
			if ( argc>=2 && argv[1][0]=='a' ) glob( "a/*" , 0 , NULL , &g ) ;
			else                              glob( "*"   , 0 , NULL , &g ) ;
			globfree(&g) ;
		}
	''',file=open('glob.c','w'))

	os.makedirs('a/b',exist_ok=True)

	ut.lmake( 'dut.a.ko' , 'dut.a.ok' , done=2 , failed=1 , new=1 , rc=1 ) # check readdir-ok is necessary when reading dir a
	ut.lmake( 'dut..ko'  , 'dut..ok'  , done=1 , failed=1 ,         rc=1 ) # check readdir-ok is necessary when reading top-level
