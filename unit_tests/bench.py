# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

r = 10    # number of executables in regression
n = 10000 # number of executables in sources
l = 10    # number of objects per executable
p = 5     # number of deps per object

# to bench lmake versus ninja :
# - from top level, run : make unit_tests/bench.dir/tok ==> dir preparation
#
# - from this dir, run :      lmake all_{n}
# - from this dir, run : time lmake all_{n} ==> record time to do nothing (do 3x and take fastest for a more reliable measure)
# - from this dir, run :      ninja all_{n}
# - from this dir, run : time ninja all_{n} ==> record time to do nothing (do 3x and take fastest for a more reliable measure)
# observe that lmake is faster than ninja although lmake does the full job reliably

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	import gxx

	lmake.manifest = (
		'Lmakefile.py'
	,	'gxx.py'
	,	*( f'exe_{e}.c'     for e in range(n)                   )
	,	*( f'obj_{e}_{o}.h' for e in range(n) for o in range(l) )
	,	*( f'obj_{e}_{o}.c' for e in range(n) for o in range(l) )
	)

	class Compile(Rule) :
		targets = { 'OBJ' : r'{File:.*}.o' }
		deps    = { 'SRC' : '{File}.c'     }
		autodep = 'ld_preload'                                               # clang seems to be hostile to ld_audit
		cmd     = 'PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -c -o {OBJ} -xc {SRC}'

	class Link(Rule) :
		targets = { 'EXE' : r'exe_{N:\d+}.exe' }
		deps    = { 'OBJ' : 'exe_{N}.o'        }
		autodep = 'ld_preload'                                                                                       # clang seems to be hostile to ld_audit
		cmd     = "PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -o {EXE} {OBJ} {' '.join(f'obj_{N}_{o}.o' for o in range(l))}"

	class All(PyRule) :
		target = r'all_{N:\d+}'
		def cmd() :
			lmake.depend(*(f'exe_{e}.exe' for e in range(int(N))))

else :

	from lmake import multi_strip
	import ut

	ut.mk_gxx_module('gxx')

	import gxx

	nl = '\n' # \n are forbidden in f-sting

	with open('build.ninja','w') as ninja :
		ninja.write(multi_strip(f'''
			rule cc
			 command     = PATH={gxx.gxx_dir}:$$PATH {gxx.gxx} -c -o $out -xc $in
			 description = Compile to $out
			rule link
			 command     = PATH={gxx.gxx_dir}:$$PATH {gxx.gxx} -o $out $in
			 description = Link to $out
			rule gather
			 command     = touch $out
			 description = Gather to $out
		'''))
		if True : print(f"build all_{r} : gather {' '.join(f'exe_{e}.exe' for e in range(r))}",file=ninja)
		if n!=r : print(f"build all_{n} : gather {' '.join(f'exe_{e}.exe' for e in range(n))}",file=ninja)
		#
		for e in range(n) :
			with open(f'exe_{e}.c','w') as exe :
				for o in range(l) : print(f'#include "obj_{e}_{o}.h"',file=exe)
				print(f'int main() {{ return 0 ; }}',file=exe)
			ninja.write(multi_strip(f'''
				build exe_{e}.o   : cc   exe_{e}.c | {' '.join(f'obj_{e}_{o}.h' for o in range(l))}
				build exe_{e}.exe : link exe_{e}.o   {' '.join(f'obj_{e}_{o}.o' for o in range(l))}
			'''))
			for o in range(l) :
				print(f'int foo_{o}() ;',file=open(f'obj_{e}_{o}.h','w'))
				with open(f'obj_{e}_{o}.c','w') as obj :
					for i in range(p) : print(f'#include "obj_{e}_{(o+i)%l}.h"',file=obj)
					print(f'int foo_{o}() {{ return 0 ; }}',file=obj)
				print(f"build obj_{e}_{o}.o : cc obj_{e}_{o}.c | {' '.join(f'obj_{e}_{(o+i)%l}.h' for i in range(p))}",file=ninja)

	ut.lmake( f'all_{r}' , new=... , may_rerun=r+1 , done=r*l+2*r , was_done=1 ) # lmake all_10000 to reproduce bench conditions of : https://david.rothlis.net/ninja-benchmark
