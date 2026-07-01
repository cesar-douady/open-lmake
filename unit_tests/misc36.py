# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Gen(Rule) :
		targets = {
			'BUILT' : 'built'
		}
		cmd = '''
			ltarget --source-ok src
			echo v1 > src
			echo v1 > {BUILT}
		'''

	class Cp2(Rule) :
		target = 'built2'
		dep    = 'built'
		cmd    = 'cat'

	class Cp3(Rule) :
		target = 'built3'
		cmd    = 'cat built2'

	class Chk(Rule) :
		target = 'chk'
		deps = {
			'SRC'   : 'src'
		,	'BUILT' : 'built'
		}
		cmd = 'diff {SRC} {BUILT}'

	class Dut(Rule) :
		target = 'dut'
		deps = {
			'CHK'   : 'chk'
		,	'BUILT' : 'built3'
		}
		cmd = ''

else :

	import ut

	print('v1',file=open('src','w'))
	ut.lmake( 'dut' , new=1 , may_rerun=1 , done=5 )
