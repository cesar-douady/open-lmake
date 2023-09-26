# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	from step import step

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	)

	class A(lmake.Rule) :
		target = 'a'
		if step==1 : cmd = 'cat b'
		else       : cmd = ''

	class B(lmake.Rule) :
		target = 'b'
		cmd = 'cat a'

	class C(lmake.Rule) :
		target = 'c'
		cmd = 'cat d/x || :'

	class D(lmake.Rule) :
		target = 'd'
		cmd = 'cat c'

else :

	import ut

	if False :                                                                 # XXX : activate when cycles are correctly handled

		print('step=1',file=open('step.py','w'))
		ut.lmake( 'b' , may_rerun=2 ,          rc=1 )
		ut.lmake( 'c' , may_rerun=2 , done=2 , rc=0 )                          # this is not a loop as d being constructible, d/x is not

		print('step=2',file=open('step.py','w'))
		ut.lmake( 'b' , steady=2 , rc=0 )                                      # loop is solved
