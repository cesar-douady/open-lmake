# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import subprocess as sp

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep'
	,	'sub/mrkr'
	)

	class Bad(Rule) :
		target = 'sub/dep'
		cmd    = 'exit 1'

	class Dut(PyRule) :
		target = 'dut'
		def cmd() :
			sp.run(('hostname',),cwd='sub')
			open('dep')                     # create a dep to ensure it is not seen in the sub directory after vfork with chdir

else :

	import os
	import os.path as osp

	import ut

	os.makedirs('sub',exist_ok=True)
	open('sub/mrkr','w')
	open('dep'     ,'w')

	ut.lmake( 'dut' , done=1 , new=1 ) # check target is written at the top level despites being after a vfork that does a cd in the child
