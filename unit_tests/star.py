# This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake
	from step import step

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	,	'hello'
	)

	if   step==1 : optional = (           )
	elif step==2 : optional = ('optional',)

	class Star(lmake.Rule) :
		targets = { 'DST' : ('{File:.*}.star{Digit*:\\d+}',*optional) }
		dep     = '{File}'
		def cmd() :
			text = sys.stdin.read()
			open(f'{File}.star1','w').write(text)
			open(f'{File}.star2','w').write(text)

else :

	import ut

	print( 'hello' , file=open('hello','w') )

	print( 'step=1' , file=open('step.py','w') )
	ut.lmake( 'hello.star1' ,        done=1 , new=1 )
	ut.lmake( 'hello.star2' ,        done=0 , new=0 )
	ut.lmake( 'hello.star3' , rc=1 , done=0 , new=0 )

