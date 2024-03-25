# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class PyEnv(Rule) :
		targets          = { 'DUT' : 'dut' }
		allow_stderr     = True
		max_submit_count = 1
		cmd = '''
			stat {DUT}.tmp
			echo dut > {DUT}.tmp
			mv {DUT}.tmp {DUT}
		'''

else :

	import ut

	ut.lmake('dut',done=1)
