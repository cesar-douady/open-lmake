# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep'
	,	'ref'
	)

	class Dut(Rule) :
		target    = 'dut'
		stderr_ok = True
		cmd       = 'cat dep/sub ; cat dep'

	class Test(Rule) :
		target = 'test'
		deps = {
			'DUT' : 'dut'
		,	'REF' : 'ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import ut

	print('good',file=open('ref','w'))

	print('bad',file=open('dep','w'))
	ut.lmake( 'dut' , done=1 , new=1 )

	print('good',file=open('dep','w'))
	ut.lmake( 'test' , done=2 , changed=1 , new=1 ) # check dut is rerun, i.e. that depending on dep/sub does not suppress dep as a dependency
