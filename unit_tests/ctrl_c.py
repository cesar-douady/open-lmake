# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import ut

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'ut.py'
	)

	class Dut(PyRule) :
		target = 'dut'
		def cmd() :
			ut.trigger_sync(0)
			ut.wait_sync   (1)

else :

	import os

	ut.mk_syncs(2)

	proc = ut.lmake( 'dut' , wait=False , killed=1 , rc=-2 )
	#
	ut.wait_sync(0)
	#
	assert proc.proc.pid>1 , f'bad pid {proc.proc.pid}' # ensure we do not kill the world
	os.kill(proc.proc.pid,2)
	#
	proc()

	proc = ut.lmake( 'dut' , wait=False , new=1 , steady=1 )
	ut.wait_sync(0)
	ut.trigger_sync(1)
	proc()
