# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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
		target = 'wait'
		def cmd() :
			ut.trigger_sync(0)
			ut.wait_sync   (1)

	class Cpy(Rule) :
		target = 'dut'
		dep    = 'wait'
		cmd    = 'cat'

else :

	ut.mk_syncs(2)

	proc = ut.lmake( 'dut' , wait=False , new=1 , done=2 )
	#
	ut.wait_sync(0)
	x,xp = ut.lshow( ('-r','--running') , 'dut' )
	ut.trigger_sync(1)
	#
	proc()

	assert xp=={('R','Wait','wait')},xp
