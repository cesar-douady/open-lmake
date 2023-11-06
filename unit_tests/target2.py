# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if __name__!='__main__' :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	,	'a_source'
	)

	from step import step

	class A(lmake.PyRule):
		target = 'src_name'
		if step==1 :
			def cmd():
				print('a_source_with_typo')
		elif step==2 :
			def cmd():
				print('a_source')

	class Toto(lmake.PyRule):
		target = 'tgt'
		deps = {
			'SRC_NAME': 'src_name'
		}
		def cmd():
			src = open(SRC_NAME).read().strip()
			lmake.target( src , source_ok=True , write=True )
			open('a_source','w').write('something')

else :

	import ut

	print('hello',file=open('a_source','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'tgt' , done=1 , failed=1 , rc=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'tgt' , done=1 , steady=1 )

