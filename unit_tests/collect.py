# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'hello'
	,	'world'
	)

	lmake.config.collect.stems = {
		'SFX' : r'[\w/]+'
	}
	lmake.config.collect.ignore = {
		'TOK'     : ( 'tok.out' , 'tok.err' )
	,	'COLLECT' : 'dont_collect{SFX}'
	}

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

	if step==1 :
		class CatPy(Cat,PyRule) :
			target = '{File1}+{File2}_py'
			def cmd() :
				for fn in (FIRST,SECOND) :
					with open(fn) as f : print(f.read(),end='')

else :

	import os
	import os.path as osp
	import time

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	print('step=1',file=open('step.py','w'))

	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=2 , new=2   ) # check targets are out of date

	os.makedirs('to_collect1',exist_ok=True)
	open('to_collect1/a','w')
	open('to_collect2'  ,'w')
	os.makedirs('dont_collect1',exist_ok=True)
	open('dont_collect1/a','w')
	open('dont_collect2'  ,'w')

	print('step=2',file=open('step.py','w'))
	os.system('lcollect -v .')

	for f,collected in {
		'tok.out'         : False
	,	'tok.err'         : False
	,	'hello+world_py'  : True
	,	'hello+world_sh'  : False
	,	'to_collect1/a'   : True
	,	'to_collect2'     : True
	,	'dont_collect1/a' : False
	,	'dont_collect2'   : False
	}.items() :
		assert osp.exists(f)!=collected , f"{f} was {('','not ')[collected]}collected"
