# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake

	from lmake.rules import Rule

	lmake.config.max_dep_depth = 10
	lmake.config.path_max      = 20

	lmake.manifest = ('Lmakefile.py',)

	class Inf1(Rule) :
		target = '{X:.*}x1'
		dep    = 'a{X}x1'
		cmd    = ''

	class Inf3(Rule) :
		target = '{X:.*}x3'
		dep    = 'aaa{X}x3'
		cmd    = ''

	class Impossible(Rule) :
		prio   = 1
		target = '{X:.*}x'
		dep    = 'b'
		cmd    = ''

	class LowPrio(Rule) :
		prio   = -1
		target = '{X:.*}x'
		dep    = ''
		cmd    = ''

else :

	import ut

	ut.lmake( 'x1' , failed=1 , rc=1 ) # limit is max_dep_depth
	ut.lmake( 'x3' , failed=1 , rc=1 ) # limit is path_max
