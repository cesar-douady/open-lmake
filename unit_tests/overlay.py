# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'read/src'
	,	'ref'
	)

	class Dut(Rule) :
		views   = { 'read_write/' : ('write/','read/') }
		targets = { 'DUT' : 'write/dut' }
		deps    = { 'SRC' : 'read/src'  }
		cmd     = 'cd read_write ; cp src dut' # a typical cmd that work in a dir rather than having inputs and outputs

	class Test(Rule) :
		target = 'test'
		deps = {
			'DUT' : 'write/dut'
		,	'REF' : 'ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os

	import ut

	os.makedirs('read',exist_ok=True)

	print('good',file=open('ref'     ,'w'))
	print('good',file=open('read/src','w'))

	ut.lmake( 'test' , new=2 , done=2 )
