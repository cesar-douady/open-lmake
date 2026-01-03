# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Rc(Rule) :
		target = r'{File:[^/]*}.{Rc:\d+}'
		cmd    = 'exit {Rc}'

	class Ko(PyRule) :
		target = r'{File:[^/]*}.ko'
		def cmd() :
			try     : print(open(f'{File}.0').read())
			except  : print(open(f'{File}.1').read())
			finally : print(open(f'{File}.2').read())

else :

	import ut

	ut.lmake( 'test.ko' , new=... , may_rerun=1 , done=1 , failed=2 , was_failed=1 , dep_error=1 , rc=1 )
