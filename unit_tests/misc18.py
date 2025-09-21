# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Derive(PyRule) :
		target = 'derived'
		deps   = {'SRC':'src'}
		def cmd() :
			assert open(SRC).read().strip()!='bad'
			print('ok')

	class Dut(PyRule) :
		target = 'dut'
		deps   = {
			'DERIVED' : ('derived','critical')
		,	'SRC'     :  'src'
		}
		def cmd() :
			open(DERIVED)
			open(SRC    )

else :

	import ut

	print('good1',file=open('src','w'))
	ut.lmake( 'dut' , new=1 , done=2 ) # derived and dut are run

	print('bad',file=open('src','w'))
	ut.lmake( 'dut' , changed=1 , new=1 , failed=1 , rc=1 ) # derived fails

	print('good1',file=open('src','w'))
	ut.lmake( 'dut' , changed=1 , done=1 , steady=1 ) # derived is rerun (dut is up-to-date but rerun in v25.07)

	print('good2',file=open('src','w'))
	ut.lmake( 'dut' , changed=1 , steady=2 ) # derived and dut are rerun
