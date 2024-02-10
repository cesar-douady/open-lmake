# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	)

	class Cat(Rule) :
		targets      = { 'DST'     :  'hello.cpy'                            }
		target_flags = { 'SCRATCH' : ('hello'    ,'incremental','source_ok') }
		cmd = '''
			echo 2nd line >> hello
			cat hello > {DST}
		'''

else :

	import ut

	print('hello',file=open('hello','w'))

	ut.lmake( 'hello.cpy' , done=1 ) # check no dependency on hello
