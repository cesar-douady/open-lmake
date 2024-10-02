# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

r = 10    # number of executables in regression
n = 10000 # number of executables in sources
l = 10    # number of objects per executable
p = 5     # number of deps per object

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import Rule,PyRule

	gxx = lmake.user_environ.get('CXX') or 'g++'

	lmake.manifest = (
		'Lmakefile.py'
	,	*( f'exe_{e}.file_{o}.c' for e in range(n) for o in range(l) )
	,	*( f'inc_{i}.h'          for i in range(n*l)                 )
	)

	class Compile(Rule) :
		targets = { 'OBJ' : r'{File:.*}.o' }
		deps    = { 'SRC' : '{File}.c'     }
		autodep = 'ld_preload'                                        # clang seems to be hostile to ld_audit
		cmd     = 'PATH=$(dirname {gxx}) {gxx} -c -o {OBJ} -xc {SRC}'

	class Link(Rule) :
		targets = { 'EXE' : r'{File:.*}.exe' }
		autodep = 'ld_preload'                                                                                # clang seems to be hostile to ld_audit
		cmd     = "PATH=$(dirname {gxx}) {gxx} -o {EXE} {' '.join((f'{File}.file_{o}.o' for o in range(l)))}"

	class All(PyRule) :
		target = r'all_{N:\d+}'
		def cmd() :
			lmake.depend(*(f'exe_{e}.exe' for e in range(int(N))))

else :

	import ut

	from lmake import multi_strip

	nl = '\n'

	for e in range(n) :
		for o in range(l) :
			print(multi_strip(f'''
				{nl.join(f'#include "inc_{(e*o+i)%(n*l)}.h"' for i in range(p))}
				int {'main' if o==0 else f'foo_{o}'}() {{ return 0 ; }}
			'''),file=open(f'exe_{e}.file_{o}.c','w'))

	for i in range(n*l) : open(f'inc_{i}.h','w')

	ut.lmake( f'all_{r}' , new=... , may_rerun=r+1 , done=r*l+r , steady=1 ) # lmake all_10000 to reproduce bench conditions of : https://david.rothlis.net/ninja-benchmark
