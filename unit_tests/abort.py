# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dut.c'
	)

	class Compile(Rule) :
		targets = { 'EXE' : r'{File:.*}.exe' }
		deps    = { 'SRC' :  '{File}.c'      }
		cmd     = 'gcc -std=c99 -o {EXE} {SRC}'

	class Dut(Rule) :
		target   = r'{File:.*}.out'
		deps     = { 'EXE':'{File}.exe' }
		tmp_view = '/tmp'
		cmd      = './{EXE}'

else :

	import textwrap

	import ut

	open('dut.c','w').write(textwrap.dedent('''
		#include <stdlib.h>
		int main() {
			abort() ;
		}
	'''[1:])) # strip initial \n

	ut.lmake( 'dut.out' , new=1 , done=1 , failed=1 , rc=1 )
