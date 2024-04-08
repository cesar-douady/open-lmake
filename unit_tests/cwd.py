# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'sub/hello_sub'
	,	'sub/world_sub'
	,	'hello'
	,	'world'
	)

	class Cat(Rule) :
		cwd = 'sub'
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}

	class CatSh(Cat) :
		target = '{File1}+{File2}_sh'
		cmd    = 'cat {FIRST} {SECOND}'

	class CatPy(Cat,PyRule) :
		target = '{File1}+{File2}_py'
		def cmd() :
			print(open(FIRST ).read(),end='')
			print(open(SECOND).read(),end='')
			lmake.get_autodep()               # check get_autodep works in a subdir

else :

	import os

	import ut

	os.mkdir('sub')
	print('hello'        ,file=open('hello'        ,'w'))
	print('world'        ,file=open('world'        ,'w'))
	print('sub/hello_sub',file=open('sub/hello_sub','w'))
	print('sub/world_sub',file=open('sub/world_sub','w'))

	ut.lmake( 'sub/hello_sub+world_sub_sh' , 'sub/hello_sub+world_sub_py' , done=2 , new=2 ) # check targets are out of date
	ut.lmake( 'hello+world_sh'             , 'hello+world_py'             , done=0 , rc=1  ) # check targets are not buildable
