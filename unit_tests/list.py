# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	,	'hello+world_sh.ref'
	,	'hello+world_py.ref'
	)

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}

	class CatSh(Cat) :
		targets = { 'TARGET' : '{File1}+{File2}_sh' }
		cmd    = '''
			cat {FIRST} {SECOND} >/dev/null
			(
			echo deps    : ; ldepend -l
			echo targets : ; ltarget -l
			) >{TARGET}
		'''

	class CatPy(Cat,PyRule) :
		target = '{File1}+{File2}_py'
		def cmd() :
			for fn in (FIRST,SECOND) :
				with open(fn) as f : pass
			print('deps'   ,':', tuple( f for f in lmake.list_deps   () if not f.startswith('.local/') ) )
			print('targets',':',                   lmake.list_targets()                                  )

	class Chk(Rule) :
		target = r'{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT} >&2'

else :

	import textwrap

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))
	print(
		textwrap.dedent('''
			deps :
			hello
			world
			targets :
			hello+world_sh
		''')[1:-1]                          # get rid of initial & final empty lines
	,	file=open('hello+world_sh.ref','w')
	)
	print(
		textwrap.dedent('''
			deps : ('hello', 'world')
			targets : ('hello+world_py',)
		''')[1:-1]                          # .
	,	file=open('hello+world_py.ref','w')
	)

	ut.lmake( 'hello+world_sh.ok' , 'hello+world_py.ok' , done=4 , new=4   )
