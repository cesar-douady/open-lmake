# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	class Dut(Rule) :
		targets = { 'TARGET' : ( 'dut' , 'incremental' ) }
		deps    = { 'DEP'    :   'src'                   }
		cache   = 'my_cache'
		cmd     = 'cat {DEP} >{TARGET}'

else :

	import os
	import stat
	import textwrap

	import ut

	# cache my_cache must be writable by all users having access to the cache
	# use setfacl(1) with adequate rights in the default ACL, e.g. :
	# os.system('setfacl -m d:g::rw,d:o::r CACHE')
	os.makedirs( 'CACHE/LMAKE' , mode=stat.S_ISGID|stat.S_IRWXU|stat.S_IRWXG )
	print( 'size = 1<<20' , file=open('CACHE/LMAKE/config.py','w') )

	print( 'v1' , file= open('src','w') )

	ut.lmake( 'dut' , new=1 , done=1 )             # dut is not actually built incrementally
	#
	os.system(f'mkdir bck_1 ; mv LMAKE dut bck_1')
	ut.lmake( 'dut' , new=1 , hit_done=1 )
	#
	os.system(f'mkdir bck_2 ; mv LMAKE dut bck_2')
	ut.lmake( '-I' , 'dut' , new=1 , hit_done=1 )  # dut was not actually built incrementally
	#
	print( 'v2' , file= open('src','w') )
	ut.lmake( 'dut' , changed=1 , done=1 )         # dut is actually built incrementally
	#
	os.system(f'mkdir bck_3 ; mv LMAKE dut bck_3')
	ut.lmake( 'dut' , new=1 , hit_done=1 )
	#
	os.system(f'mkdir bck_4 ; mv LMAKE dut bck_4')
	ut.lmake( '-I' , 'dut' , new=1 , bad_cache_download=1 , done=1 )              # dut was actually built incrementally
