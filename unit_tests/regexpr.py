# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Dep(Rule) :
		target = 'dep_{File:.*}'
		cmd    = ''

	class DutSh(Rule) :
		target    = 'dut_sh'
		stderr_ok = True
		cmd    = '''
			cat dep_a1
			ldepend --ignore --regexpr 'dep_a.*'
			ltarget --ignore --regexpr 'ignore.*'
			> ignore_sh
			cat dep_a2 || :
			cat dep_b1
		'''

	class DutPy(PyRule) :
		target    = 'dut_py'
		stderr_ok = True
		def cmd() :
			print(open('dep_a3').read())
			lmake.depend(r'dep_a.*' ,ignore=True,regexpr=True)
			lmake.target(r'ignore.*',ignore=True,regexpr=True)
			try    : print(open('dep_a4'),read())
			except : pass
			open('ignore_py','w')
			print(open('dep_b2').read())

else :

	import ut

	ut.lmake( 'dut_sh' , may_rerun=1 , done=3         ) # check dep_a1 & dep_b1 are built, but not dep_a2
	ut.lmake( 'dut_py' , may_rerun=2 , done=3 , new=1 ) # check dep_a3 & dep_b2 are built, but not dep_a4
