# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		kill_sigs = (2,)                                        # SIGKILL (9) is automatically added when list is exhausted
		targets = { 'TGT' : ( r'dut.{*:\d}' , 'incremental' ) }
		cmd = '''
			trap 'echo killed > {TGT(2)} ; sleep 2 ; echo killed >{TGT(3)}' 2
			cat dep > {TGT(1)}
			lcheck_deps
			sleep 3
		'''

	class Dep(Rule) :
		target = 'dep'
		cmd    = ''

else :

	import os
	import os.path as osp

	import ut

	ut.lmake( 'dut.1' , rerun=1 , done=2 )

	assert     osp.exists('dut.2') # created when job is killed because of new dep
	assert not osp.exists('dut.3') # should be kill -9'ed before generating 2nd message
