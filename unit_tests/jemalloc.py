# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	)

	class Cpy(Rule) :
		stems   = { 'File' : r'.*' }
		autodep = 'ld_preload_jemalloc'
		environ = { 'LD_PRELOAD':'libjemalloc.so' }

	class Auto(Rule) :
		target = r'auto{D:\d+}'
		cmd    = 'echo {D}'

	class CpySh(Cpy) :
		target = '{File}_sh'
		cmd    = 'cat {File}'

	class CpyPy(Cpy,PyRule) :
		target = '{File}_py'
		def cmd() :
			print(open(File).read(),end='')

else :

	import os
	import subprocess as sp
	import sys

	sav = os.environ.get('LD_PRELOAD')
	os.environ['LD_PRELOAD'] = 'libjemalloc.so'
	has_jemalloc             = not sp.run(('/usr/bin/echo',),check=True,stderr=sp.PIPE).stderr
	if sav is None : del os.environ['LD_PRELOAD']
	else           :     os.environ['LD_PRELOAD'] = sav

	if not has_jemalloc :
		print('jemalloc not available',file=open('skipped','w'))
		exit()

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'auto1_sh' , 'auto2_py' , may_rerun=2 , done=4 , new=1 ) # check deps are acquired
