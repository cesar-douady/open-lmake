# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		target = r'x{X:.*}{D:\d}'
		deps   = { f'DEP_{d}':f'{{X}}{d}' for d in range(10) }
		cmd    = ''

	class Match(Rule) :
		target = r'_ok{D:\d}'
		cmd    = ''

else :

	import ut

	ut.lmake( 'xxxxxxxxxx_ok0' , done=101 ) # check no combinatorial explosion
	ut.lmake( 'xxxxxxxxxx_ko0' , rc=1     ) # .
