# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Bad(Rule) :
		target = 'bad'
		cach   = 'my_cache'
		dep    = 'src'
		cmd    = 'exit 1'

	class Dut(Rule) :
		target = 'dut'
		cmd    = 'grep read_bad src && cat bad ; exit 0'

else :

	import ut

	print('read_bad1',file=open('src','w'))
	ut.lmake( 'dut' , new=1 , may_rerun=1 , failed=1 , was_dep_error=1 , rc=1 ) # check targets are out of date

	print('read_bad2',file=open('src','w'))
	ut.lmake( 'dut' , changed=1 , failed=1 , was_failed=1 , dep_error=1 , rc=1 ) # failed : when bad is computed speculatively, was_failed : when bad is finally known to be actually needed

	print('3',file=open('src','w'))
	ut.lmake( 'dut' , changed=1 , failed=1 , done=1 )
