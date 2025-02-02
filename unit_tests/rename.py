# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from step import step,python

	lmake.manifest = ('Lmakefile.py','step.py')

	class Cpy(Rule) :
		targets      = { 'DST'        : 'test'    }
		side_targets = { 'SCRATCHPAD' : 'test.sp' }
		if python :
			def cmd() :
				import os
				if step!=1 : print('hello',file=open(SCRATCHPAD,'w'))
				os.rename(SCRATCHPAD,DST)
		else :
			if step==1 : cmd = '                             mv {SCRATCHPAD} {DST} '
			else       : cmd = ' echo hello > {SCRATCHPAD} ; mv {SCRATCHPAD} {DST} '
else :

	import ut

	for python in (False,True) :
		print(f'step=1 ; python={python}',file=open('step.py','w'))
		ut.lmake( 'test' , new=python , failed=1 , rc=1 )                      # python reads Lmakefile.py to display backtrace

		print(f'step=2 ; python={python}',file=open('step.py','w'))
		ut.lmake( 'test' , done=1 , rc=0 )
