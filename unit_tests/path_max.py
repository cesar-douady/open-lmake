# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import path_max
	lmake.config.path_max = path_max

	class Test(Rule) :
		target = '12345'
		cmd    = ''

else :

	import os

	import ut

	print('path_max=4',file=open('step.py','w')) ; ut.lmake( '12345' ,          name=1 ,            rc=1 )
	print('path_max=5',file=open('step.py','w')) ; ut.lmake( '12345' , done=1                            )
	print('path_max=4',file=open('step.py','w')) ; ut.lmake( '12345' ,          name=1 , unlink=1 , rc=1 )
	print('path_max=5',file=open('step.py','w')) ; ut.lmake( '12345' , done=0                            )
