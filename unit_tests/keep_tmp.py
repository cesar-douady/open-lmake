# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule,AliasRule

	lmake.manifest       = ('Lmakefile.py',)

	class Tmp(Rule) :
		target = 'dut'
		cmd    = 'echo tmp >$TMPDIR/tmp'

else :

	import subprocess as sp

	import ut

	ut.lmake( '-t' , 'dut' , done=1 )
	info = eval(sp.check_output(('lshow','-ip','dut'),universal_newlines=True))['dut']
	assert open(f"{info['tmp dir']}/tmp").read().strip()=='tmp'
