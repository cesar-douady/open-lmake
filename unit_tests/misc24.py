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

	class Dut(Rule) :
		targets = { 'DUT':('dut','optional') }
		if step==1 : cmd = 'echo bad >{DUT}'
		else       : cmd = ''

	class Good(Rule) :
		prio   = -1
		target = 'dut'
		cmd    = 'echo good'

	class Test(Rule) :
		target = 'test'
		deps   = { 'DUT':'dut' }
		cmd    = '[ "$(cat {DUT})" = good ]'

else :

	import ut

#	print('step=1',file=open('step.py','w'))
#	ut.lmake( 'test' , done=2 )

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , done=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'test' , done=3 )
