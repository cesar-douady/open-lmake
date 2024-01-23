# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	from step import step

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	class A(Rule) :
		target = 'a'
		if step==1 : cmd = 'cat b/c'
		else       : cmd = ''

	class B(Rule) :
		target = 'b'
		cmd = 'cat a'

else :

	import subprocess as sp

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'b' , may_rerun=2 ,          rc=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'b' , steady=0 , rc=1 )                                          # loop is solved, but lmake cant cope with the situation
	sp.run(('lforget','-d','a'),check=True)                                    # follow recommandation
	ut.lmake( 'b' , steady=2 , rc=0 )                                          # loop is solved
