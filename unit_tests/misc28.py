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
		lmake.extra_manifest = ('dep',)

	class MkDep(Rule) :
		target = 'dep'
		cmd    = ''

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'cat dep'

else :

	import os
	import subprocess as sp

	import ut

	def lmark(opt,file) :
		sp.run( ('lmark',opt,file) )

	print('step=1',file=open('step.py','w'))

	ut.lmake(    'dut' , may_rerun=1 , done=2 )
	lmark('-fa', 'dep' )
	os.unlink(   'dep' )
	lmark('-fd', 'dep' )
	open(        'dep' , 'w' )

	print('step=2',file=open('step.py','w'))

	lmark('-fa','dep')
