# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__=='Lmakefile' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'a'
	)

	class Dut(Rule) :
		target = 'dut'
		dep    = 'a'
		cmd    = 'cat'

	class Test(Rule) :
		target = 'test_{V:.*}'
		dep    = 'dut'
		cmd    = '[ $(cat) = {V} ]'

elif __name__=='Lmakefile.config' :

	pass

elif __name__=='Lmakefile.rules' :

	from lmake.rules import Rule

	class Dut(Rule) :
		target = 'dut'
		dep    = 'b'
		cmd    = 'cat'

	class Test(Rule) :
		target = 'test_{V:.*}'
		dep    = 'dut'
		cmd    = '[ $(cat) = {V} ]'

elif __name__=='Lmakefile.sources' :

	import lmake

	lmake.manifest = (
		'Lmakefile/__init__.py'
	,	'Lmakefile/config.py'
	,	'Lmakefile/rules.py'
	,	'Lmakefile/sources.py'
	,	'b'
	)

elif __name__=='__main__' :

	import os
	import shutil

	import ut

	def with_modules(yes) :
		try                      : os.unlink('Lmakefile.py')
		except FileNotFoundError : pass
		shutil.rmtree('Lmakefile',ignore_errors=True)
		if yes :
			os.makedirs('Lmakefile',exist_ok=True)
			open('Lmakefile/__init__.py','w')
			open('Lmakefile/config.py'  ,'w').write(makefile)
			open('Lmakefile/rules.py'   ,'w').write(makefile)
			open('Lmakefile/sources.py' ,'w').write(makefile)
		else :
			open('Lmakefile.py','w').write(makefile)

	makefile = open('Lmakefile.py').read()
	print('a',file=open('a','w'))
	print('b',file=open('b','w'))

	with_modules(False)
	ut.lmake('test_a',new=1,done=2)

	with_modules(True)
	ut.lmake('test_b',new=1,done=2)

	with_modules(False)
	ut.lmake('test_a',new=1,done=1) # test_a is not remade

else :

	assert False,f'bad __name__ {__name__}'
