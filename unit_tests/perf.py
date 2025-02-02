# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	from step import n_srcs

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	*(f'src/{i}' for i in range(n_srcs))
	)

	class Base(Rule) :
		start_delay = 1
	class NoDep(Base) :
		target = r'no_dep/{:\d+}'
		cmd = ''

	class WithDeps(Base,PyRule) :
		target = r'with_deps_{N:\d+}/{:\d+}'
		def cmd() :
			lmake.depend(*(f'no_dep/{i}' for i in range(int(N))))

	class TestNoDep(Base,PyRule) :
		target = r'test1_{N:\d+}'
		def cmd() :
			lmake.depend(*(f'no_dep/{i}' for i in range(int(N))))

	class TestWithDeps(Base,PyRule) :
		target = r'test2_{P:\d+}_{N:\d+}'
		def cmd() :
			lmake.depend(*(f'with_deps_{P}/{i}' for i in range(int(N))))

else :

	import os
	import time

	import ut

	# provide a cleaning procedure as this test is meant to be repeated with various parameters from a clean base
	print('rm -rf LMAKE no_dep with_deps* test[12]_*',file=open('clean','w'))
	os.chmod('clean',0o755)

	n_srcs      = 1000
	n_no_deps   =  200
	n_deps      =   10
	n_with_deps =  100

	print(f'n_srcs = {n_srcs}',file=open('step.py','w'))

	os.makedirs('src',exist_ok=True)
	for i in range(n_srcs) :
		print('',file=open(f'src/{i}','w'))

	start = time.time()
	ut.lmake()
	t1 = time.time()-start

	print('n_srcs = 0',file=open('step.py','w'))

	start = time.time()
	ut.lmake( f'test1_{n_no_deps}'    , may_rerun=1 , done=n_no_deps , was_done=1 )
	t2 = time.time()-start

	assert n_deps<n_no_deps,'there will be some unpredictable number of reruns if 2nd level deps are not prepared before test'
	start = time.time()
	ut.lmake( f'test2_{n_deps}_{n_with_deps}' , may_rerun=1 , done=n_with_deps , was_done=1 )
	t3 = time.time()-start

	print( f'time to read {n_srcs} sources : '        , t1             )
	print( f'launch/s with no deps : '                , t2/n_no_deps   )
	print( f'launch/s with {n_deps} 2nd level deps : ', t3/n_with_deps )
