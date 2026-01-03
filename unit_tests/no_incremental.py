# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src1'
	,	'src2'
	)

	class Dut(Rule) :
		targets = { 'DUT' : (r'dut{D:\d}','incremental') }
		deps    = { 'SRC' :  'src{D}'                    }
		cmd     = 'cp {SRC} {DUT}'

	class Collect(Rule) :
		target = 'dut'
		deps   = { 'DUT1':'dut1' , 'DUT2':'dut2' }
		cmd    = 'cat {DUT1} {DUT2}'

else :

	import ut

	print('src1',file=open('src1','w'))
	print('src2',file=open('src2','w'))

	ut.lmake(        'dut' , new    =2 , done  =3 )
	ut.lmake( '-I' , 'dut'                        ) # nothing was made incremental, nothing to rerun
	print('src2_2',file=open('src2','w'))
	ut.lmake(        'dut' , changed=1 , done  =2 ) # dut2 remade
	ut.lmake( '-I' , 'dut' ,             steady=1 ) # dut2 remade as it was done incrementally
