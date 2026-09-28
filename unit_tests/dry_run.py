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
	,	'src'
	)

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'cat sub1 sub2'

	class Sub1(Rule) :
		target = 'sub1'
		dep    = 'src'
		cmd    = 'cat'

	if step==1 :
		class Sub2(Rule) :
			target = 'sub2'
			cmd    = 'echo sub2'

else :

	import os
	import subprocess as sp

	import ut

	print('step=1',file=open('step.py','w'))

	print('v1',file=open('src','w'))

	ut.lmake( 'dut' , new=1 , may_rerun=1 , done=3 )
	res = sp.run( ('lmake','-n','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut' not in res and 'sub1' not in res and 'sub2' not in res

	os.unlink('sub1')
	res = sp.run( ('lmake','-n','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut' not in res and 'sub1' not in res and 'sub2' not in res
	res = sp.run( ('lmake','-na','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut' not in res and 'sub1' in res and 'sub2' not in res

	print('v2',file=open('src','w'))
	res = sp.run( ('lmake','-n','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut' in res and 'sub1' in res and 'sub2' not in res

	ut.lmake( 'dut' , done=2 )

	print('step=2',file=open('step.py','w'))
	res = sp.run( ('lmake','-n','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut' in res and 'sub1' not in res and 'sub2' in res and 'unlink' in res

	print('pollute',file=open('sub2','w'))
	res = sp.run( ('lmake','-n','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut' in res and 'sub1' not in res and 'sub2' in res and 'quarantine' in res
