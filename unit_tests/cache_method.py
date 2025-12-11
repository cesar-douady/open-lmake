# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.caches.dir = {
		'tag' : 'dir'
	,	'dir' : lmake.repo_root+'/CACHE'
	}

	class Stable(Rule) :
		target = r'stable{D:\d+}'
		cache  = 'dir'
		cmd    = "echo 'stable {D}'"

	class Unstable(Rule) :
		target = r'unstable{D:\d+}'
		cache  = 'dir'
		cmd    = "echo 'unstable {D}'; date -Ins"

else :

	import os

	import ut

	# cache dir must be writable by all users having access to the cache
	# use setfacl(1) with adequate rights in the default ACL, e.g. :
	# os.system('setfacl -m d:g::rw,d:o::r CACHE')
	os.makedirs('CACHE/LMAKE')
	print('size=1<<20',file=open('CACHE/LMAKE/config.py','w'))

	ut.lmake( '-cnone'     , 'stable1'  , 'stable2'  , 'stable3'  , 'stable4'  , 'unstable1'  , 'unstable2'  , 'unstable3'  , 'unstable4'  , done=8 )
	ut.lmake( '-cdownload' , 'stable5'  , 'stable6'  , 'stable7'  , 'stable8'  , 'unstable5'  , 'unstable6'  , 'unstable7'  , 'unstable8'  , done=8 )
	ut.lmake( '-cupload'   , 'stable9'  , 'stable10' , 'stable11' , 'stable12' , 'unstable9'  , 'unstable10' , 'unstable11' , 'unstable12' , done=8 )
	ut.lmake( '-cplain'    , 'stable13' , 'stable14' , 'stable15' , 'stable16' , 'unstable13' , 'unstable14' , 'unstable15' , 'unstable16' , done=8 )

	os.system(f'mkdir bck ; mv LMAKE bck ; rm -f *stable*')

	ut.lmake( '-cnone'     , 'stable1' , 'stable5' , 'stable9'  , 'stable13' , 'unstable1' , 'unstable5' , 'unstable9'  , 'unstable13' , hit_done=0 , done=8                     )
	ut.lmake( '-cdownload' , 'stable2' , 'stable6' , 'stable10' , 'stable14' , 'unstable2' , 'unstable6' , 'unstable10' , 'unstable14' , hit_done=4 , done=4                     )
	ut.lmake( '-cupload'   , 'stable3' , 'stable7' , 'stable11' , 'stable15' , 'unstable3' , 'unstable7' , 'unstable11' , 'unstable15' , hit_done=0 , done=8 , no_cache_upload=2 )
	ut.lmake( '-cplain'    , 'stable4' , 'stable8' , 'stable12' , 'stable16' , 'unstable4' , 'unstable8' , 'unstable12' , 'unstable16' , hit_done=4 , done=4                     )
