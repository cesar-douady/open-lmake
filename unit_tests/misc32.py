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
	,	'src'
	)

	class Dut(PyRule) :
		target = 'dut'
		cache = 'my_cache'
		def cmd() :
			lmake.target('src', source_ok=True)
			print('from_job',file=open('src','w'))

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

	print('src',file=open('src','w'))

	ut.lmake( 'dut' , done=1 )
	sp.run(('lforget','dut'))
	ut.lmake( 'dut' , hit_steady=1 )

	assert osp.exists('src')
