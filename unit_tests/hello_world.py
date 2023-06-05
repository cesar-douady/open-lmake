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
	)

	class Cat(lmake.Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		cmd = 'cat $FIRST $SECOND'

else :

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world' ,                 done=1 , new=2 )                 # check target is out of date
	ut.lmake( 'hello+world' ,                 done=0 , new=0 )                 # check target is up to date
	ut.lmake( 'hello+hello' , 'world+world' , done=2         )                 # check reconvergence
