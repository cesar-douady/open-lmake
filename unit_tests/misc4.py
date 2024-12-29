# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Cat(Rule) :
		targets = { 'CAT' : 'bin/cat' }
		cmd     = 'ln -s /usr/bin/cat {CAT}'

	class Reg(Rule) :
		target = r'x{:\d}'
		cmd    = ''

	class Dut1(Rule) :
		target = 'dut1'
		deps   = { 'X1' : 'x1' }
		cmd    = 'readlink x1 ; readlink x2'

	class Dut2(Rule) :
		target = 'dut2'
		cmd    = 'bin/cat x3'

	class Dut3(Rule) :
		target = 'dut3'
		def cmd() :
			import os
			os.lstat('bin/cat')

else :

	import ut

	ut.lmake('dut1',may_rerun=1,done=2,was_failed=1,rc=1)
	ut.lmake('dut2',may_rerun=2,done=3                  )
	ut.lmake('dut3',may_rerun=0,done=1                  )
