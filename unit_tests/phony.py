# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class Test1(lmake.Rule):
		targets = { 'GOOD' : ('good','Phony') }
		def cmd():
			if step==1 : lmake.depend('bad',required=True)

	class Test2(lmake.Rule):
		target = 'tgt'
		def cmd():
			lmake.depend('good',required=True)
			if step==1 : lmake.depend('bad',required=True)

else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake('tgt',may_rerun=1,dep_err=1,was_dep_err=1,rc=1)

	print('step=2',file=open('step.py','w'))
	ut.lmake('tgt',steady=2)
