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
	,	'src1'
	,	'src2'
	)

	class Common(Rule) :
		target = 'common'
		dep    = 'src2'
		cmd    = 'cat src2'

	class Dut1(Rule) :
		target = 'dut1'
		deps   = { 'SUB1' : 'sub1' }
		cmd    = 'cat {SUB1} sub2'

	class Sub1(Rule) :
		target = 'sub1'
		deps = {
			'SRC'    : 'src1'
		,	'COMMON' : 'common'
		}
		cmd = 'cat {SRC} {COMMON}'

	if step==1 :
		class Sub2(Rule) :
			target = 'sub2'
			deps   = { 'COMMON' : 'common' }
			cmd    = 'echo sub2 ; cat common'

	class Dut2(Rule) :
		target = 'dut2{:.*}'
		cmd    = 'echo basic'

	if step==2 :
		class SuperDut2(Rule) :
			prio   = 1
			target = 'dut2'
			cmd    = 'echo super'

else :

	import os
	import subprocess as sp

	import ut

	print('step=1',file=open('step.py','w'))

	print('v1',file=open('src1','w'))
	print('v1',file=open('src2','w'))

	res = sp.run( ('lmake','-n','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' in res and 'sub1' in res and 'sub2' not in res and 'common' in res

	ut.lmake( 'dut1' , may_rerun=1 , done=4 )
	res = sp.run( ('lmake','-n','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' not in res and 'sub1' not in res and 'sub2' not in res

	os.unlink('sub1')
	res = sp.run( ('lmake','-n','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' not in res and 'sub1' not in res and 'sub2' not in res
	res = sp.run( ('lmake','-na','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' not in res and 'sub1' in res and 'sub2' not in res

	print('v2',file=open('src1','w'))
	res = sp.run( ('lmake','-n','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' in res and 'sub1' in res and 'sub2' not in res and 'common' not in res

	print('v2',file=open('src2','w'))
	res = sp.run( ('lmake','-n','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' in res and 'sub1' in res and 'sub2' in res and 'common' in res

	ut.lmake( 'dut1' , done=4 )

	ut.lmake( 'dut2' , done=1 )

	print('step=2',file=open('step.py','w'))

	res = sp.run( ('lmake','-n','sub2') , stdout=sp.PIPE , universal_newlines=True , check=False ).stdout
	print(res)
	assert 'sub2' in res and 'unlink' in res
	res = sp.run( ('lmake','-n','dut1' ) , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' in res and 'sub1' not in res and 'sub2' in res and 'unlink' in res

	print('pollute',file=open('sub2','w'))
	res = sp.run( ('lmake','-n','dut1') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut1' in res and 'sub1' not in res and 'sub2' in res and 'quarantine' in res

	res = sp.run( ('lmake','-n','dut2') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	assert 'dut2' in res
