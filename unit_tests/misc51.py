# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep_ok'
	)

	class Dep(Rule) :
		target = 'dep'
		dep    = 'dep_ok'
		cmd    = 'grep -qx ok && echo dep_content' # fails unless dep_ok contains ok

	class Dut(Rule) :
		target = 'dut'
		dep    = 'dep'
		cmd    = 'cat'

else :

	import subprocess as sp

	import ut

	print( 'ok' , file=open('dep_ok','w') ) ; ut.lmake( 'dut' , new    =1 , done  =2        ) # Dut runs once, so that it can be frozen
	print( 'ko' , file=open('dep_ok','w') ) ; ut.lmake( 'dut' , changed=1 , failed=1 , rc=1 ) # Dep fails, Dut is in dep_error

	sp.run(('lmark','-f','-a','dut'),check=True) # freeze Dut while it is in dep_error

	print( 'ok' , file=open('dep_ok','w') ) ; ut.lmake( 'dep' , changed=1 , done  =1 ) # repair Dep
	None                                    ; ut.lmake( 'dep'                        ) # dep is up-to-date
	None                                    ; ut.lmake( 'dut' , frozen =1            ) # Dut is frozen, it must not fail because of its old dep_error state

	assert open('dut').read()=='dep_content\n' # frozen target is untouched
