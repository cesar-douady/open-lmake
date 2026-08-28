# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os

import ut

N = 50 # number of extra dynamic deps read before the slow one

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'ut.py'
	,	'slow_dep'
	,	'src'
	,	*( f'f{i}' for i in range(N) )
	)

	# 'slow' is the only non-source dep of 'dut'.
	# On the 2nd lmake, 'slow_dep' has changed so 'slow' must be rebuilt ; its cmd blocks
	# until the test driver releases it, giving us a window where 'dut' is parked waiting
	# on 'slow' with ri.iter advanced up to it.
	class Slow(PyRule) :
		target = 'slow'
		deps   = { 'SRC':'slow_dep' }
		def cmd() :
			import ut
			ut.trigger_sync(0)             # tell the driver we (re)started : dut is now waiting on us
			ut.wait_sync   (1)             # block until the driver says go
			print(open(SRC).read(),end='')

	# 'dut' has ONE static dep (src) so that `lforget -d` leaves a non-empty (but much
	# shorter) deps vector. It reads src, then f0..fN-1, then 'slow' LAST, so ri.iter
	# gets advanced past ~N+1 deps before parking on 'slow'.
	class Dut(Rule) :
		target = 'dut'
		deps   = { 'SRC':'src' }
		cmd    = f"cat {{SRC}} {' '.join(f'f{i}' for i in range(N))} slow"

	# aux exists only to consume the size-1 deps slot that dut frees when its deps grow
	# from [src] to the full list at the end of run 1. With that slot consumed, lforget's
	# re-allocation of dut's (again size-1) deps has to bump the store high-water, so the
	# now-stale ri.iter offset points into the freshly-mapped zero region -> node idx 0.
	class Aux(Rule) :
		target = 'aux'
		dep    = 'src'
		cmd    = 'cat'

else :

		import subprocess as sp
		import sys

		import ut

		ut.mk_syncs(2)

		for i in range(N) : open(f'f{i}','w').write(f'f{i}\n')
		open('src'     ,'w').write('st\n'    )
		open('slow_dep','w').write('slow-v1\n')

		# ---- run 1 : populate dut's deps = [ st , f0..fN-1 , slow ] ----
		p1 = ut.lmake('dut',new=N+3,may_rerun=1,done=2,wait=False) # Slow started
		ut.wait_sync   (0)
		ut.trigger_sync(1)                                         # let Slow finish (no interference on run 1)
		p1()

		# ---- consume dut's freed size-1 deps slot so that lforget must bump the high-water ----
		ut.lmake('aux',done=1)

		# ---- invalidate 'slow' so it must be rebuilt on run 2 ----
		ut.file_sync()
		open('slow_dep','w').write('slow-v2\n')

		# ---- run 2 : while dut waits on 'slow', forget dut's deps (truncate to [st]) ----
		p2 = ut.lmake('dut',changed=1,done=2,wait=False)
		ut.wait_sync(0)                                                               # Slow restarted -> dut is parked waiting on slow
		sp.run( ('lforget','-J','-d','dut') , universal_newlines=True , check=False )
		ut.trigger_sync(1)                                                            # release Slow -> dut wakeup -> stale ri.iter -> BOOM
		p2()
