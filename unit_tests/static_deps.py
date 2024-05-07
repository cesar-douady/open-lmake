# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import sys

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dummy'
	)

	class Dut(Rule) :
		target  = r'dut.{Autodep:\w+}'
		deps    = { 'Dummy' : 'dummy' }
		autodep = '{Autodep}'
		cmd     = ''

else :

	autodeps = ['none']
	if lmake.has_ptrace     : autodeps.append('ptrace'    )
	if lmake.has_ld_audit   : autodeps.append('ld_audit'  )
	if lmake.has_ld_preload : autodeps.append('ld_preload')

	import ut

	print(f'1',file=open('dummy','w'))
	ut.lmake( *(f'dut.{ad}' for ad in autodeps) , new=1 , done=len(autodeps) )

	print(f'2',file=open('dummy','w'))
	ut.lmake( *(f'dut.{ad}' for ad in autodeps) , changed=1 , steady=1 ) # only case autodep=none believes that dut needs dummy
