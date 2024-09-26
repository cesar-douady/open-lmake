# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class X(PyRule) :
		targets = { 'X' : r'{Key:[12]}{*:}' }
		def cmd() :
			if int(Key)!=step : raise ValueError('bad Key')
			open(Key,'w')

	class Test(PyRule) :
		target = 'test'
		def cmd() :
			open(f'{step}')

else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake('test',new=1,may_rerun=1,done=1,steady=1)

	print('step=2',file=open('step.py','w'))
	ut.lmake('test',failed=1,may_rerun=1,done=1,steady=1)
