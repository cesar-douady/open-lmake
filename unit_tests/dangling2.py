# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Hide(Rule) :
		target    = r'{File:.*}.hide'
		stderr_ok = True
		cmd       = 'cat {File} || :'

else :

	import os

	import ut

	ut.lmake( 'src3.hide' , done=1 )

	open('src3','w')
	os.unlink('src3.hide')
	ut.lmake( 'src3.hide' , dangling=1 , rc=1 )

	os.unlink('src3')
	ut.lmake( 'src3.hide' , steady=1 )
	ut.lmake( 'src3.hide'            )

	os.system('''
		lshow   -dv src3.hide
		lforget -d  src3.hide
		lshow   -dv src3.hide
	''')
	ut.lmake( 'src3.hide' , steady=1 )
