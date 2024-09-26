# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	gxx             = lmake.user_environ.get('CXX','g++')
	ld_library_path = lmake.find_cc_ld_library_path(gxx)

	depth = len(lmake.root_dir.split('/')) - 1

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello.h'
	,	'hello.c'
	,	'world.h'
	,	'world.c'
	,	'hello_world.c'
	,	'ref'
	)

	class Compile(Rule) :
		targets = { 'OBJ' : r'{File:.*}.o' }
		deps    = { 'SRC' :  '{File}.c'    }
		cmd     = '{gxx} -fprofile-arcs -c -O0 -fPIC -o {OBJ} -xc {SRC}'

	class Link(Rule) :
		targets = { 'EXE' :'hello_world' }
		deps    = {
			'MAIN' : 'hello_world.o'
		,	'SO'   : 'hello_world.so'
		}
		cmd = "{gxx} -fprofile-arcs -o {EXE} {' '.join((f'./{f}' for k,f in deps.items()))}"

	class So(Rule) :
		targets = { 'SO' : 'hello_world.so' }
		deps    = {
			'H'  : 'hello.o'
		,	'W'  : 'world.o'
		}
		cmd = "{gxx} -fprofile-arcs -o {SO} -shared {' '.join((f for k,f in deps.items()))}"

	class Dut(Rule) :
		targets     = { 'DUT':'dut' , 'GCDA':r'gcda_dir/{File*:.*}' }
		deps        = { 'EXE':'hello_world'                         }
		environ_cmd = { 'LD_LIBRARY_PATH' : ld_library_path         }
		cmd         = 'GCOV_PREFIX=gcda_dir GCOV_PREFIX_STRIP={depth} ./{EXE} >{DUT}'

	class Test(Rule) :
		target = 'test'
		deps   = {
			'DUT' : 'dut'
		,	'REF' : 'ref'
		}
		cmd = 'diff ref dut'

else :

	from lmake import multi_strip
	import ut

	print(                                         'void hello() ;'                      ,file=open('hello.h','w'))
	print(                                         'void world() ;'                      ,file=open('world.h','w'))
	print('#include <stdio.h>\n#include "hello.h"\nvoid hello() { printf("hello\\n") ; }',file=open('hello.c','w'))
	print('#include <stdio.h>\n#include "world.h"\nvoid world() { printf("world\\n") ; }',file=open('world.c','w'))
	open('hello_world.c','w').write(multi_strip('''
		#include "hello.h"
		#include "world.h"
		int main() {
			hello() ;
			world() ;
			return 0 ;
		}
	'''))
	print('hello\nworld',file=open('ref','w'))

	ut.lmake( 'test' , new=6 , done=7 )
