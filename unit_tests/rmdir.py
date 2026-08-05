# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	if step==1 :
		class Dep(Rule) :
			target = 'dir/dep'
			cmd    = 'echo x'

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'cat dir/dep'

else :

	import os.path as osp

	import ut

	print( 'step=1' , file=open('step.py','w') )
	ut.lmake( 'dut' , may_rerun=1 , done=2 )

	print( 'step=2' , file=open('step.py','w') )
	ut.lmake( 'dut' , unlinked=1 , failed=1 , rc=1 )

	assert not osp.isdir('dir') # should have been rmdir'ed when dir/dep has been rm'ed
