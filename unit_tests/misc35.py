# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	if step==1 :
		class Dut(Rule) :
			target = 'dut'
			cmd    = ''

else :

	import os
	import os.path as osp
	import time

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , done=1 )
	assert os.system('lmark -fa dut')==0

	print('step=2',file=open('step.py','w'))
	ut.lmake()
	assert os.system('lmark -fl')==0
