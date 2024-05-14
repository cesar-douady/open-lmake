# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'phy/dep'
	,	'ref'
	)

	class Dut(Rule) :
		views = { 'log' : 'phy' }
		target = 'dut'
		cmd    = 'cat log/dep'

	class Test(Rule) :
		target = 'test'
		deps = {
			'DUT' : 'dut'
		,	'REF' : 'ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os

	import ut

	print('good',file=open('ref','w'))

	os.makedirs('phy',exist_ok=True)

	print('bad',file=open('phy/dep','w'))
	ut.lmake( 'test' , new=2 , done=1 , failed=1 , rc=1 ) # check deps are mapped from log to phy

	print('good',file=open('phy/dep','w'))
	ut.lmake( 'test' , changed=1 , done=2 )         # check deps are correctly recorded
