# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import sys
import time

import lmake

if getattr(sys,'reading_makefiles',False) :

	lmake.sources = (
		'Lmakefile.py'
	,	'hello'
	,	'side.py'
	,	'side.sh'
	)

	class Base(lmake.Rule) :
		stems        = {
			'File'  : r'.*'
		,	'YesNo' : r'[yn]'
		}
		dep          = '{File}'
		autodep      = 'none'

	class ShCpy(Base) :
		targets = { 'DST' : '{File}.sh.{YesNo}.cpy' }
		cmd     = '[ $YesNo = y ] && ltarget $DST ; cat>$DST ; ltarget -w -s side.sh ; echo side > side.sh'

	class PyCpy(Base) :
		targets = { 'DST' : '{File}.py.{YesNo}.cpy' }
		def cmd() :
			if YesNo=='y' : lmake.target(DST)
			open(DST,'w').write(sys.stdin.read())
			lmake.target('side.py',write=True,source_ok=True)
			print('side',file=open('side.py','w'))

else :

	import ut

	print(f'hello'   ,file=open('hello'  ,'w'))
	print(f'not side',file=open('side.py','w'))
	print(f'not side',file=open('side.sh','w'))

	ut.lmake( 'hello.sh.y.cpy' , 'hello.py.y.cpy' , done  =2 , new=1        )
	ut.lmake( 'hello.sh.n.cpy' , 'hello.py.n.cpy' , failed=2 , new=0 , rc=1 )

	assert open('side.py').read()=='side\n'
	assert open('side.sh').read()=='side\n'
