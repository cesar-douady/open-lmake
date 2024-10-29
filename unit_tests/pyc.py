# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'mod.py'
	)

	class Dut(Rule) :
		target ='dut'
		python = (sys.executable,'-B')
		def cmd():
			sys.path.append('.') # ensure mod can be founded even if executed with use_script=True
			import mod

else :

	import os
	import ut

	os.environ['LMAKE_ARGS'] = 'dut'

	os.makedirs('mod',exist_ok=True)
	open('mod.py','w')
	ut.lmake(  done=1 , new=1 , rc=0 )

	os.makedirs('__pycache__',exist_ok=True)
	open(f'__pycache__/mod.cpython-{sys.version_info.major}{sys.version_info.minor}.pyc','w')
	os.unlink('dut')

	ut.lmake(        dangling=1 , failed=0  , new=0 , rc=1 )
	ut.lmake( '-e' , dangling=1 , dep_err=1 , new=0 , rc=1 )
