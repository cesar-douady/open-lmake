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
	,	'hello.py'
	,	'src1'
	,	'src2'
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

	class Import(BaseRule,PyRule) :
		target = '{File}.import'
		dep    = '{File}'
		def cmd() :
			import hello
			print(hello.hello(sys.stdin.read()))

else :

	import os
	import textwrap

	import ut

	def gen_hello() :
		print(textwrap.dedent('''
			def hello(x) :
				return f'hello {x}'
		'''[:-1]),file=open('hello.py','w'))

	print('#src1',file=open('src1','w'))
	print('#src2',file=open('src2','w'))
	gen_hello()

	ut.lmake( 'src2.import' , new=2 , done=1 )
	ut.lmake( 'src1.import' , new=1 , done=1 )
	ut.lmake( 'src1.import'                  )

	# just touch, dont actually modify hello.py
	gen_hello()
	ut.lmake( 'src1.import' , steady=1 )

	# update hello.py
	print(file=open('hello.py','a'))
	ut.lmake( 'src1.import' , changed=1 , steady=1 )
	ut.lmake( 'src2.import' ,             steady=1 )
