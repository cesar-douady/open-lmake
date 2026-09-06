# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = [
		'Lmakefile.py'
	,	'step.py'
	]

	from step import step

	class Dut(Rule) :
		if step==1 : targets = { 'DUT' : 'dut.{:.*}'  }
		else       : targets = { 'DUT' : 'dut.{*:.*}' }
		cmd = 'echo >dut.1'

else :

	import os

	import ut

	# check static->star
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'dut.1' , done  =1 )
	print('step=2',file=open('step.py','w')) ; ut.lmake( 'dut.1' , steady=1 )

	os.system('rm -rf LMAKE dut.1')

	# check star->static
	print('step=2',file=open('step.py','w')) ; ut.lmake( 'dut.1' , done  =1 )
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'dut.1' , steady=1 )
