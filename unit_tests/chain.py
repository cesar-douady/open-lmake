# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'lmake_read_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	,	'hello.cpy+world.ref'
	)

	class BaseRule(lmake.Rule) :
		stems = { 'File' : r'.*' }
		stems['File1'] = stems['File']
		stems['File2'] = stems['File']

	class Cat(BaseRule) :
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		cmd = 'cat {FIRST} {SECOND}'

	class Cpy(BaseRule) :
		target = '{File:.*}.cpy'
		dep    = '{File}'
		cmd = 'cat'

	class Cmp(BaseRule) :
		target = '{File}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import ut

	print( 'hello'        , file=open('hello'              ,'w') )
	print( 'world'        , file=open('world'              ,'w') )
	print( 'hello\nworld' , file=open('hello.cpy+world.ref','w') )

	ut.lmake( 'hello.cpy+world.ok' , done=3 , new=3 )
