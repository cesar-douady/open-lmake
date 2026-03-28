# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'b'
	)

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	class A(Rule) :
		target = 'a'
		cache  = 'my_cache'
		cmd    = '''
			echo A
			ltarget -s b
			ltarget    c
			echo B2>b
			echo C2>c
		'''
else :

	import os
	import stat
	import textwrap

	import ut

	print('B1',file=open('b','w'))
	os.makedirs( 'CACHE/LMAKE' , mode=stat.S_ISGID|stat.S_IRWXU|stat.S_IRWXG )
	print(textwrap.dedent('''
		size = 1<<20
	''')[1:],file=open('CACHE/LMAKE/config.py','w'))

	ut.lmake( 'a' , done=1 )
	assert open('b').read().strip()=='B2'
	assert open('c').read().strip()=='C2'

	os.unlink('a')

	ut.lmake( 'a' , hit_steady=1 )
	assert open('b').read().strip()=='B2'
	assert open('c').read().strip()=='C2'
