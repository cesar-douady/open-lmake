# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

tmp_views = (None,'tmp','tmp2/sub')

if __name__!='__main__' :

	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'tmp_map_ref'
	)

	for lmake_view in (None,'lmake') :
		for repo_view in (None,'repo') :
			for tmp_view in tmp_views :
				class Dut(Rule) :
					name      = f'dut {lmake_view} {repo_view} {tmp_view}'
					target    = f'dut.{lmake_view}.{repo_view}.{tmp_view}'
					resources = {}
					if lmake_view :
						lmake_view = '/'+lmake_view
					if repo_view :
						repo_view = '/'+repo_view
					if tmp_view :
						tmp_view         = '/'+tmp_view
						resources['tmp'] = '100M'
					cmd = '''
						unset PWD                       # ensure pwd calls getcwd
						type -p lmake >  $TMPDIR/stdout
						pwd           >> $TMPDIR/stdout
						echo $TMPDIR  >> $TMPDIR/stdout
						cat $TMPDIR/stdout
					'''
					if lmake_view : cmd += f'[ $(type -p lmake) = {lmake_view}/bin/lmake ] || exit 1\n'
					if repo_view  : cmd += f'[ $(pwd)           = {repo_view }           ] || exit 1\n'
					if tmp_view   : cmd += f'[ $TMPDIR          = {tmp_view  }           ] || exit 1\n'

	class TmpMap(Rule) :
		target   = 'tmp_map_dut'
		tmp_view = '/tmp'
		views    = { '/tmp/merged/' : { 'upper':'/tmp/upper/' , 'lower':'/tmp/lower/' } }
		cmd = '''
			echo lower > /tmp/lower/x
			echo upper > /tmp/merged/x
			cat /tmp/lower/x
			cat /tmp/upper/x
		'''

	class TmpMapTest(Rule) :
		target = 'tmp_map_test'
		deps   = {
			'DUT' : 'tmp_map_dut'
		,	'REF' : 'tmp_map_ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import ut

	print('lower\nupper',file=open('tmp_map_ref','w'))

	ut.lmake( *(f'dut.{l}.{r}.{t}' for l in (None,'lmake') for r in (None,'repo') for t in tmp_views ) ,         done=12 )
	ut.lmake( 'tmp_map_test'                                                                           , new=1 , done=2  )
