# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	from step import step

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	,	'side.1'
	,	'side.2'
	)

	class Test(lmake.PyRule) :
		targets = { 'DST'  : 'test' }
		if step==1 :
			targets['SIDE'] = ( 'side.{*:[12]}' , '-Dep','Incremental','-Match','-Write' )
		allow_stderr = True
		def cmd() :
			open('side.1')
			open('side.2')
			open(DST,'w')

else :

	import os

	import ut

	print(file=open('side.1','w'))
	print(file=open('side.2','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'test' , done=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'test' , steady=1 , new=2 )

	print('v2',file=open('side.1','w'))
	ut.lmake( 'test' , steady=1 , new=1 )
