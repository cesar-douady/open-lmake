# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys
	import time

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class BaseRule(Rule) :
		start_delay = 0

	class Good(BaseRule) :
		target = 'good'
		cmd    = 'echo good_content'

	class Bad(BaseRule) :
		targets = { 'Bad' : 'bad1{*:}' }
		cmd     = 'sleep 3' # dont produce bad

	class Ptr(BaseRule) :
		target = 'ptr'
		cmd    = ' echo good ; sleep 2 '

	class Dut(BaseRule) :
		target       = 'dut'
		allow_stderr = True
		cmd = '''
			deps="$( cat ptr 2>/dev/null || echo bad1 bad2 )"
			ldepend $deps                             # makes deps required
			echo $deps $(cat $deps) >&2
			echo $(cat $deps)
			sleep 2
		'''

	class Side(BaseRule) :
		target = 'side'
		cmd = ' sleep 3 ; cat ptr good '

	class chk(BaseRule) :
		target = 'chk'
		deps   = { 'DUT':'dut' , 'SIDE':'side' }
		cmd    = '[ "$(cat {DUT})" = good_content ]'

else :

	import ut

#	ut.lmake( 'bad' , failed=1 , rc=1 )
	ut.lmake( 'chk' , done=5 , may_rerun=2 , steady=1 )
