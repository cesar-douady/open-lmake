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
	,	'hello'
	,	'hello+auto1.hide.ref'
	,	'mkdir.dut.ref'
	)

	from step import z_lvl

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	class Auto(Rule) :
		target = r'auto{:\d}'
		cache  = 'my_cache'
		cmd    = "echo '#auto'"

	class Hide(Rule) :
		target    = r'{File:.*}.hide'
		stderr_ok = True
		cache     = 'my_cache'
		cmd       = 'cat {File} || :'

	class Cat(Rule) :
		prio = 1
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		cache       = 'my_cache'
		compression = z_lvl
		cmd         = 'cat {FIRST} {SECOND}'

	class MkDir(Rule):
		target      = 'mkdir.dut'
		targets     = { 'OUT' : r'mkdir.dir/{*:.*}' }
		cache       = 'my_cache'
		compression = z_lvl
		cmd = '''
			dir={OUT('v1')}
			mkdir -p $dir
			> $dir/res
			echo mkdir
		'''

	class Ok(Rule) :
		target = r'{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT}>&2'

else :

	import os
	import stat
	import textwrap

	import ut

	for z_lvl in (0,5) :
		print(f'z_lvl={z_lvl}',file=open('step.py','w'))
		bck = f'bck_{z_lvl}'

		print('hello'       ,file=open('hello'               ,'w'))
		print('hello\n#auto',file=open('hello+auto1.hide.ref','w'))
		print('mkdir'       ,file=open('mkdir.dut.ref'       ,'w'))

		# cache my_cache must be writable by all users having access to the cache
		# use setfacl(1) with adequate rights in the default ACL, e.g. :
		# os.system('setfacl -m d:g::rw,d:o::r CACHE')
		os.makedirs( 'CACHE/LMAKE' , mode=stat.S_ISGID|stat.S_IRWXU|stat.S_IRWXG )
		print(textwrap.dedent('''
			size = 1<<20
		''')[1:],file=open('CACHE/LMAKE/config.py','w'))

		ut.lmake( 'hello+auto1.hide.ok' , done=4 , may_rerun=1 , new=2 ) # check target is out of date
		ut.lmake( 'hello+auto1.hide.ok' , done=0                       ) # check target is up to date
		ut.lmake( 'mkdir.dut.ok'        , done=2 ,               new=1 ) # check everything is ok with dirs and empty files

		os.system('find CACHE -type f -ls')
		os.system(f'mkdir {bck}_1 ; mv LMAKE auto1 auto1.hide hello+auto1.hide {bck}_1')

		print('hello2'       ,file=open('hello'               ,'w'))
		print('hello2\n#auto',file=open('hello+auto1.hide.ref','w'))

		ut.lmake( 'hello+auto1.hide.ok' , unlinked=1 , done=2 , hit_done=2 ,                 new=2 )  # check cache hit on common part (except auto1), and miss when hello is dep
		ut.lmake( 'mkdir.dut.ok'        , unlinked=1 , done=1 , hit_done=1 , quarantined=1 , new=1 )  # check all is ok with dirs and empty files (mkdir.dut still exists and is unlinked)
		os.system(f'mkdir {bck}_2 ; mv LMAKE auto1 auto1.hide hello+auto1.hide {bck}_2')

		assert os.system(f"rm -rf CACHE/auto1 ; lcache_repair -f CACHE")==0
		ut.lmake( 'hello+auto1.hide.ok' , unlinked=1 , done=2 , hit_rerun=1 , hit_done=2 ,                 new=2 )
		ut.lmake( 'mkdir.dut.ok'        , unlinked=1 , done=1 ,               hit_done=1 , quarantined=1 , new=1 )
		os.system(f'mkdir {bck}_3 ; mv LMAKE CACHE *auto1* mkdir* {bck}_3')

		bck = f'bck'
		ut.lmake( 'hello+auto1.hide' , done=3 , may_rerun=1 , new=1 ) # check no crash with no cache
		os.system(f'mkdir {bck}_3 ; mv LMAKE *auto1* {bck}_3')
