# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# spurious clash on a target whose actual job has lost its rule (SWEAR(+aj->rule()) in JobExec::end_analyze) :
# - tgt is produced by the job of rule Old, then Old disappears from Lmakefile.py, so this job has no rule any more
# - New (different match, hence a different job) rewrites tgt with the same content
# - meanwhile, a 2nd lmake asks for tgt : the server refreshes it, finds it steady and dates it now, i.e. after New started
# - when New ends, this date looks like a clash with the actual job of tgt, which is the ruleless job of Old

import ut

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import Rule,PyRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'ut.py'
	,	'step.py'
	,	'src'
	)

	if step==1 :

		class Old(Rule) :
			target = 'tgt'
			cmd    = 'echo hello'

	else :

		class New(PyRule) :
			targets = { 'TGT' : 'tgt' }
			deps    = { 'SRC' : 'src' }        # ensure match differs from Old's
			def cmd() :
				open(TGT,'w').write('hello\n') # same content as Old
				ut.trigger_sync(0)             # wait until 2nd lmake has refreshed tgt
				ut.wait_sync   (1)             # .

		class Go(PyRule) :
			target = 'go'
			def cmd() :
				ut.trigger_sync(1) # tgt has been refreshed when 2nd lmake launches us

else :

	ut.mk_syncs(2)

	print('src',file=open('src','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'tgt' , done=1 )

	print('step=2',file=open('step.py','w'))
	p1 = ut.lmake( 'tgt' , wait=False , new=... , steady=1 )
	ut.wait_sync(0)                                                                      # New runs and has rewritten tgt
	p2 = ut.lmake( 'tgt' , 'go' , wait=False , new=... , started=1 , done=1 , steady=1 ) # refresh tgt while New runs
	cnt1 = p1()
	cnt2 = p2()

	ut.lmake( 'tgt' , 'go' ) # ensure all is up-to-date
