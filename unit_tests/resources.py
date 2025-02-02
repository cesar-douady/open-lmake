# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

n_good = 20

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.backends.local.gnat = 4

	class Test(Rule) :
		cmd = 'echo {gnat}'
	class Test1(Test) :
		target    = r'test1.{Tokens:\d}'
		resources = { 'gnat': '1' }

	class Test2(Test) :
		target    = r'test2.{Tokens:\d}'
		resources = { 'gnat': '{Tokens}' }

	def compute_gnat() :
		return f'{Tokens}'
	class Test3(Test) :
		target    = r'test3.{Tokens:\d}'
		resources = { 'gnat': compute_gnat }

	class Test4(Rule) :
		target = r'test4.{Tokens:\d}'
		def resources() : return { 'gnat' : f'{Tokens}' }
		def cmd      () : print(resources['gnat'])

else :

	import ut

	ut.lmake( 'test1.2' , 'test1.3' , done=2 )
	assert int(open('test1.2').read())==1
	assert int(open('test1.3').read())==1

	for x in (2,3,4) :
		ut.lmake( f'test{x}.2' , f'test{x}.3' , done=2 )
		assert int(open(f'test{x}.2').read())+int(open(f'test{x}.3').read())==5
