# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Dep(PyRule) :
		targets = { 'OUT' : 'deps/{*:.*}' }
		cmd     = ''

	class Dut(Rule):
		target = 'dut'
		cmd    = 'cat deps/dangling'

else :

	import os

	import ut

	ut.lmake( 'deps/dangling' , steady=1 , rc=1 )

	os.makedirs('deps',exist_ok=True)
	open('deps/dangling','w')

	ut.lmake( 'dut' , dangling=1 , dep_err=1 , rc=1 )
