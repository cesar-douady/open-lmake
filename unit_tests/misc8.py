# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.config.link_support = None # avoid dut1 being read as a link when running dut2 for the first time

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	class Dep(Rule) :
		target = 'dep'
		cmd = 'echo {step}'

	class Dut1(Rule) :
		target = 'dut1'
		cmd    = 'echo good'

	class Dut2(Rule) :
		target = 'dut2'
		deps = { 'DUT1' : 'dut1' }
		cmd = '''
			if   [ $(cat dep) = 1 ]
			then echo bad ; echo bad > dut1
			else cat dut1
			fi
		'''

	class Chk(Rule) :
		target = 'chk'
		deps   = { 'DUT' : 'dut2' }
		cmd = '[ $(cat {DUT}) = good ]'

else :

	import os

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dep' , done=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'chk' , may_rerun=1 , done=5 ) # check that dut1 is remade after first run of dut2 despite having been done before
