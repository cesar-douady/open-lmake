# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'ref'
	)


	class Dep(Rule) :
		targets = { 'DEP' : 'dep' }
		cmd    = 'echo line1 > {DEP} ; sleep 2 ; echo line2 >> {DEP}'

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'ldepend dep ; sleep 1 ; cat dep ; sleep 2 ; cat dep' # dep is required, done, and moving while dut is running

	class Chk(Rule) :
		target = 'chk'
		deps   = {
			'DUT' : 'dut'
		,	'REF' : 'ref'
		}
		cmd    = 'diff {REF} {DUT}'

else :

	import ut

	print('line1\nline2\nline1\nline2',file=open('ref','w'))

	ut.lmake( 'dep' , 'chk' , new=1 , rerun=1 , done=3 ) # build dep so that it appear during dut run
