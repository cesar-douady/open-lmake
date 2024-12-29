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
	,	'src1'
	,	'src2'
	)

	class Dep(Rule) :
		target = r'dep{D:\d+}'
		dep    = 'src{D}'
		cmd    = 'sleep 2 ; echo dep_content {D}'

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'cat dep1 dep2'

	class Ref(Rule) :
		target = 'dut.ref'
		cmd    = 'echo dep_content 1 ; echo dep_content 2'

	class Test(Rule) :
		target = r'test{D:\d+}'
		deps   = { 'REF' : 'dut.ref' }
		cmd    = 'sleep 1 ; diff {REF} dut'

else :

	import os

	import ut

	print('src1 v1',file=open('src1','w'))
	print('src2 v1',file=open('src2','w'))

	ut.lmake( 'test1' , done=5 , may_rerun=2 , new=2 ) # dut is made

	os.unlink('dep1')
	os.unlink('dut')
	print('src2 v2',file=open('src2','w'))

	ut.lmake( 'test1' , 'test2' , changed=1 , may_rerun=1 , done=1 , steady=3 ) # dut is not necessary on disk for test1 as dep2 will be steady and test1 will not run, ...
	#                                                                           # ... but 1s later, while it is waiting for dep2, it is necessary for test2
