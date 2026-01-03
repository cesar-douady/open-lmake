# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'dep'
	)

	from step import step

	class Dut(Rule) :
		target    = 'dut'
		deps      = { 'DEP':'dep' }
		resources = { 'cpu':step }
		cmd       = 'cat no_file 2>/dev/null ; cat {DEP} ; [ {cpu} != 2 ]'

else :

	import subprocess as sp

	import ut

	open('dep','w')

	print('step=1',file=open('step.py','w'))

	ut.lmake( 'dut' , done=1 , new=1 )

	sp.run(('lforget','-d',     'dut'),check=True) ; ut.lmake( 'dut' , steady=1 )
	sp.run(('lforget','-d','-J','dut'),check=True) ; ut.lmake( 'dut' , steady=1 )
	sp.run(('lforget','-r'           ),check=True) ; ut.lmake( 'dut'            )

	print('step=2',file=open('step.py','w'))                                       # dont run lmake before lforget to ensure lforget forces a rule refresh
	sp.run(('lforget','-r'),check=True)      ; ut.lmake( 'dut' , failed=1 , rc=1 ) # force taking into account resources modif, even for ok jobs
	print('step=3',file=open('step.py','w')) ; ut.lmake( 'dut' , steady=1        ) # resources modif rerun ko jobs

