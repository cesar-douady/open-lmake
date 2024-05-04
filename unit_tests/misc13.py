# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys
	import time

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class Hdep(Rule) :
		target = r'hdep'
		cmd    = 'echo {step}'

	class Sdep(Rule) :
		target = r'sdep'
		cmd    = 'echo sdep'

	class Dut(Rule) :
		target = 'dut'
		deps   = { 'SDEP' : 'sdep' }
		cmd    = '[ $(cat hdep) = 2 ] && cat sdep'

else :

	import os

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , may_rerun=1 , done=2 , failed=1 , rc=1 )

	print('step=2',file=open('step.py','w'))
	os.unlink('sdep')
	ut.lmake( 'dut' , done=2 , steady=1 )               # check sdep is not forgetten due to execution w/o hdep
