# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

ref = 'src1+{auto1.hide}.ref'
dut = 'src1+{auto1.hide}.ok'

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src1'
	,	ref
	)

	def balanced(n) :
		if not n : return r'[^{}]*'
		p = balanced(n-1)
		return fr'{p}(\{{{p}\}}{p})*'
	class BaseRule(Rule) :
		stems = {
			'File'    : r'.*'
		,	'SubExpr' : balanced(0)
		,	'Expr'    : balanced(1)
		,	'Digit'   : r'\d'
		}
		stems['Expr1'] = stems['Expr']
		stems['Expr2'] = stems['Expr']
		shell = Rule.shell + ('-e',)

	class Auto(BaseRule) :
		target = 'auto{Digit}'
		cmd    = "echo '#auto'{Digit}"

	class Cat(BaseRule) :
		target = '{Expr1}+{Expr2}'
		deps = {
			'FIRST'  : '{Expr1}'
		,	'SECOND' : '{Expr2}'
		}
		cmd = 'cat {FIRST} {SECOND}'

	class Cmp(BaseRule) :
		target = '{File}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT}'

	class Hide(BaseRule) :
		target    = '{File}.hide'
		stderr_ok = True
		cmd       = 'cat {File} || :'

	class Par(BaseRule) :
		target = '{{{SubExpr}}}'
		dep    = '{SubExpr}'
		cmd    = 'cat'

else :

	import ut

	print('#src1'        ,file=open('src1','w'))
	print('#src1\n#auto1',file=open(ref   ,'w'))

	ut.lmake( dut , new=2 , may_rerun=1 , done=5 )
