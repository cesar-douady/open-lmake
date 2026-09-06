# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	from enum import IntEnum
	class Cpu(IntEnum) :
		QUAD = 4

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.backends.local.cpu = Cpu.QUAD   # an IntEnum member is a genuine int (==4) and must not corrupt the generated config

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'echo hello'

else :

	import ut

	ut.lmake() # check no crash
