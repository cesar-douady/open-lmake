# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if __name__!='__main__' :

	import lmake

	from step import step

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	)

	class Test(lmake.Rule) :
		target       = 'test'
		allow_stderr = True
		if step==1 : cmd = "cat a ; echo >a"
		else       : cmd = "cat a ; :      "

else :

	import os

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'test' , failed=1 , new=0 , rc=1 )                               # unexpected write to a

	os.unlink('a')
	print('step=2',file=open('step.py','w'))
	ut.lmake( 'test' , steady=1 , new=0 , rc=0 )                               # fixed
