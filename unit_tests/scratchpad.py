# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake       import multi_strip
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	)

	for ad in lmake.autodeps :
		class Cat(Rule) :
			name    = f'cat {ad}'
			target  = fr'{{File:.*}}.{ad}'
			dep     =   '{ File    }'
			autodep = ad
			cmd = multi_strip('''
				echo tmp > scratch.$$
				cat
				rm scratch.$$
			''')

else :

	import ut

	print('hello',file=open('hello','w'))

	ut.lmake( *(f'hello.{ad}' for ad in lmake.autodeps) , done=len(lmake.autodeps) , new=1 ) # check scratchpad does not prevent job from running normally
