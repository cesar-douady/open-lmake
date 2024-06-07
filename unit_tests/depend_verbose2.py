# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep0'
	,	'step.py'
	)

	from step import step

	class Dep(Rule):
		target = 'dep'
		dep    = f'dep{step}'
		cmd    = 'cat'

	class Dut(PyRule) :
		target  = r'dut{R:[01]}'
		def cmd():
			from lmake import depend
			status = depend('dep',verbose=True,required=int(R))
			if status['dep'][0] and status['dep'][1]!='none' :
				open('dep')
			print(status)

else :

	import ut
	open('dep0','w').close()
	print('step=0',file=open('step.py','w'))
	cnts = ut.lmake( 'dut0' , 'dut1' , new=1 , done=3 , rerun=... , may_rerun=... )
	assert cnts.rerun+cnts.may_rerun==2

	import os
	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut0' , unlinked=1 , done=1 )
	ut.lmake( 'dut1' ,                       rc=1 ) # dut1 cannot be built with a non-buildable required dep
