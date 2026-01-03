# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'phy_dir/dep'
	,	'ref'
	,	'../'
	)

	class Dut(Rule) :
		views  = { 'log_dir/' : 'phy_dir/' }
		target = 'dut'
		cmd    = 'cat log_dir/dep'

	class Test(Rule) :
		target = 'test'
		deps = {
			'DUT' : 'dut'
		,	'REF' : 'ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os
	import subprocess as sp

	import ut

	print('good',file=open('ref','w'))

	os.makedirs('phy_dir',exist_ok=True)

	print('bad',file=open('phy_dir/dep','w'))
	ut.lmake( 'test' , new=2 , done=1 , failed=1 , rc=1 ) # check deps are mapped from log to phy
	#
	print('good',file=open('phy_dir/dep','w'))
	ut.lmake( 'test' , changed=1 , done=2 )               # check deps are correctly recorded through dir views
	#
	ut.lmake( 'test' )

	x = sp.check_output(('ldebug','-t','dut'),universal_newlines=True)
	assert open('ref').read()==x

	x = sp.check_output(('lshow','-i','dut'),universal_newlines=True)
	assert 'views' in x and 'log_dir' in x and 'phy_dir' in x

	px = eval(sp.check_output(('lshow','-ip','-J','dut'),universal_newlines=True))
	assert px['views'] == {'log_dir':'phy_dir'}
