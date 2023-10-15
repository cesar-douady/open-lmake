# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	from step import step

	lmake.sources = ('Lmakefile.py','step.py')

	class Cpy(lmake.Rule) :
		targets = {
			'DST'        :   'test'
		,	'SCRATCHPAD' : ( 'test.sp' , 'phony' )
		}
	#	if step==1 : cmd = '                             mv {SCRATCHPAD} {DST} ' # XXX : make mv work on CentOS 7
	#	else       : cmd = ' echo hello > {SCRATCHPAD} ; mv {SCRATCHPAD} {DST} '
		def cmd() :
			import os
			if step!=1 : print('hello',file=open(SCRATCHPAD,'w'))
			os.rename(SCRATCHPAD,DST)
else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'test' , failed=1 , rc=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'test' , done=1 , rc=0 )
