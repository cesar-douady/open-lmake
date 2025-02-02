# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys
	import time

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class DepErr(Rule) :
		target = 'dep_err'
		cmd    = 'echo bad ; exit 1'

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'ldepend -e dep_err ; cat dep_err'

else :

	import ut

	ut.lmake('dep_err',failed=1,rc=1)

	print('manual',file=open('dep_err','w'))

	ut.lmake('dut',quarantined=1,may_rerun=1,failed=1,done=1,rc=0)
