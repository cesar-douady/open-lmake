# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class Dep(Rule) :
		target = r'dep.{N:\d+}'
		cmd    = ''

	class SubmitLoop(PyRule) :
		if step==2 : max_submits = 2
		target = 'submit_loop'
		def cmd() :
			for i in range(9) :
				open(f'dep.{i}') # discover deps one by one

	class Dut(Rule) :
		target = 'dut'
		cmd = '''
			cat submit_loop
		'''

else :

	import os
	import subprocess as sp

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'submit_loop' , new=1 , may_rerun=9 , done=10 )

	for i in range(9) : os.unlink(f'dep.{i}')
	sp.run(('lforget','-d','submit_loop'))
	print('step=2',file=open('step.py','w'))
	ut.lmake( 'dut' , may_rerun=3 , steady=2 , submit_loop=1 , was_dep_error=1 , rc=1 ) # check no crash
