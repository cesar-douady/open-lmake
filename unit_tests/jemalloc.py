# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	)

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		autodep     = 'ld_preload_jemalloc'
		environ_cmd = { 'LD_PRELOAD':'libjemalloc.so' }

	class CatSh(Cat) :
		target = '{File1}+{File2}_sh'
		cmd    = 'cat {FIRST} {SECOND}'

	class CatPy(Cat,PyRule) :
		target = '{File1}+{File2}_py'
		def cmd() :
			print(open(FIRST ).read(),end='')
			print(open(SECOND).read(),end='')

else :

	import os
	import subprocess as sp

	os.environ['LD_PRELOAD'] = 'libjemalloc.so'
	has_jemalloc             = not sp.run(('/usr/bin/echo',),check=True,stderr=sp.PIPE).stderr

	if has_jemalloc :

		import ut

		print('hello',file=open('hello','w'))
		print('world',file=open('world','w'))

		ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=2 , new=2 )           # check no crash
