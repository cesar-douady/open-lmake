# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

r = 10    # number of executables in regression
n = 10000 # number of executables in sources
l = 10    # number of objects per executable
p = 5     # number of deps per object

use_cat = False # if True, fake gcc using cat

# to bench lmake versus bash, ninja, make and bazel :
# - from top level, run : make unit_tests/bench.dir/tok ==> dir preparation
#
# - from this dir, run : time ./run               ==> this provides a reference time for a full rebuild
# - from this dir, run :      lmake       all_{n}
# - from this dir, run : time lmake       all_{n} ==> record time to do nothing (do 3x and take fastest for a more reliable measure)
# - from this dir, run :      ninja       all_{n}
# - from this dir, run : time ninja       all_{n} ==> record time to do nothing (do 3x and take fastest for a more reliable measure)
# - from this dir, run :      make        all_{n}
# - from this dir, run : time make        all_{n} ==> record time to do nothing (do 3x and take fastest for a more reliable measure)
# - from this dir, run :      bazel build all_{n}
# - from this dir, run : time bazel build all_{n} ==> record time to do nothing (do 3x and take fastest for a more reliable measure)
#
# observe that lmake is      faster than ninja although lmake does the full job reliably
# observe that lmake is much faster than make
# observe that lmake is much faster than bazel

def compile_cmd(with_out,protect_dollar,output,input) :
	d = '$$' if protect_dollar else '$'
	if   not use_cat : return f'PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -c -pipe -o {output} -xc {input}'
	elif with_out    : return f'cat {d}(cat {input}) >{output}'
	else             : return f'cat {d}(cat {input})'

def link_cmd(with_out,output,*inputs) :
	if   not use_cat : return f"PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -o {output} {' '.join(inputs)}"
	elif with_out    : return f"cat {' '.join(inputs)} >{output}"
	else             : return f"cat {' '.join(inputs)}"

if __name__!='__main__' :

	import socket

	import gxx

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.config.backends.local.cpu *= 2

	lmake.config.backends.slurm = {
		'interface' : lmake.user_environ.get('LMAKE_INTERFACE',socket.gethostname())
	}

	import gxx

	lmake.manifest = (
		'Lmakefile.py'
	,	'gxx.py'
	,	*( f'exe_{e}.c'     for e in range(n)                   )
	,	*( f'obj_{e}_{o}.h' for e in range(n) for o in range(l) )
	,	*( f'obj_{e}_{o}.c' for e in range(n) for o in range(l) )
	)

	class Base(Rule) :
		autodep   = 'ld_preload'   # clang seems to be hostile to ld_audit
		backend   = 'local'        # replace by slurm to bench rebuild through slurm
		resources = { 'mem':'1M' }

	class Compile(Base) :
		if use_cat : target  =            r'{File:.*}.o'
		else       : targets = { 'OBJ' :  r'{File:.*}.o' }
		deps      = { 'SRC'    :   '{File}.c'             }
		side_deps = { 'GCH'    : (r'{*:.*}.gch','ignore') }
		environ   = { 'TMPDIR' : ''                       }
		cmd       = compile_cmd(False,False,'{OBJ}','{SRC}')

	class Link(Base) :
		if use_cat : target  =           r'exe_{N:\d+}.exe'
		else       : targets = { 'EXE' : r'exe_{N:\d+}.exe' }
		deps = { 'OBJ' : 'exe_{N}.o'        }
		cmd  = link_cmd(False,'{EXE}','{OBJ}',*(f'obj_{{N}}_{o}.o' for o in range(l)))

	class All(Base,PyRule) :
		target = r'all_{N:\d+}'
		def cmd() :
			lmake.depend(*(f'exe_{e}.exe' for e in range(int(N))),read=True)

