# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class A(Rule) :
		targets = { 'A' : r'a/{*:.*}' }
		cmd     = 'exit 1'

	class E(Rule) :
		targets = { 'E' : ('e','phony') }
		cmd     = ''

	class Ef(Rule) :
		target = 'e/f'
		cmd    = ''

	class D(Rule):
		target = 'res'
		cmd    = 'cat a/b/c/d e/f'

else :

	import ut

	ut.lmake( 'res' , may_rerun=1 , failed=1 , steady=1 , done=1 , was_dep_err=1 , rc=1 )
