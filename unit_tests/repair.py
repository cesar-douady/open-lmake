# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=2 , new=2 ) # check targets are out of date

	assert os.system('lrepair')==0
	print('hello2',file=open('hello','w'))
	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=2 , new=2 ) # check targets are remade

	assert os.system('mv LMAKE.bck LMAKE.bck2 ; lrepair')==0
	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=0 , new=2 ) # check targets are up to date
