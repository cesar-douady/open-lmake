# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

import sys

if getattr(sys,'lmake_read_makefiles',False) :

	import lmake

	lmake.sources = ('Lmakefile.py',)

	class A(lmake.Rule):
		targets = { 'A' : 'a/{*:.*}' }
		cmd = 'exit 1'

	class D(lmake.Rule):
		target = 'res'
		cmd    = 'cat a/b/c/d'

else :

	import ut

	ut.lmake( 'res' , may_rerun=1 , failed=1 , was_failed=1 , rc=1 )           # check no loop
