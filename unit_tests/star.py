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
	)

	class Star(lmake.Rule) :
		targets = { 'DST' : ('{File:.*}.star{D*:\\d+}',) }
		dep     = '{File}'
		def cmd() :
			text = sys.stdin.read()
			open(f'{File}.star1','w').write(text)
			open(f'{File}.star2','w').write(text)

	class Cpy(lmake.Rule) :
		target = '{File:.*}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

else :

	import ut

	print( 'hello' , file=open('hello','w') )

	ut.lmake( 'hello.star1.cpy' ,        done=2 , new=1 )
	ut.lmake( 'hello.star2'     ,        done=0 , new=0 )
	ut.lmake( 'hello.star3'     , rc=1 , done=0 , new=0 )

