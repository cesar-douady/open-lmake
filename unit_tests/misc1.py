# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	class Test(Rule) :
		target    = 'test'
		stderr_ok = True
		if step==1 : cmd = "cat a ; echo >a"
		else       : cmd = "cat a ; :      "

else :

	import os

	import ut

	os.environ['LMAKE_ARGS'] = 'test'

	print('step=1',file=open('step.py','w'))
	ut.lmake( failed=1 , new=0 , rc=1 )      # unexpected write to a

	os.unlink('a')
	print('step=2',file=open('step.py','w'))
	ut.lmake( steady=1 , new=0 , rc=0 )      # fixed
