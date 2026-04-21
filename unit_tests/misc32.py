# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	lmake.manifest = (
		'Lmakefile.py'
	,	'src1'
	)

	class Init(PyRule) :
		target = 'init'
		def cmd() :
			lmake.target('src2')
			print('from job',file=open('src2','w'))

	class Dut1(PyRule) :
		target = 'dut1'
		cache = 'my_cache'
		def cmd() :
			lmake.target('src1',source_ok=True)
			open('src1')

	class Dut2(PyRule) :
		target = 'dut2'
		cache = 'my_cache'
		def cmd() :
			lmake.target('src2',incremental=True)
			open('src2')

else :

	import os
	import os.path as osp
	import stat
	import subprocess as sp
	import textwrap

	import ut

	os.makedirs( 'CACHE/LMAKE' , mode=stat.S_ISGID|stat.S_IRWXU|stat.S_IRWXG )
	print(textwrap.dedent('''
		size = 1<<20
	''')[1:],file=open('CACHE/LMAKE/config.py','w'))

	print('src1',file=open('src1','w'))

	ut.lmake( 'init' , done=1 )

	ut.lmake( 'dut1' , 'dut2' , done=2 , new=1 )
	sp.run(('lforget','dut1','dut2'))
	ut.lmake( 'dut1' , 'dut2' , hit_steady=2 )

	assert open('src1').read().strip()=='src1'
	assert open('src2').read().strip()=='from job'
