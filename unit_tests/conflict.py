# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,)

	class Star(PyRule):
		targets = { 'DST' : r'out{Wait:\d}/{*:.*}' }
		def cmd():
			import time
			import os
			time.sleep(int(Wait))
			dir = f'out{Wait}'
			try                      : os.unlink(DST('mrkr0'))
			except FileNotFoundError : pass
			try                      : os.unlink(DST('mrkr1'))
			except FileNotFoundError : pass
			open(DST('a_file'),'w').write('hello')

	class Mrkr(Rule):
		targets = { 'MRKR' : r'{:(.*/)?}mrkr{Wait:\d}' } # cannot use target as we want to wait before creating MRKR
		cmd     = 'sleep {Wait} ; echo > {MRKR}'         # just create output

	class Res1(PyRule):
		target = 'res1'
		def cmd():
			lmake.depend('out1/mrkr0')
			print(open('out1/a_file').read())

	class Res2(PyRule):
		target = 'res2'
		def cmd():
			lmake.depend('out0/mrkr1')
			print(open('out0/a_file').read())

else :

	import ut

	ut.lmake( 'res1' , new=1 , done=3 , may_rerun=1 , steady=1 ) # check case where mrkr has been unlinked after having been created
	ut.lmake( 'res1'                                           ) # ensure up to date
	ut.lmake( 'res2' ,         done=3 , may_rerun=1            ) # check case where mrkr has been unlinked before having been created
	ut.lmake( 'res2'                                           ) # ensure up to date
