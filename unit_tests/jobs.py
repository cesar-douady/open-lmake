# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

n_jobs = 4

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'trig'
	)

	lmake.config.backends.local.cpu = n_jobs*2
	lmake.config.backends.local.mem = n_jobs*2

	class GenFile(PyRule) :
		target    = 'file_{:\d+}'
		deps      = {'TRIG':'trig'}
		resources = {'mem':1}
		def cmd() :
			lmake.depend(TRIG)
			assert int(os.environ['SMALL_ID'])<=n_jobs , f"small id is {os.environ['SMALL_ID']} > {n_jobs}"

	class Trig(PyRule) :
		target    = 'out_{N:\d+}'
		resources = {'mem':1}
		def cmd() :
			lmake.depend([f'file_{x}' for x in range(int(N))])

else :

	import ut

	n = n_jobs*2+10
	print(1,file=open('trig','w'))
	ut.lmake( '-j' , str(n_jobs) , f'out_{n}' , new=1 , may_rerun=1 , done=n , steady=1 )
	print(2,file=open('trig','w'))
	ut.lmake(                      f'out_{n}' , new=1 , failed=... , steady=... , changed=1 , rc=1 ) # python reads Lmakefile.py to display backtrace
