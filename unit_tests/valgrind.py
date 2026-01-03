# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

valgrind = '/usr/bin/valgrind'

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Dut1(Rule) :
		autodep = 'ptrace'      # XXX! : generates a free(): invalid pointer with ld_audit
		targets = {'LOG':'log'}
		cmd     = '{valgrind} --log-file={LOG} --tool=memcheck hostname'

	class Dut2(Rule) :
		max_submits = 2
		autodep     = 'ptrace'                      # valgrind does not go through libc to write its output
		targets     = { 'LOGS' : r'logdir/{*:.*}' }
		cmd = '''
			{valgrind} --log-file={LOGS('log')} --tool=memcheck hostname
		'''

else :

	import os
	import os.path as osp

	if 'ptrace' not in lmake.autodeps :
		print('ptrace not available',file=open('skipped','w'))
		exit()

	if not osp.exists(valgrind) :
		print('valgrind not available',file=open('skipped','w'))
		exit()

	import ut

	ut.lmake( 'log'        , done=1 )
	ut.lmake( 'logdir/log' , done=1 )
