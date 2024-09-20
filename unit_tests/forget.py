
# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep'
	)

	class Dut(Rule) :
		target = 'dut'
		deps   = {'DEP':'dep'}
		cmd    = 'cat no_file 2>/dev/null ; cat {DEP}'

else :

	import os

	import ut

	open('dep','w')

	ut.lmake( 'dut' , done=1 , new=1 )

	assert os.system('lforget -d dut')==0

	ut.lmake( 'dut' , steady=1 )
