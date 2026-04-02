# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	class Dyn(Rule) :
		target = r'dyn{D:\d}'
		cmd    = 'echo D'

	class Dut(Rule) :
		target            = r'dut{D:\d}'
		cache             = 'my_cache'
		environ           = { 'DYN'           : "{open('dyn'+D).read().strip() if int(D)==1 else ''}" }
		environ_resources = { 'DYN_RSRCS'     : "{open('dyn'+D).read().strip() if int(D)==2 else ''}" }
		environ_ancillary = { 'DYN_ANCILLARY' : "{open('dyn'+D).read().strip() if int(D)==3 else ''}" }
		cmd               = 'echo $DYN'

else :

	import os
	import stat
	import textwrap

	import ut

	os.makedirs( 'CACHE/LMAKE' , mode=stat.S_ISGID|stat.S_IRWXU|stat.S_IRWXG )
	print(textwrap.dedent('''
		size = 1<<20
	''')[1:],file=open('CACHE/LMAKE/config.py','w'))

	ut.lmake( 'dut1','dut2','dut3' , early_rerun=2 , done=5 )

	os.system('find CACHE -type f -ls')
	os.system(f'mkdir bck ; mv LMAKE dut* dyn* bck')

	ut.lmake( 'dut1','dut2','dut3' , hit_rerun=2 , hit_done=3 , done=2 )
