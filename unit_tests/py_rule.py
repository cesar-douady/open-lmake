# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import time

	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello.py'
	,	'world.py'
	)

	class Import(PyRule) :
		target = r'import.{Autodep:\w+}.{Wait:\d+}'
		def autodep() : return Autodep
		def cmd() :
			import hello
			time.sleep(int(Wait))
			import world
			print(f'{hello.a} {world.b}')

else :

	import ut

	print('a=1',file=open('hello.py','w'))
	print('b=2',file=open('world.py','w'))

	ut.lmake(*(f'import.{ad}.{w}' for ad in lmake.autodeps for w in (1,2,3)),done=3*len(lmake.autodeps),new=2)
