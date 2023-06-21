# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'hello'
	)

	lmake.config.caches.dir = {
		'tag'  : 'dir'
	,	'repo' : lmake.root_dir
	,	'dir'  : lmake.root_dir+'/CACHE'
	,	'size' : 1_000_000_000
	}

	class Auto(lmake.Rule) :
		target = r'auto{Digit:\d}'
		cache  = 'dir'
		cmd    = "echo '#auto'$Digit"

	class Hide(lmake.Rule) :
		target       = r'{File:.*}.hide'
		allow_stderr = True
		cache        = 'dir'
		cmd          = 'cat $File || :'

	class Cat(lmake.Rule) :
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
		cmd   = 'cat $FIRST $SECOND'

else :

	import ut

	print('hello',file=open('hello','w'))

	os.makedirs('CACHE')

	# XXX when cache is operational, add tests for cached accesses
	ut.lmake( 'hello+auto1.hide' , done=3 , may_rerun=1 , new=1 )              # check target is out of date
	ut.lmake( 'hello+auto1.hide' , done=0 ,               new=0 )              # check target is up to date

	os.system('rm -rf LMAKE *auto*')

	print('hello2',file=open('hello','w'))
	ut.lmake( 'hello+auto1.hide' , done=1 , hit_done=2 , new=1 )              # check cache hit on common part, and miss when we depend on hello
