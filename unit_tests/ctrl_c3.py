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

	class Dut1(PyRule) :
		target = 'dut1'
		def cmd() :
			ut.trigger_sync(0)
			ut.wait_sync   (1)

	class Dut2(PyRule) :
		target = 'dut2'
		def cmd() :
			ut.trigger_sync(2)
			ut.wait_sync   (3)

else :

	import os
	import time

	ut.mk_syncs(4)

	proc_duts = ut.lmake( 'dut1' , 'dut2' , wait=False , fast_exit=True , new=0 , killed=1 , rc=-2 , **{'continue':1} )
	#
	ut.wait_sync(0)
	ut.wait_sync(2)
	#
	proc_dut1 = ut.lmake( '-o' , 'dut1' , wait=False , started=1 , new=1 , done=1 )
	time.sleep(1)                                                                   # dont know how to securely wait for proc_dut1 to have started
	assert proc_duts.proc.pid>1 , f'bad pid {proc.proc.pid}'                        # ensure we do not kill the world
	os.kill(proc_duts.proc.pid,2)
	proc_duts()
	ut.trigger_sync(1)
	proc_dut1()
