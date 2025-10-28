# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

ref = 'src1+{src2.version}.ref'

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'src1'
	,	'src2'
	,	ref
	)

	from step import step,rule_step

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

	class Version(BaseRule,PyRule) :
		target = '{File}.version'
		dep    = '{File}'
		def cmd() :
			sys.stdout.write(sys.stdin.read())
			print(f'#version{step}')

	class Par(BaseRule) :
		target = '{{{SubExpr}}}'
		dep    = '{SubExpr}'
		cmd    = 'cat'

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

	class Cpy(BaseRule) :
		target = '{File}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

	if rule_step==2 :
		class CpyNew(Cpy) :
			target = '{File}.cpyn'

else :

	import ut

	target='src1+{src2.version}.ok'

	print('step=1;rule_step=1',file=open('step.py','w'))

	print('#src1',file=open('src1','w'))
	print('#src2',file=open('src2','w'))
	open(ref,'w').write( open('src1').read() + open('src2').read() + '#version1\n' )

	ut.lmake( target+'.cpy' , new=3 , done=5 )

	# update source
	print('x',file=open('src2','a'))
	ut.lmake( target+'.cpy' , changed=1 , done=3 , failed=1 , rc=1 )
	open(ref,'w').write( open('src1').read() + open('src2').read() + '#version1\n' ) # update ref
	ut.lmake( target+'.cpy' , changed=1 , done=1 )

	# update cmd
	print('step=2;rule_step=1',file=open('step.py','w'))
	ut.lmake( target , done=3 , failed=1 , rc=1 )
	open(ref,'w').write( open('src1').read() + open('src2').read() + '#version2\n' ) # update ref
	ut.lmake( target , changed=1 , done=1 )

	print('step=3;rule_step=1',file=open('step.py','w'))
	ut.lmake( target , done=3 , failed=1 , rc=1 )
	open(ref,'w').write( open('src1').read() + open('src2').read() + '#version3\n' ) # update ref
	ut.lmake( target , changed=1 , done=1 )

	# update rules
	ut.lmake( target+'.cpyn' , rc=1 )
	print('step=3;rule_step=2',file=open('step.py','w'))
	ut.lmake( target+'.cpyn' , done=1 )
