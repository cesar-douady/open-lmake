# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

	def var_func() : return 'the_value'

	class Cat(lmake.Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		def force() : return False
		environ_cmd = { 'VAR' : var_func }
		deps = {
			'FIRST'  : '{File1+""}'
		,	'SECOND' : '{File2}'
		}
		cmd = 'cat $FIRST $SECOND ; echo $VAR'

	class Cmp(lmake.Rule) :
		target = '{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff $REF $DUT'

else :

	import ut

	print('hello'                  ,file=open('hello','w')          )
	print('world'                  ,file=open('world','w')          )
	print('hello\nworld\nthe_value',file=open('hello+world.ref','w'))

	ut.lmake( 'hello+world.ok' ,                 done=2 , new=3 )                 # check target is out of date
	ut.lmake( 'hello+world'    ,                 done=0 , new=0 )                 # check target is up to date
	ut.lmake( 'hello+hello'    , 'world+world' , done=2         )                 # check reconvergence
