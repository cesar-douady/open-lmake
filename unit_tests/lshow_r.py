# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import ut

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'ut.py'
	)

	class Wait(PyRule) :
		target = r'wait{D:\d}'
		def cmd() :
			ut.trigger_sync(int(D)*2  )
			ut.wait_sync   (int(D)*2+1)

	class Cpy(Rule) :
		target = 'dut'
		deps   = {
			'W0' : 'wait0'
		,	'W1' : 'wait1'
		}
		cmd = 'cat'

else :

	ut.mk_syncs(4)

	proc = ut.lmake( 'dut' , wait=False , new=1 , done=3 )
	#
	ut.wait_sync(0)
	ut.wait_sync(2)
	x,xp = ut.lshow( ('-r','--running') , 'dut' )
	ut.trigger_sync(1)
	ut.trigger_sync(3)
	#
	proc()

	assert xp=={('Cpy','dut'):{('Wait','wait0'):True,('Wait','wait1'):True}},xp
