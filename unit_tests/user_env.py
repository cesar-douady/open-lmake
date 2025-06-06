# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	var = lmake.user_environ.VAR

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'echo {var}'

	class Test(Rule) :
		target = r'test.{Ref:.*}'
		dep    =  'dut'
		cmd    = '[ $(cat) = {Ref} ]'

else :

	import os

	import ut

	os.environ['VAR'] = 'val1' ; ut.lmake( 'test.val1' , done=2 )
	os.environ['VAR'] = 'val2' ; ut.lmake( 'test.val2' , done=2 ) # check config is re-read
