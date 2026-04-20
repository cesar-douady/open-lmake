# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	,	'step.py'
	)

	from step import step

	class Dut(PyRule) :
		target = 'dut'
		if step==1:
			def cmd() :
				lmake.target('src', source_ok=True)
				print('from_job',file=open('src','w'))
		else:
			def cmd() :
				open('src')

else :

	import subprocess as sp
	import ut

	print('src',file=open('src','w'))

	print(f'step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , done=1 )
	sp.run(('lshow', '-t', 'dut'), check=True)

	print(f'step=2',file=open('step.py','w'))
	ut.lmake( 'dut' , steady=1 )
	out = sp.check_output( ('lshow','-t','dut') , universal_newlines=True )
	assert 'src' not in out, 'src should not appear as a target in step 2'

