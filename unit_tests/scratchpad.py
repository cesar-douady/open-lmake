# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

import lmake

autodeps = []
if lmake.has_ptrace     : autodeps.append('ptrace'    )
if lmake.has_ld_audit   : autodeps.append('ld_audit'  )
if lmake.has_ld_preload : autodeps.append('ld_preload')

if getattr(sys,'reading_makefiles',False) :

	lmake.sources = (
		'Lmakefile.py'
	,	'hello'
	)

	for ad in autodeps :
		class Cat(lmake.Rule) :
			name    = f'cat {ad}'
			target  = f'{{File:.*}}.{ad}'
			dep     = '{File}'
			autodep = ad
			cmd = '''
				echo tmp > scratch.$$
				cat
				rm scratch.$$
			'''

else :

	import ut

	print('hello',file=open('hello','w'))

	ut.lmake( *(f'hello.{ad}' for ad in autodeps) , done=len(autodeps) , new=1 ) # check scratchpad does not prevent job from running normally
