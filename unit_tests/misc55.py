# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# lmake -n must announce once a job that would run, even if it is asked through several of its targets
# currently : J is announced (and counted) twice : after the dry run Done, ri.state.reason still needs a run,
# so when asked through its 2nd target, JobData::make goes to Run again

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Cpy(Rule) :
		target = r'{File:.*}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

	class Dut(Rule) :
		target = 'dut'
		deps = {
			'SUB1' : 'sub1.cpy'
		,	'SUB2' : 'sub2.cpy'
		}
		cmd = 'cat {SUB1} {SUB2}'

	class Sub(Rule) :
		targets = {
			'TGT1':'sub1'
		,	'TGT2':'sub2'
		}
		deps = { 'SRC' : 'src' }
		cmd  = 'cat {SRC} > {TGT1} ; cat {SRC} > {TGT2}'

else :

	import re
	import subprocess as sp

	import ut

	print('v1',file=open('src','w'))
	ut.lmake( 'dut' , new=1 , done=4 ) # may_rerun vs rerun depends on scheduling

	print('v2',file=open('src','w'))
	res = sp.run( ('lmake','-n','dut') , stdout=sp.PIPE , universal_newlines=True , check=True ).stdout
	print(res)
	n_sub = len(re.findall(r'\sSub\s',res))
	assert n_sub==1 and 'sub1.cpy' in res and 'sub2.cpy' in res
