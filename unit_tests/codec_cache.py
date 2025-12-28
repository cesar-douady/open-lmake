# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule

	from step import cache_tag

	if cache_tag : lmake.config.caches.my_cache = { 'tag':cache_tag , 'dir':lmake.repo_root+'/CACHE' }
	else         : lmake.config.caches.my_cache = {                   'dir':lmake.repo_root+'/CACHE' } # defaults to tag='daemon'

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'codec_file'
	)

	class Dut(Rule) :
		cache  = 'my_cache'
		target = r'dut{C:\d}{V:\d}'
		cmd    = '''
			code="$( echo -n val{V} | lencode -f codec_file -x ctx )" ; [ "$code" = code{C} ] || echo bad code "$code" versus code{C} >&2
			val="$(  ldecode -f codec_file -x ctx -c code{C}       )" ; [ "$val"  = val{V}  ] || echo bad val  "$val"  versus val{C}  >&2
		'''

else :

	import os
	import textwrap

	import ut

	for cache_tag in ('dir','') :
		print(f'cache_tag={cache_tag!r}',file=open('step.py','w'))
		bck = f"bck{'_' if cache_tag else ''}{cache_tag}"

		os.makedirs('CACHE/LMAKE',exist_ok=True)
		print(textwrap.dedent('''
			size = 1<<20
			perm = 'group'
		''')[1:],file=open('CACHE/LMAKE/config.py','w'))

		open('codec_file','w').write(
			'\tcode2\tctx\tval2\n'    # lines are out of order to generate refresh line
		+	'\tcode1\tctx\tval1\n'
		)

		ut.lmake( 'dut11' , 'dut22' , reformat=1 , new=1 , done=2 )

		os.system(f'mkdir {bck}_1 ; mv LMAKE {bck}_1')

		ut.lmake( 'dut11' , 'dut22' , unlinked=2 , hit_rerun=2 , new=1 , expand=1 , hit_done=2 )

		os.system(f'mkdir {bck}_2 ; mv LMAKE {bck}_2')
		open('codec_file','w')
		ut.lmake( 'dut11' , 'dut22' , unlinked=2 , hit_rerun=2 , new=1 , expand=1 , failed=2 , update=1 , rc=1 )

		os.system(f'mkdir {bck}_3 ; mv LMAKE {bck}_3')
		open('codec_file','w').write(
			'\tcode1\tctx\tval1\n'
		+	'\n'                                                                                                                # force refresh
		)
		ut.lmake( 'dut11' , 'dut22' , unlinked=2 , hit_rerun=2 , new=1 , reformat=1 , hit_done=1 , failed=1 , update=1 , rc=1 ) # check we do not use old codec entries from cache

		os.system(f'mkdir {bck}_4 ; mv LMAKE {bck}_4')
		open('codec_file','w').write(
			'\tcode1\tctx\tval2\n'
		)
		ut.lmake( 'dut11' , 'dut22' , unlinked=2 , hit_rerun=2 , new=1 , expand=1 , failed=2 , update=1 , rc=1 )

		os.system(f'mkdir {bck}_5 ; mv LMAKE {bck}_5')
		open('codec_file','w').write(
			'\tcode1\tctx\tval1\n'
		+	'\tcode2\tctx\tval2\n'
		)
		ut.lmake( 'dut11' , 'dut22' , unlinked=2 , hit_rerun=2 , new=1 , expand=1 , hit_done=2 )

		os.system(f'mkdir {bck}_6 ; mv CACHE LMAKE dut* {bck}_6')
