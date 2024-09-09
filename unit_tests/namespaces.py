# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule
	from lmake       import multi_strip

	lmake.manifest = (
		'Lmakefile.py'
	,	'tmp_map_ref'
	)

	for tmp_view in (None,'/tmp','/new_tmp') :
		for root_view in (None,'/repo') :
			class Dut(Rule) :
				name   = f'dut {tmp_view} {root_view}'
				target = f'dut.{tmp_view}{root_view}'
				if tmp_view  : tmp_view  = tmp_view
				if root_view : root_view = root_view
				cmd = multi_strip('''
					unset PWD                     # ensure pwd calls getcwd
					pwd          > $TMPDIR/stdout
					echo $TMPDIR > $TMPDIR/stdout
					cat $TMPDIR/stdout
				''')
				if tmp_view  : cmd += f'[ $TMPDIR = {tmp_view } ] || exit 1\n'
				if root_view : cmd += f'[ $(pwd)  = {root_view} ] || exit 1\n'

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

	ut.lmake( *(f'dut.{t}{r}' for t in (None,'/tmp','/new_tmp') for r in (None,'/repo') ) ,         done=6 )
	ut.lmake( 'tmp_map_test'                                                              , new=1 , done=2 )
