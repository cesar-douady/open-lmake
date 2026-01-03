# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
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

	class WaitBefore(PyRule) :
		target = 'wait_before'
		deps   = { 'SRC' : 'src' }
		def cmd() :
			ut.trigger_sync(0)
			ut.wait_sync   (1)
			sys.stdout.write(open(SRC).read())

	class WaitAfter(PyRule) :
		target = 'wait_after'
		deps   = { 'SRC' : 'src' }
		def cmd() :
			sys.stdout.write(open(SRC).read())
			ut.trigger_sync(0)
			ut.wait_sync   (1)

else :

	import os

	ut.mk_syncs(2)

	print(1,file=open('src','w'))
	#
	proc = ut.lmake( 'wait_before' , wait=False , new=2 , done=1 )
	#
	ut.wait_sync(0)
	ut.file_sync()
	print(1,file=open('src','w')) # do not change content
	ut.trigger_sync(1)
	#
	proc()
	#
	proc = ut.lmake( 'wait_before' )

	ut.file_sync()
	print(2,file=open('src','w'))
	#
	proc = ut.lmake( 'wait_before' , wait=False , changed=1 , dep_error=1 , rc=1 ) # src will change during execution
	#
	ut.wait_sync(0)
	ut.file_sync()
	print(3,file=open('src','w'))                                                  # modif
	ut.trigger_sync(1)
	#
	proc()
	#
	ut.lmake( 'wait_before' )                                                      # job has sampled src after modif

	print(4,file=open('src','w'))
	#
	proc = ut.lmake( 'wait_after' , wait=False , changed=2 , dep_error=1 , rc=1 ) # src will change during execution
	#
	ut.wait_sync(0)
	ut.file_sync()
	import time ; time.sleep(1)
	print(5,file=open('src','w'))                                                 # modif
	ut.trigger_sync(1)
	#
	proc()
	#
	ut.lmake( 'wait_after' , wait=False , done=1 )                                # job has sampled src before modif
	ut.wait_sync   (0)
	ut.trigger_sync(1)
	proc()

