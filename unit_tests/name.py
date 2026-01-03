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
	,	'test.tgz'
	)

	from step import step

	class Expand(Rule) :
		name = f'expand{step}'
		targets = {
			'TARGET'  : r'{File:.*}.tgzdir/{*:.*}'
		,	'TRIGGER' :  '{File   }.tgzdir.trigger'
		}
		deps = { 'TGZ' : '{File}.tgz' }
		cmd  = 'tar -xvf {TGZ} -C {File}.tgzdir >{TRIGGER}'

else :

	import os

	import ut

	os.makedirs('test',exist_ok=True)
	open('test/testfile.py','w')
	os.system('tar -czf test.tgz test/testfile.py')

	print('step=1',file=open('step.py','w'))
	ut.lmake('test.tgzdir.trigger',new=1,done=1)

	print('step=2',file=open('step.py','w'))
	ut.lmake('test.tgzdir.trigger')
