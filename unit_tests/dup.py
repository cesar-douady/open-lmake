# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py','src')

	n = 10

	class Dut(Rule) :
		targets = { f'T{i}' : f'dut_{i}' for i in range(3,n) }
		cmd = ' '.join((
			'('
		,	*(f'echo "$(cat src){i}" >&{i} ;' for i in range(3,n))
		,	')'
		,	*(f'{i}>dut_{i}' for i in range(3,n))
		))

	class Chk(Rule) :
		target = 'test'
		deps   = { f'D{i}' : f'dut_{i}' for i in range(3,n) }
		shell  = ('/bin/bash','-e')
		cmd    = '\n'.join( f'[ "$(cat dut_{i})" = "$(cat src){i}" ]' for i in range(3,n) )

else :

	import ut

	print('hello',file=open('src','w'))

	ut.lmake( 'test' , done=2 , new=1 ) # check we can duplicate file descriptors with dup2
