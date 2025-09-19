# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule

	lmake.config.caches.dir = {
		'tag'  : 'dir'
	,	'dir'  : lmake.repo_root+'/CACHE'
	,	'perm' : 'group'
	}

	lmake.manifest = (
		'Lmakefile.py'
	,	'codec_file'
	)

	class Dut(Rule) :
		cache  = 'dir'
		target = r'dut{C:\d}{V:\d}'
		cmd    = '''
			code="$( echo -n val{V} | lencode -f codec_file -x ctx )" ; [ "$code" = code{C} ] || echo bad code "$code" versus code{C} >&2
			val="$(  ldecode -f codec_file -x ctx -c code{C}       )" ; [ "$val"  = val{V}  ] || echo bad val  "$val"  versus val{C}  >&2
		'''

else :

	import os

	import ut

	os.makedirs('CACHE/LMAKE',exist_ok=True)
	print('1M',file=open('CACHE/LMAKE/size','w'))

	open('codec_file','w').write(
		' ctx code2 val2\n'       # lines are out of order to generate refresh line
	+	' ctx code1 val1\n'
	)

	ut.lmake( 'dut11' , 'dut22' , reformat=1 , new=1 , done=2 )

	os.rename('LMAKE','LMAKE.bck1')

	ut.lmake( 'dut11' , 'dut22' , unlinked=2 , new=1 , hit_done=2 )

	os.rename('LMAKE','LMAKE.bck2')
	open('codec_file','w')

	ut.lmake( 'dut11' , 'dut22' , unlinked=2 , new=1 , hit_done=2 )

	os.rename('LMAKE','LMAKE.bck3')
	open('codec_file','w').write(
		' ctx code1 val2\n'
	+	'\n'                                                                         # force refresh line
	)
	ut.lmake( 'dut11' , 'dut22' , reformat=1 , unlinked=2 , new=1 , failed=2 , rc=1 ) # check we do not use old codec entries from cache

