# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	)

	lmake.config.caches.dir = {
		'tag' : 'dir'
	,	'dir' : lmake.repo_root+'/CACHE'
	}

	class Auto(Rule) :
		target = r'auto{:\d}'
		cache  = 'dir'
		cmd    = "echo '#auto'"

	class Hide(Rule) :
		target       = r'{File:.*}.hide'
		allow_stderr = True
		cache        = 'dir'
		cmd          = 'cat {File} || :'

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
		cache = 'dir'
		cmd   = 'cat {FIRST} {SECOND}'

	class MkDir(Rule):
		target  = 'mkdir.dut'
		targets = { 'OUT' : r'mkdir.dir/{*:.*}' }
		cache   = 'dir'
		cmd = '''
			dir={OUT('v1')}
			mkdir -p $dir
			> $dir/res
		'''

else :

	import os

	import ut

	print('hello',file=open('hello','w'))

	os.makedirs('CACHE/LMAKE')
	print('1M',file=open('CACHE/LMAKE/size','w'))

	ut.lmake( 'hello+auto1.hide' , done=3 , may_rerun=1 , new=1 ) # check target is out of date
	ut.lmake( 'hello+auto1.hide' , done=0 ,               new=0 ) # check target is up to date
	ut.lmake( 'mkdir.dut'        , done=1                       ) # check everything is ok with dirs and empty files

	os.system('mkdir bck ; mv LMAKE *auto* bck')
	os.system('find CACHE -type f -ls')

	print('hello2',file=open('hello','w'))
	ut.lmake( 'hello+auto1.hide' , done=1     , hit_done=2 , new=1 ) # check cache hit on common part, and miss when we depend on hello
	ut.lmake( 'mkdir.dut'        , unlinked=1 , hit_done=1         ) # check everything is ok with dirs and empty files (mkdir.dut still exists and is unlinked)

