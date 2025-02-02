# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import os.path as osp

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Py2Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	,	'a_no_dep'
	,	'a_dep'
	)

	class Cat(Py2Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		python = (lmake.user_environ['PYTHON2'],)
		def cmd() :
			lmake.depend('a_dep'   ,read=True)
			lmake.depend('a_no_dep'          )
			sys.stdout.write(open(FIRST ).read())
			sys.stdout.write(open(SECOND).read())

else :

	if not os.environ['PYTHON2'] :
		print('python2 not available',file=open('skipped','w'))
		exit()

	import ut

	open('hello'   ,'w').write('hello\n')
	open('world'   ,'w').write('world\n')
	open('a_no_dep','w').write('1\n'    )
	open('a_dep'   ,'w').write('1\n'    )

	ut.lmake( 'hello+world' , done=1   , new    =4 ) # check targets are out of date
	open('a_dep','w').write('2\n')
	ut.lmake( 'hello+world' , changed=1 , steady=1 ) # check target is sensitive to a_dep
	open('a_no_dep','w').write('2\n')
	ut.lmake( 'hello+world' , changed=1            ) # check target is not sensitive to a_no_dep
	ut.lmake( 'hello+world'                        ) # check targets are up to date
	ut.lmake( 'world+world' , done=1               ) # check reconvergence
