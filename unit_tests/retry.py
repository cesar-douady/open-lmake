# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class Dep1(Rule) :
		target = 'dep'
		cmd    = ': {step}'

	class Bad1(Rule) :
		target = 'dut1'
		cmd    = 'exit 1'

	class Bad2(Rule) :
		target = 'dut2'
		cmd    = 'cat dep ; exit 1'

	class Rerun(Rule) :
		target = 'rerun'
		cmd    = 'cat dep'


else :

	import os

	import ut

	print('step=1',file=open('step.py','w'))

	ut.lmake( '-r2' , 'dut1' , retry=2 , failed=3 , rc=1 ) # check retry works as expected

	ut.lmake('dep' , done=1 )
	print('step=2',file=open('step.py','w'))
	ut.lmake( '-r2' , 'dut2' , may_rerun=1 , steady=1 , retry=2 , failed=2 , rc=1 ) # check retry also works when dut would be was_failed

	os.unlink('dep')
	print('step=3',file=open('step.py','w'))
	ut.lmake( '-m1' , 'rerun' , may_rerun=1 , steady=1 , run_loop=1 , rc=1 ) # observe job in the middle of rerun loop
	ut.lmake(         'rerun' ,               steady=1                     ) # observe job at end
