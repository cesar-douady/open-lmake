# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import Rule

	gxx = os.environ.get('CXX','g++')

	lmake.manifest = (
		'Lmakefile.py'
	,	'dut.cc'
	,	'dep'
	)

	class Compile(Rule) :
		targets = { 'EXE' : r'{File:.*}.exe' }
		deps    = { 'SRC' :  '{File}.cc'     }
		cmd     = '{gxx} -O0 -fdiagnostics-color=always -std=c++20 -pthread -o {EXE} {SRC}'

	class Dut(Rule) :
		target = r'{File:.*}.out'
		deps   = { 'EXE':'{File}.exe' }
		cmd    = './{EXE}'

else :

	import os

	import ut

	os.symlink('../clone.cc','dut.cc')

	print('1',file=open('dep','w'))
	ut.lmake( 'dut.out' , new=2 , done=2 )

	print('2',file=open('dep','w'))
	ut.lmake( 'dut.out' , changed=1 , done=1 ) # ensure dep is seen as a dep
