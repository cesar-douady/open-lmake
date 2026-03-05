# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class A(Rule) :
		targets = {
			'A' : 'a'
		,	'X' : r'a-{*:.*}'
		}
		cmd = '>{A}'

	class B(Rule) :
		targets = { 'B' : 'b' }
		deps    = { 'A' : 'a' }
		cmd = '''
			>{B}
			[ -e 'a-1' ] || >a-1
		'''

else :

	import ut

	ut.lmake( 'b' , done=1 , unlink=1 , failed=1 , rc=1 )
