# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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

	lmake.config.caches.dir = {
		'tag'  : 'dir'
	,	'dir'  : lmake.repo_root+'/CACHE'
	,	'perm' : 'group'
	}

	class Auto(Rule) :
		target = r'auto{:\d}'
		cache  = 'dir'
		cmd    = "echo '#auto'"

	class Hide(Rule) :
		target    = r'{File:.*}.hide'
		stderr_ok = True
		cache     = 'dir'
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
		cache       = 'dir'
		compression = z_lvl
		cmd         = 'cat {FIRST} {SECOND}'

	class MkDir(Rule):
		target      = 'mkdir.dut'
		targets     = { 'OUT' : r'mkdir.dir/{*:.*}' }
		cache       = 'dir'
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

	import ut

	for z_lvl in (0,5) :
		print(f'z_lvl={z_lvl}',file=open('step.py','w'))

		print('hello'       ,file=open('hello'               ,'w'))
		print('hello\n#auto',file=open('hello+auto1.hide.ref','w'))
		print('mkdir'       ,file=open('mkdir.dut.ref'       ,'w'))

		# cache dir must be writable by all users having access to the cache
		# use setfacl(1) with adequate rights in the default ACL, e.g. :
		# os.system('setfacl -m d:g::rw,d:o::r CACHE')
		os.makedirs('CACHE/LMAKE')
		os.chmod('CACHE'      ,0o775)
		os.chmod('CACHE/LMAKE',0o775)
		print('size=1<<20',file=open('CACHE/LMAKE/config.py','w'))

		ut.lmake( 'hello+auto1.hide.ok' , done=4 , may_rerun=1 , new=2 ) # check target is out of date
		ut.lmake( 'hello+auto1.hide.ok' , done=0                       ) # check target is up to date
		ut.lmake( 'mkdir.dut.ok'        , done=2 ,               new=1 ) # check everything is ok with dirs and empty files

		os.system(f'mkdir bck{z_lvl} ; mv LMAKE auto1 auto1.hide hello+auto1.hide bck{z_lvl}')
		os.system('find CACHE -type f -ls')

		assert os.system('rm -rf CACHE/auto1 ; ldircache_repair CACHE')==0

		print('hello2'       ,file=open('hello'               ,'w'))
		print('hello2\n#auto',file=open('hello+auto1.hide.ref','w'))
		ut.lmake( 'hello+auto1.hide.ok' , done=3 , hit_rerun=1 , hit_done=1 , unlinked=1                 , new=2 ) # check cache hit on common part (except auto1), and miss when we depend on hello
		ut.lmake( 'mkdir.dut.ok'        , done=1 , hit_rerun=1 , hit_done=1 , unlinked=1 , quarantined=1 , new=1 ) # check all is ok with dirs and empty files (mkdir.dut still exists and is unlinked)

		os.system(f'mkdir bck2{z_lvl} ; mv LMAKE CACHE *auto1* mkdir* bck2{z_lvl}')

	ut.lmake( 'hello+auto1.hide' , done=3 , may_rerun=1 , new=1 ) # check no crash with no cache

