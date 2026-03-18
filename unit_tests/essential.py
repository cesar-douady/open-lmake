# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'essential'
	,	'non-essential'
	)

	class Dut(Rule) :
		target = 'dut'
		deps = {
			'ESSENTIAL'     :  'essential'
		,	'NON_ESSENTIAL' : ('non-essential','-essential')
		}
		cmd = 'cat {ESSENTIAL} {NON_ESSENTIAL}'

else :

	import os

	import ut

	print('v1',file=open('essential'    ,'w'))
	print('v1',file=open('non-essential','w'))
	ut.lmake( 'dut' , new=2 , done=1 )

	print('v2',file=open('essential'    ,'w')) ; ut.lmake( '-d' , 'dut' , changed=1 , done  =1 )
	print('v2',file=open('non-essential','w')) ; ut.lmake( '-d' , 'dut'                        ) # dont rebuild if only due to a non-essential dep
	None                                       ; ut.lmake(        'dut' , changed=1 , done  =1 ) # restore fully checked repo
	os.unlink('dut')                           ; ut.lmake( '-d' , 'dut' ,             steady=1 ) # rebuild if not due to a dep

	print('v3',file=open('essential'    ,'w')) ; ut.lmake( '-D' , 'dut'                        ) # dont rebuild if only due to a dep
	print('v3',file=open('non-essential','w')) ; ut.lmake( '-D' , 'dut'                        ) # .
	None                                       ; ut.lmake(        'dut' , changed=2 , done  =1 ) # restore fully checked repo
	os.unlink('dut')                           ; ut.lmake( '-D' , 'dut' ,             steady=1 )
