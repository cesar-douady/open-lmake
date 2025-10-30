# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Dep(Rule) :
		target = 'dep'
		dep    = 'src'
		cmd    = ''

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'ldepend -vR dep'

else :

	import ut

	print('ok',file=open('src','w'))

	ut.lmake( 'dep' , new=1 , done=1 )

	print('ok2',file=open('src','w'))

	ut.lmake( 'dut' , changed=1 , rerun=1 , steady=1 , done=1 )
