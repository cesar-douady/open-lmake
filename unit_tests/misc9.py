# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Good(Rule) :
		target = r'good{D:\d+}'
		cmd    = ''

	class Dut(Rule) :
		target = 'dut'
		cmd    = '''
			if cat good1 ; then ldepend good2
			else                ldepend bad2
			fi
		'''

else :

	import ut

	ut.lmake( 'dut' , may_rerun=2 , done=2 , steady=1 ) # check that errors past out-of-date does not prevent rerun
