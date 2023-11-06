# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if __name__!='__main__' :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'ref'
	)

	class Hello(lmake.Rule) :
		target = 'hello.py'
		cmd    = 'echo hello=$(cat ref)'

	class Test(lmake.DynamicPyRule) :
		target       = 'test'
		allow_stderr = True
		def cmd():
			import hello
			print(open('hello.py').read(),file=sys.stderr)
			print(hello.hello,file=sys.stderr)
			print(hello.hello)

	class Chk(lmake.Rule) :
		target = 'chk'
		deps = {
			'TEST' : 'test'
		,	'REF'  : 'ref'
		}
		cmd = 'diff {REF} {TEST}'

else :

	import time

	import ut

	print('1',file=open('ref','w'))
	ut.lmake( 'chk' , done=3 , may_rerun=1 , new=2 )                           # Python reads Lmakefile.py to display backtrace
	ut.lmake( 'chk' , done=0                       )

	time.sleep(1)                                                              # Python .pyc validation is only sensitive to seconds ! so to ensure .py are seen as different, we must wait 1s.
	print('2',file=open('ref','w'))
	ut.lmake( 'chk' , done=2 , changed=1 , steady=1 )
