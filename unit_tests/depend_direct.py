# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Sub(Rule) :
		target = r'sub{N:\d+}'
		cmd    = ''

	class DutSh(Rule) :
		target = 'dut_sh'
		cmd = 'ldepend -d sub1'

	class DutPy(PyRule) :
		target = 'dut_py'
		def cmd() :
			lmake.depend('sub2',direct=True)

else :

	import ut

	ut.lmake( 'dut_sh' , 'dut_py' , done=4 )
