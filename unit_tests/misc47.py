# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import ut

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = [
		'Lmakefile.py'
	,	'ut.py'
	]

	class Fast(Rule) :
		targets = {
			'DUT'  : 'dut'     # asked on command line, a regular file
		,	'LNK'  : 'dut_lnk' # asked on command line, a symlink (as python3 -> python3.11)
		,	'MARK' : 'mark'    # dep of Slow, ensures Slow starts after Fast is done
		}
		cmd = 'echo hello >{DUT} ; ln -s {DUT} {LNK} ; >{MARK}'

	class Slow(PyRule) :
		target = 'slow'
		deps   = { 'MARK' : 'mark' }
		def cmd() :
			ut.trigger_sync(0) # Fast is done, tell test script it can alter dates
			ut.wait_sync   (1) # wait for test script to be done, then finish, which triggers run 2 of req

else :

	import os

	ut.mk_syncs(2)

	proc = ut.lmake( 'dut' , 'dut_lnk' , 'slow' , wait=False , new=1 , done=2 , manual_steady=... )
	#
	ut.wait_sync(0)                                            # Fast is done, its targets are recorded with their dates
	ut.file_sync()
	os.utime( 'dut'                             )              # content is unchanged, only date is changed
	os.utime( 'dut_lnk' , follow_symlinks=False )              # .
	ut.trigger_sync(1)
	#
	proc()                                                     # currently fails : run_loop req, rc=1
	#
	ut.lmake( 'dut' , 'dut_lnk' , 'slow' , manual_steady=... ) # nothing to do
