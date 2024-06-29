# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,TraceRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'sub_script'
	)

	class Dut(TraceRule) :
		targets  = { 'DUT' : 'dut'        }
		deps     = { 'EXE' : 'sub_script' }
		tmp_view = '/tmp'
		cmd = '''
			./{EXE} > $TMPDIR/tmp
			cat $TMPDIR/tmp >{DUT} # check tracing does not prevent redirection of stdout
		'''

	class Test(Rule) :
		target = 'test'
		dep    = 'dut'
		cmd = '''
			./sub_script >$TMPDIR/1
			cat          >$TMPDIR/2
			diff $TMPDIR/1 $TMPDIR/2 >&2
		'''

else :

	import os
	import subprocess as sp

	from lmake.utils import multi_strip

	import ut

	print('echo hello',file=open('sub_script','w'))
	os.chmod('sub_script',0o755)

	ut.lmake( 'test' , done=2 , new=1 )

	log = sp.check_output(('lshow','-o','dut'),universal_newlines=True)
	ref = multi_strip('''
		Dut dut
		+ ./sub_script
		+ cat /tmp/tmp
	''')
	assert log==ref,f'log={log} ref={ref}'
