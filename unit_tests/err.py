# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Bad(Rule) :
		target = 'bad'
		cmd    = 'exit 1'

	class Double(Rule) :
		targets = {
			'DOUBLE1' : 'double1'
		,	'DOUBLE2' : 'double2'
		}
		deps = { 'BAD' : 'bad' }
		cmd = 'cat bad >{DOUBLE1} ; cp {DOUBLE1} {DOUBLE2}'

	class Dut(Rule) :
		target = 'dut'
		cmd = 'cat double1 double2'

else :

	import ut

	ut.lmake( 'bad' , failed=1    , rc=1 )
	ut.lmake( 'dut' , dep_error=1 , rc=1 ) # check Double is not run, although dut depends both on double1 and double2
