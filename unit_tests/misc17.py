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
		targets = { 'T' : '{*:.}' }
		if   step==1 : cmd = 'echo a  >a ; echo b >b'
		elif step==2 : cmd = 'echo a  >a'
		elif step==3 : cmd = 'echo a2 >a'
		elif step==4 : cmd = 'echo a  >a'

	if step==4 :
		class Alternate(Rule) :
			target = 'b'
			prio   = -1
			cmd    = 'echo b2'

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'cat a b'

else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , may_rerun=1 , done=2 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'dut' , steady=1 , failed=1 , rc=1 )

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , done=2 )

	print('step=3',file=open('step.py','w'))
	ut.lmake( 'dut' , done=1 , failed=1 , rc=1 )

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , done=2 )

	print('step=4',file=open('step.py','w'))
	ut.lmake( 'dut' , steady=1 , done=2 )
