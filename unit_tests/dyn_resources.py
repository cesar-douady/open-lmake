# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'cpu'
	)

	def get_cpu() :
		return open('cpu').read().strip()

	class Dut(Rule) :
		targets = { 'TGT':r'{:.*}' }
		resources = {
			'cpu' : get_cpu
		}
		cmd = '''
			[ {TGT} = ko   ] && exit 1
			[ {TGT} = read ] && cat cpu
			>{TGT}
		'''

else :

	import ut

	print(1,file=open('cpu','w'))
	ut.lmake( 'ok' , 'read' , 'ko' , new=1 , done=2 , failed=1 , rc=1 )
	print(2,file=open('cpu','w'))
	ut.lmake( 'ok' , 'read' , 'ko' , changed=1 , steady=1 , failed=1 , rc=1 ) # check ok is not remade, but read is remade because it read cpu in job
