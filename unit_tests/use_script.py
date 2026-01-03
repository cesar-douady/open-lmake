# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step-0'
	)

	class Step(Rule) :
		use_script = True
		target     = r'step-{Step:\d+}'
		dep        = 'step-{int(Step)-1}'
		cmd        = 'echo {Step}'

	class Test(Rule) :
		target = r'test-{Step:\d+}'
		dep    = 'step-{Step}'
		cmd    = '[ $(cat) = {Step} ]'

else :

	import ut

	print('0',file=open('step-0','w'))

	n = 10

	ut.lmake( f'step-{n}'                         , done=n , new=1 )
	ut.lmake( *(f'test-{i+1}' for i in range(n) ) , done=n         )
