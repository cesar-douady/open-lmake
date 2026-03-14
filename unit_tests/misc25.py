# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	class Hi(Rule) :
		prio = 1
		if step==1 : targets = { 'T' :  r'a{D*:.*}'          }
		else       : targets = { 'T' : (r'a{D*:.*}','phony') }
		cmd = ''


	class Low(Rule):
		target = 'a'
		cmd    = 'echo low'


	class Test(Rule) :
		target = 'test'
		dep    = 'a'
		cmd    = 'cat'

else :

	import ut

	print( 'step=1' , file=open('step.py','w') )
	ut.lmake( 'test' , steady=1 , done=2 )

	print( 'step=2' , file=open('step.py','w') )
	ut.lmake( 'test' , steady=1 , unlinked=1 , failed=1 , rc=1 )
