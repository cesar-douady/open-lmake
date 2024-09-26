# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'no_dep.1'
	,	'no_dep.2'
	)

	class Test(PyRule) :
		targets = { 'DST' : r'{File:.*}.test' }
		if step==1 :
			side_targets = {
				'UNIQ' : 'uniq'
			,	'SIDE' : r'side.{*:[12]}'
			}
			side_deps = { 'NO_DEP' : ( r'no_dep.{*:[12]}' , 'Ignore' ) }
		allow_stderr = True
		def cmd() :
			open('no_dep.1')
			open('no_dep.2')
			if step==1 :
				open('uniq'  ,'w')
				open('side.1','w')
				open('side.2','w')
			open(DST,'w')

else :

	import os

	import ut

	print(file=open('no_dep.1','w'))
	print(file=open('no_dep.2','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'foo.test' , done=1 )
	os.unlink('foo.test')
	ut.lmake( 'foo.test' , steady=1 )

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'foo.test' , steady=1 , new=2 )

	print('v2',file=open('no_dep.1','w'))
	ut.lmake( 'foo.test' , steady=1 , changed=1 )
