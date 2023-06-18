# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'world'
	,	'world.ref'
	)

	class HelloWorld(lmake.Rule) :
		targets = {
			'HELLO' :   'hello'
		,	'WORLD' : ( 'world' , 'SourceOk' )
		}
		cmd = 'echo hello >$HELLO ; echo world > $WORLD'

	class Cmp(lmake.Rule) :
		target = '{File:.*}.ok'
		deps   = {
			'ACTUAL' : '{File}'
		,	'REF'    : '{File}.ref'
		}
		cmd = 'diff $REF $ACTUAL >&2'


else :

	import ut

	print('moon' ,file=open('world'    ,'w'))
	print('world',file=open('world.ref','w'))

	ut.lmake( 'hello'    , done=1 , new=0 , source= 1 )                        # check rule is ok despite writing to source world
	ut.lmake( 'world.ok' , done=1 , new=1 )                                    # check world has been updated
