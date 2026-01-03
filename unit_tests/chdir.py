# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class NewDep(Rule) :
		target = r'new_dir/dep{Digit:\d}'
		cmd    = 'echo dep{Digit}'

	class Chdir(Rule) :
		target     = r'chdir{Digit:\d}'
		auto_mkdir = step==2
		shell      = Rule.shell + ('-e',)
		cmd = '''
			cd new_dir
			cat dep{Digit}
		'''

else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'chdir1' , failed=1 , rc=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'chdir1' , done=2 , may_rerun=1 )
