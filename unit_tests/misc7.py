# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import HomelessRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class Test(HomelessRule) :
		targets = { 'OUT' : 'out' }
		def cmd() :
			import lmake
			if step==1 : lmake.target('side')
			else       : lmake.target('side',incremental=True)
			open('side','w').close()
			print('toto')

else :

	import os.path as osp

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake('out',failed=1,rc=1)
	assert osp.exists('side')

	print('step=2',file=open('step.py','w'))
	ut.lmake('out',failed=1,rc=1)
	assert osp.exists('side')
