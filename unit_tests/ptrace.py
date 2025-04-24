# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		autodep = 'ptrace'
		targets = {
			'DUT' : 'dut'
		,	'LNK' : 'lnk'
		}
		cmd = '''
			echo dut >{DUT}.tmp
			ln {DUT}.tmp {DUT} # no ln with ptrace in other UT's
			rm {DUT}.tmp
			ln -s {DUT} {LNK} # no ln -s with ptrace in other UT's
		'''

	class Chk(Rule) :
		shell  = ('/usr/bin/bash','-e')
		target = 'chk'
		deps = {
			'DUT' : 'dut'
		,	'LNK' : 'lnk'
		}
		cmd = '''
			[ "$(cat {DUT})" = dut ]
			[ "$(cat {LNK})" = dut ]
		'''

else :

	import ut

	ut.lmake( 'chk' , done=2 )