else :

	from lmake import multi_strip

	import ut

	ut.mk_gxx_module('gxx')

	import gxx

	nl = '\n' # \n are forbidden in f-sting

	for e in range(n) :
		with open(f'exe_{e}.c','w') as exe :
			if use_cat :
				for o in range(l) : print(f'obj_{e}_{o}.h',file=exe)
			else :
				for o in range(l) : print(f'#include "obj_{e}_{o}.h"',file=exe)
				print(f'int main() {{ return 0 ; }}',file=exe)
		for o in range(l) :
			if use_cat :
				print(f'obj_{e}_{o}.h',file=open(f'obj_{e}_{o}.h','w'))
				with open(f'obj_{e}_{o}.c','w') as obj :
					for i in range(p) : print(f'obj_{e}_{(o+i)%l}.h',file=obj)
			else :
				print(f'int foo_{o}() ;',file=open(f'obj_{e}_{o}.h','w'))
				with open(f'obj_{e}_{o}.c','w') as obj :
					for i in range(p) : print(f'#include "obj_{e}_{(o+i)%l}.h"',file=obj)
					print(f'int foo_{o}() {{ return 0 ; }}',file=obj)

	#
	# ninja
	#
	with open('build.ninja','w') as ninja :
		ninja.write(multi_strip(f'''
			rule cc
			 command     = {compile_cmd(True,True,'$out','$in')}
			 description = Compile to $out
			rule link
			 command     = {link_cmd(True,'$out','$in')}
			 description = Link to $out
			rule gather
			 command     = touch $out
			 description = Gather to $out
		'''))
		for k in {r,n} :
			print(f"build all_{k} : gather {' '.join(f'exe_{e}.exe' for e in range(k))}",file=ninja)
		#
		for e in range(n) :
			ninja.write(multi_strip(f'''
				build exe_{e}.o   : cc   exe_{e}.c | {' '.join(f'obj_{e}_{o}.h' for o in range(l))}
				build exe_{e}.exe : link exe_{e}.o   {' '.join(f'obj_{e}_{o}.o' for o in range(l))}
			'''))
			for o in range(l) :
				print(f"build obj_{e}_{o}.o : cc   obj_{e}_{o}.c | {' '.join(f'obj_{e}_{(o+i)%l}.h' for i in range(p))}",file=ninja)

	#
	# bazel
	#
	if not use_cat :
		open('MODULE.bazel','w')
		with open('BUILD','w') as bazel :
			for k in {r,n} :
				print(f'''genrule   ( name='all_{k}_g'   , outs=['all_{k}'    ] , srcs=[{','.join(f"'//:exe_{e}_e'" for e in range(k))}] , cmd='>$@' )''',file=bazel)
			#
			for e in range(n) :
				bazel.write(multi_strip(f'''
					cc_library( name='exe_{e}_o' , copts=['-pipe','-xc']  , srcs=['exe_{e}.c'  ] , hdrs=[{','.join(f"'obj_{e}_{o}.h'" for o in range(l))}] )
					cc_binary ( name='exe_{e}_e'                          , srcs=[           ] , deps=['//:exe_{e}_o'{''.join(f",'//:obj_{e}_{o}_o'" for o in range(l))}] )
				'''))
				for o in range(l) :
					print(f'''cc_library( name='obj_{e}_{o}_o' , copts=['-pipe','-xc'] , srcs=['obj_{e}_{o}.c'] , hdrs=[{','.join(f"'obj_{e}_{(o+i)%l}.h'" for i in range(p))}] )''',file=bazel)

	#
	# make
	#
	with open('Makefile','w') as makefile :
		makefile.write(multi_strip(f'''
			%.o : %.c
				{compile_cmd(True,True,'$@','$<')}
			%.exe : %.o
				{link_cmd(True,'$@','$^')}
			all_% :
				touch $@
		'''))
		for k in {r,n} :
			print(f"all_{k} : {' '.join(f'exe_{e}.exe' for e in range(k))}",file=makefile)
		#
		for e in range(n) :
			makefile.write(multi_strip(f'''
				exe_{e}.o   : {' '.join(f'obj_{e}_{o}.h' for o in range(l))}
				exe_{e}.exe : {' '.join(f'obj_{e}_{o}.o' for o in range(l))}
			'''))
			for o in range(l) :
				print(f"obj_{e}_{o}.o : {' '.join(f'obj_{e}_{(o+i)%l}.h' for i in range(p))}",file=makefile)

	#
	# bash
	#
	import multiprocessing as mp
	import os
	try                        : n_cpu = mp.cpu_count()
	except NotImplementedError : n_cpu = 1
	n_cpu *= 2
	#
	for b in range(n_cpu) :
		with open(f'run_{b}','w') as script :
			for e in range(b,n,n_cpu) :
				for o in range(l) : print(compile_cmd(True,False,f'obj_{e}_{o}.o',f'obj_{e}_{o}.c'                                  ),file=script)
				pass ;              print(compile_cmd(True,False,f'exe_{e}.o'    ,f'exe_{e}.c'                                      ),file=script)
				pass ;              print(link_cmd   (True      ,f'exe_{e}.exe'  ,f'exe_{e}.o',*(f'obj_{e}_{o}.o' for o in range(l))),file=script)
		os.chmod(f'run_{b}',0o755)
	with open('run','w') as script :
		for b in range(n_cpu) : print(f'./run_{b} &'                                     ,file=script)
		pass ;                  print(f"wait {' '.join(f'%{b+1}' for b in range(n_cpu))}",file=script)
	os.chmod('run',0o755)

	#
	# run bench
	#
	with open('bench','w') as bench :
		bench.write(multi_strip(r'''
			set -x
			# lmake
			rm -rf LMAKE
			ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f ; time ../../bin/lmake       all_10000 >/dev/null
			ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f ; time ../../bin/lmake       all_10000 >/dev/null
			: ;                                                   time ../../bin/lmake       all_10000 >/dev/null
			: ;                                                   time ../../bin/lmake       all_10000 >/dev/null
			: ;                                                   time ../../bin/lmake       all_10000 >/dev/null
			: ;                                                   time ../../bin/lmake       all_10    >/dev/null
			: ;                                                   time ../../bin/lmake       all_10    >/dev/null
			: ;                                                   time ../../bin/lmake       all_10    >/dev/null
			: ;                                                   time ../../bin/lmake       all_10    >/dev/null
			# ninja
			rm -f .ninja_log
			ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f ; time ninja           -j 16 all_10000 >/dev/null
			ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f ; time ninja           -j 16 all_10000 >/dev/null
			: ;                                                   time ninja           -j 16 all_10000 >/dev/null
			: ;                                                   time ninja           -j 16 all_10000 >/dev/null
			: ;                                                   time ninja           -j 16 all_10000 >/dev/null
			: ;                                                   time ninja           -j 16 all_10    >/dev/null
			: ;                                                   time ninja           -j 16 all_10    >/dev/null
			: ;                                                   time ninja           -j 16 all_10    >/dev/null
			: ;                                                   time ninja           -j 16 all_10    >/dev/null
			# make
			ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f ; time make            -j 16 all_10000 >/dev/null
			: ;                                                   time make            -j 16 all_10000 >/dev/null
			: ;                                                   time make            -j 16 all_10000 >/dev/null
			: ;                                                   time make            -j 16 all_10000 >/dev/null
			: ;                                                   time make            -j 16 all_10    >/dev/null
			: ;                                                   time make            -j 16 all_10    >/dev/null
			: ;                                                   time make            -j 16 all_10    >/dev/null
			: ;                                                   time make            -j 16 all_10    >/dev/null
		'''))
		if not use_cat : # bazel is not supported with cat
			bench.write(multi_strip(fr'''
				# bazel
				rm -rf ~/.cache/bazel
				export CC={gxx.gxx}
				ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f ; time bazel build --spawn_strategy=local --jobs=16 all_10000 >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10000 >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10000 >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10000 >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10    >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10    >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10    >/dev/null
				: ;                                                   time bazel build --spawn_strategy=local --jobs=16 all_10    >/dev/null
				unset CC
				unset BAZEL_CXXOPTS
			'''))
		bench.write(multi_strip(r'''
			# bash
			ls | grep -x '.*\.o\|.*\.exe\|all_.*' | xargs rm -f  ; time ./run
		'''))
	os.chmod('bench',0o755)

	#
	# check
	#
	ut.lmake( f'all_{r}' , new=... , may_rerun=r+1 , done=r*l+2*r+1 ) # lmake all_10000 to reproduce bench conditions of : https://david.rothlis.net/ninja-benchmark
