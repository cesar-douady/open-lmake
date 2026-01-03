# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	try    : depth
	except : depth = 0 # define depth for top level

	import lmake
	from lmake.rules import Rule

	lmake.config.sub_repos = ('a','b') # for top level only, overwritten in sub-repos

	lmake.manifest = ('Lmakefile.py',)

	class Dut(Rule) :
		target = 'dut'
		cmd    = f'echo {depth}'

	class Test(Rule) :
		target = r'{File:.*}.ok'
		dep    =  '{File   }'
		cmd    = f'[ $(cat) = {depth} ]'

else :

	import os

	import ut

	makefile = open('Lmakefile.py').read()
	os.makedirs('a'  ,exist_ok=True) ; print(f"depth=1\n{makefile}lmake.config.sub_repos = ('b',)",file=open('a/Lmakefile.py'  ,'w'))
	os.makedirs('b'  ,exist_ok=True) ; print(f"depth=1\n{makefile}lmake.config.sub_repos = ()"    ,file=open('b/Lmakefile.py'  ,'w'))
	os.makedirs('a/b',exist_ok=True) ; print(f"depth=2\n{makefile}lmake.config.sub_repos = ()"    ,file=open('a/b/Lmakefile.py','w'))

	ut.lmake('dut.ok','a/dut.ok','b/dut.ok','a/b/dut.ok',done=8)
