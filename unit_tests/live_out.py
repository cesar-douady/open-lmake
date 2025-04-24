# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		targets = { 'DUT':r'dut.{N:\d+}' }
		cmd = '''
			sleep 1
			echo before
			sleep 1
			echo after
			sleep 1
			> {DUT}
		'''

else :

	import ut

	cnt = ut.lmake( '-o' , 'dut.1' , 'dut.2' , done=2 , **{'continue':...} )
	assert cnt['continue'] in (1,2)
