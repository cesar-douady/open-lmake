# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep1'
	,	'dep2'
	)

	class Multi1(Rule) :
		target = 'dut'
		dep    = 'dep1'
		cmd    = 'cat'

	class Multi2(Rule) :
		target = 'dut'
		dep    = 'dep2'
		cmd    = 'cat'

else :

	import ut

	print('dep1',file=open('dep1','w'))
	print('dep2',file=open('dep2','w'))

	ut.lmake( 'dut' , multi=1 , rc=1 )
