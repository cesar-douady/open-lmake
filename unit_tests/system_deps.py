# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule,AliasRule

	from step import step

	if True    : lmake.manifest       = ('Lmakefile.py','step.py')
	if step==2 : lmake.extra_manifest = ('/',)

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'cat /etc/os-release'

else :

	import os
	import subprocess as sp

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , done=1 )
	x = eval(sp.check_output(('lshow','-dp','dut'),universal_newlines=True))
	assert x=={'dut':{('Dut','dut','generating'):()}},x

	os.rename('LMAKE','LMAKE2') # must clean up repo to change source dirs

	print('step=2',file=open('step.py','w'))
	cnt = ut.lmake( 'dut' , quarantined=1 , new=... , done=1 )
	assert cnt.new>=1 , 'missing at least new /etc/os-release'
	x1 = eval(sp.check_output(('lshow','-dp','dut'),universal_newlines=True))
	for k2,v2 in x1['dut'].items() :                                          # for each job run to provide target
		assert k2==('Dut','dut','generating'),k2
		deps = set()
		for x3 in v2 : # for each serial dep list of parallel deps
			for x4 in x3 : # for each dep in list of parallel deps
				deps.add(x4[-1]) # the dep name
	assert '/etc/os-release' in deps
