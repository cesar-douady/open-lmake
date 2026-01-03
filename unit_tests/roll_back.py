# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	from step import step

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	('a' if step==1 else 'b')
	)

	class DutCmd(Rule) :
		target = 'cmd_{File:.*}'
		cmd    = 'echo {File} {step}'

	class DutRule(Rule) :
		if step==1 : target = 'rule_{File:.*}'
		else       : target = 'rule_{File:.+}'
		cmd = 'echo {File} {step}'

	for d in ('a','b') :
		class DutDep(Rule) :
			name   = f'DutDep {d}'
			target = 'dep_{File:.*}'
			deps   = {'DEP':d}
			cmd    = 'echo {File} {DEP}'

else :

	import ut

	print(         file=open('a'      ,'w'))
	print(         file=open('b'      ,'w'))
	print('step=1',file=open('step.py','w'))
	ut.lmake( 'cmd_light' , 'cmd_heavy' , 'rule_light' , 'rule_heavy' , 'dep_light' , 'dep_heavy' , done=6 , new=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'cmd_light' , 'rule_light' , 'dep_light' , done=3 , new=1 )

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'cmd_light' , 'cmd_heavy' , 'rule_light' , 'rule_heavy' , 'dep_light' , 'dep_heavy' , done=3 , new=1 ) # check light are rebuilt and heavy are not
