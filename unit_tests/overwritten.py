

# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import ut

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = [
		'Lmakefile.py'
	,	'ut.py'
	,	'src'
	]

	lmake.config.console.date_precision = 3

	class Dut(PyRule) :
		target   = 'dut'
		max_runs = 1
		def cmd() :
			open('dut','w').write(open('src').read())
			ut.trigger_sync(0)
			ut.wait_sync   (1)

else :

	import os
	import time

	ut.mk_syncs(2)

	print(1,file=open('src','w'))
	#
	proc = ut.lmake( 'dut' , wait=False , new=2 , dep_error=1 , rc=1 ) # src will change during execution
	#
	# synchronize during first execution
	ut.wait_sync(0)
	ut.file_sync()
	print(2,file=open('src','w'))                                      # modif
	ut.trigger_sync(1)
	#
	proc()
