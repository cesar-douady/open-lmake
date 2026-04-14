# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule,AliasRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)
	if step==2 :
		lmake.extra_manifest = ('frozen',)

	class Dut(Rule) :
		target = 'dut'
		dep    = 'frozen'
		cmd    = 'cat'

	class Dut2(Rule) :
		prio = -1
		target = 'dut'
		cmd    = 'echo bad'

	class Test(Rule) :
		target = 'test'
		dep    = 'dut'
		cmd    = '[ "$(cat)" = good ]'

else :

	import os
	import subprocess as sp

	import ut

	def lmark(opt,file) :
		sp.run( ('lmark',opt,file) )

	print('step=1',file=open('step.py','w'))
	print('good',file=open('frozen','w'))

	ut.lmake(    'test'   ,         done=1 , failed=1 , rc=1 )
	lmark('-fa', 'frozen' )
	ut.lmake(    'test'   , new=1 , done=1 , steady=1        )
	lmark('-fd', 'frozen' )
	ut.lmake(    'test'   ,         done=1 , failed=1 , rc=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'test' , new=1 , done=1 , steady=1 )
