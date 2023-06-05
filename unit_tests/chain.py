# This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	,	'hello+world.ref'
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
		cmd = 'cat $FIRST $SECOND'

	class Cmp(BaseRule) :
		target = '{File}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff $REF $DUT'

else :

	import ut

	print( 'hello'                                   , file=open('hello'          ,'w')          )
	print( 'world'                                   , file=open('world'          ,'w')          )
	print( open('hello').read()+open('world').read() , file=open('hello+world.ref','w') , end='' )

	ut.lmake( 'hello+world.ok' , done=2 , new=3 )
