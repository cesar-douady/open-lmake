# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
			'Lmakefile.py'
		,	'lst'
		)

	class GenFile(Rule) :
		target = r'dep{Digit:\d+}'
		cmd    = ''

	class All(PyRule) :
		target = 'dut'
		deps = { 'LST' : ('lst','Critical') }
		def cmd() :
			from lmake import depend
			for t in open(LST).readlines() :
				depend(t.strip())

else :

	import ut

	print('dep1',file=open('lst','w')) ; ut.lmake( 'dut' , new    =1 , may_rerun=1 , done=1 , steady=1 )
	print('dep2',file=open('lst','a')) ; ut.lmake( 'dut' , changed=1 , may_rerun=1 , done=1 , steady=1 )
