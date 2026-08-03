# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	,	'extra'
	,	'trigger'
	)

	def cpu() :
		if open('trigger').read().strip()=='yes' : # false on 1st run, true on 2nd one
			open('src'  ).read()                   # src is also a dep of Dut, stored as a chunk header
			open('extra').read()                   # extra has never been seen by Dut : force deps rebuild
		return 1

	class Dut(Rule) :
		target    = 'dut'
		resources = { 'cpu' : cpu }
		cmd       = '[ -f hole ] ; echo "$(<src)"' # access non-existing hole, then src, with no external process in between

else :

	import ut

	print('',file=open('extra','w'))

	print(1   ,file=open('src'    ,'w'))
	print('no',file=open('trigger','w'))
	ut.lmake( 'dut' , done=1 ,             new=2 ) # normal run, records deps hole (non-existing) then src

	print(2    ,file=open('src'    ,'w'))          # force Dut to rerun
	print('yes',file=open('trigger','w'))          # from now on, cpu() reads src & extra at submit time
	ut.lmake( 'dut' , done=1 , changed=2 , new=1 ) # check resources deps are merged into job deps w/o crash
