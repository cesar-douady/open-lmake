# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,)

	class Star(lmake.PyRule):
		targets = { 'DST' : r'out{Wait:\d}/{__file__*}' }
		def cmd():
			import time
			import os
			time.sleep(int(Wait))
			dir = f'out{Wait}'
			try                      : os.unlink(f'{dir}/mrkr0')
			except FileNotFoundError : pass
			try                      : os.unlink(f'{dir}/mrkr1')
			except FileNotFoundError : pass
			open(f'{dir}/a_file','w').write('hello')

	class Mrkr(lmake.Rule):
		targets = { 'MRKR' : r'{__dir__}mrkr{Wait:\d}' }
		cmd     = 'sleep $Wait ; echo > $MRKR'                                 # just create output

	class Res1(lmake.PyRule):
		target = 'res1'
		def cmd():
			lmake.depend('out1/mrkr0')
			print(open('out1/a_file').read())

	class Res2(lmake.PyRule):
		target = 'res2'
		def cmd():
			lmake.depend('out0/mrkr1')
			print(open('out0/a_file').read())

else :

	import ut

	ut.lmake( 'res1' , done=2 , may_rerun=1 , rerun=1 , steady=2 )
	ut.lmake( 'res2' , done=3 , may_rerun=1 )
