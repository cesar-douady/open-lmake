# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# lmake -n must announce once a job that would run, even if it is asked through several of its targets
# currently : J is announced (and counted) twice : after the dry run Done, ri.state.reason still needs a run,
# so when asked through its 2nd target, JobData::make goes to Run again

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class R3(Rule) :
		prio = 0
		target = '{File:.*}abc'
		cmd    = ''

	class R2(Rule) :
		prio = 1
		target = '{File:.*}bc'
		cmd    = ''

	class R1(Rule) :
		prio = 1
		target = '{File:.*}c'
		cmd    = ''

else :

	import ut

	ut.lmake( 'zabc' , multi=1 , rc=1 ) # check both R1 and R2 apply
