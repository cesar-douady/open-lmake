# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Dut1(Rule) :
		autodep = 'ptrace'      # XXX : generates a free(): invalid pointer with ld_audit
		targets = {'LOG':'log'}
		cmd     = 'valgrind --log-file={LOG} --tool=memcheck hostname'

	class Dut2(Rule) :
		max_submit_count = 2
		autodep          = 'ptrace'                     # valgrind does not go through libc to write its output
		targets          = { 'LOGS' : 'logdir/{*:.*}' }
		cmd = '''
			valgrind --log-file={LOGS('log')} --tool=memcheck hostname
		'''

else :

	import shutil
	if not shutil.which('valgrind') :
		print('valgrind not available',file=open('skipped','w'))
		exit()

	import os
	import os.path as osp

	import ut

	ut.lmake( 'log'        , done=1 )
	ut.lmake( 'logdir/log' , done=1 )
