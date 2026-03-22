# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :
	import lmake
	from lmake.rules import Rule

if __name__=='Lmakefile' :

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'a/b.py'
	,	'a/c.py'
	)

	if step==1 :
		class Dut(Rule) :
			target = 'dut1'
			cmd    = ''
	else :
		import a.b
		import a.c

	if step in (1,2) :
		class Dut(Rule) :
			target = 'dut2'
			cmd    = ''

elif __name__=='a.b' :

	class Dut(Rule) :
		target = 'dut3'
		cmd    = ''

elif __name__=='a.c' :

	class Dut(Rule) :
		target = 'dut4'
		cmd    = ''

elif __name__=='__main__' :

	import os

	import ut

	os.makedirs('a',exist_ok=True)

	os.symlink('../Lmakefile.py','a/b.py')
	os.symlink('../Lmakefile.py','a/c.py')

	print('step=1',file=open('step.py','w'))
	ut.lmake(rc=8) # conflict on rule Dut

	print('step=2',file=open('step.py','w'))
	ut.lmake()
	x = '\n'+open('LMAKE/rules').read()
	assert '\nLmakefile.Dut :\n' in x , 'missing Lmakefile.Dut'
	assert '\na.b.Dut :\n'       in x , 'missing a.b.Dut'
	assert '\na.c.Dut :\n'       in x , 'missing a.c.Dut'

	print('step=3',file=open('step.py','w'))
	ut.lmake()
	x = '\n'+open('LMAKE/rules').read()
	assert '\nb.Dut :\n' in x , 'missing a.b.Dut'
	assert '\nc.Dut :\n' in x , 'missing a.c.Dut'

else :
	assert False , f'Lmakefile imported une unexpected name {__name__}'
