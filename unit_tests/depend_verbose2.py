# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep0'
	,	'step.py'
	)

	from step import step

	class DeepDep(Rule):
		target = r'deep_dep_{S:\d+}'
		cmd    = 'sleep {S}'

	class Dep(Rule):
		target = r'dep_{S:\d+}'
		deps = {
			'SRC'      : f'dep{step}'
		,	'DEEP_DEP' : 'deep_dep_{S}'
		}
		cmd = 'echo hello'

	class Dut(PyRule) :
		target  = r'dut{R:[01]}_{S:\d+}'
		def cmd():
			import time
			from lmake import depend
			time.sleep(1)                                                                        # ensure dep is being built if launched in //
			status = depend(f'dep_{S}',verbose=True,read=True,ignore_error=True,required=int(R))
			if not status[f'dep_{S}']['ok'] : raise RuntimeError(f"bad status : {status[f'dep_{S}']['ok']}")
			if status[f'dep_{S}']['checksum']!='none' :
				open(f'dep_{S}')
			print(status)

else :

	import ut
	open('dep0','w').close()
	print('step=0',file=open('step.py','w'))
	ut.lmake( 'dut0_1' , 'dut1_1' , new=1 , done=4 , may_rerun=2 )
	ut.lmake( 'dut1_2' , 'dep_2'  ,         done=3 , may_rerun=1 ) # dep is being built while depend verbose fires

	import os
	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut0_0' , done=1             )
	ut.lmake( 'dut1_0' , dep_error=1 , rc=1 ) # dut1 cannot be built with a non-buildable required dep
