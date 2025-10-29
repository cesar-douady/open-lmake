# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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

	class Hold(PyRule) :
		target = 'hold_server'
		def cmd() :
			ut.trigger_sync(0)
			ut.wait_sync   (1)

	class Dut(PyRule) :
		target = 'dut'
		def cmd() :
			ut.trigger_sync(2)
			ut.wait_sync   (3)

else :

	import os

	ut.mk_syncs(4)

	proc_hold = ut.lmake( 'hold_server' , wait=False , new=0 , done=1 )
	#
	ut.wait_sync(0)
	#
	proc_dut = ut.lmake( 'dut' , wait=False , fast_exit=True , killed=1 , rc=-2 )
	ut.wait_sync(2)
	assert proc_dut.proc.pid>1 , f'bad pid {proc_dut.proc.pid}' # ensure we do not kill the world
	os.kill(proc_dut.proc.pid,2)
	proc_dut()
	#
	proc_dut = ut.lmake( 'dut' , wait=False , fast_exit=True , new=1 , steady=1 )
	ut.wait_sync   (2)
	ut.trigger_sync(3)
	proc_dut()
	#
	ut.trigger_sync(1)
	proc_hold()
