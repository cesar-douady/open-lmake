# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Cat(Rule) :
		target = 'dut'
		cmd    = 'echo dut'

	class Cpy(Rule) :
		target           = r'{File:.*}.cpy'
		max_submit_count = 1
		allow_stderr     = True
		cmd              = ' cat {File} ; : '

	class Cmp(Rule) :
		target = 'dut.ok'
		dep    = 'dut.cpy'
		cmd    = '[ $(cat) = dut ]'

else :

	import ut

	ut.lmake( 'dut.ok' , may_rerun=1 , done=1 , submit_loop=1 , was_done=1 , rc=1 )
