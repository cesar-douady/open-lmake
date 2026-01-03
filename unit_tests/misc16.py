# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class A(Rule) :
		targets = { 'A1':'a1' , 'A2':'a2' }
		cmd     = ' sleep 5 ; echo >{A1} ; echo >{A2} '

	class W(Rule) :
		target = 'w'
		cmd    = 'sleep 2'

	class C(Rule) :
		target = 'c'
		cmd    = ' sleep 1 ; cat a1 '

	class B(Rule) :
		target = 'b'
		deps   = { 'W':'w' , 'A2':'a2' }
		cmd    = 'cat {A2} {W}'

	class D(Rule) :
		target = 'd'
		deps   = { 'B':'b' , 'C':'c' }
		cmd    = 'cat {B} {C}'

else :

	import os

	import ut

	ut.lmake( 'a1' , done=1 ) # create a1 & a2
	os.unlink('a1')           # but suppress a1, a2 is left up-to-date

	# when asking d, this is what happens :
	# because d needs c, C (which does not depend on a1 yet) is run (1s)
	# because d needs b which needs w, W is run (2s)
	# C terminates and discovers its dep on a1
	# a1 has been suppressed, hence A is run (5s)
	# W terminates and B is analyzed
	# the goal of this test is to ensure that despite B has already analyzed a2 as up-to-date, it must wait because in the mean time, A has been launched because of a1
	cnts = ut.lmake( 'd' , done=4 , may_rerun=... , steady=1 )
	assert 1<=cnts.may_rerun<=2                                # depending on details, we may miss busy a2 once
