# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from step import force

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'hello'
	,	'world'
	)

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*?'
		}
		target = r'{File1}+{File2}{Rc:\d*}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		force = force
		cmd = 'cat {FIRST} {SECOND} ; exit {Rc}'

else :

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	print('force=False',file=open('step.py','w'))
	ut.lmake( 'hello+world' , done=1   , new=2 )                               # check target is out of date
	ut.lmake( 'hello+world' , steady=0 , new=0 )                               # check target is remade albeit up-to-date (force)

	print('force=True',file=open('step.py','w'))
	ut.lmake( 'hello+world' , steady=1 , new=0 )                               # check target is out of date
	ut.lmake( 'hello+world' , steady=1 , new=0 )                               # check target is remade albeit up-to-date (force)

	print('force=False',file=open('step.py','w'))
	ut.lmake( 'hello+world' , steady=1 , new=0 )                               # check target is out of date
	ut.lmake( 'hello+world' , steady=0 , new=0 )                               # check target is remade albeit up-to-date (force)

	ut.lmake(        'hello+world1' , failed=1 , new=0 , rc=1 )
	ut.lmake(        'hello+world1' , failed=0 , new=0 , rc=1 )
	ut.lmake( '-e' , 'hello+world1' , failed=1 , new=0 , rc=1 )                # check target is remade when in error
