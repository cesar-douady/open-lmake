# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake       import multi_strip
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'cp_dut.ref'
	,	'exec_dut.ref'
	,	'lnk_dut.ref'
	,	'touch_dut.ref'
	)

	from step import autodep

	class TmpRule(Rule) :
		tmp     = '/tmp'
		autodep = autodep
		cmd     = f'#{autodep}' # force cmd modification

	class Src(Rule) :
		target = 'src'
		cmd    = 'echo src_content'

	class Chk(Rule) :
		target = '{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT} >&2'

	class Lnk(TmpRule) :
		target       = 'lnk_dut'
		side_targets = { 'SIDE' : 'lnk_dut.tmp' }
		# make a chain of links with all potential cases
		cmd = multi_strip('''
			ln -s /tmp/a lnk_dut.tmp             # link from repo to tmp
			cd /tmp
			ln -s b             a                # relative link within tmp
			ln -s /tmp/c        b                # absolute link within tmp
			ln -s $ROOT_DIR/src c                # link from tmp to repo
			readlink a b c $ROOT_DIR/lnk_dut.tmp
			cd $ROOT_DIR
			readlink /tmp/a /tmp/b /tmp/c lnk_dut.tmp
			cat lnk_dut.tmp
		''')

	class Cp(TmpRule) :
		target       = 'cp_dut'
		side_targets = { 'SIDE' : 'cp_dut.tmp' }
		# make a chain of copies with all potential cases
		cmd = multi_strip('''
			cp $ROOT_DIR/src /tmp/c
			cd /tmp
			cp /tmp/c        b
			cp b             a
			cd $ROOT_DIR
			cp /tmp/a cp_dut.tmp
			cat cp_dut.tmp
		''')

	class Touch(TmpRule) :
		target       = 'touch_dut'
		side_targets = { 'SIDE' : 'touch_dut.tmp' }
		cmd = multi_strip('''
			cd /tmp
			mkdir d
			cp $ROOT_DIR/src d/a
			cd d
			pwd > $ROOT_DIR/{SIDE}
			mv a b
			sleep 0.1 # ensure a and b have different dates
			cp b c
			ls -l --full-time c > date1
			touch -r /tmp/d/b /tmp/d/c
			ls -l --full-time c > date2
			cmp date1 date2 >/dev/null && {{ echo 'touch did not change date' >&2 ; exit 1 ; }}
			cd ..
			cat d/c
			rm -r d
			[ -f d/c ] && {{ echo 'rm did not rm' >&2 ; exit 1 ; }}
			cd $ROOT_DIR
			cat {SIDE}
		''')

	class Exec(TmpRule) :
		target       = 'exec_dut'
		side_targets = { 'SIDE' : 'exec_dut.tmp' }
		cmd = multi_strip('''
			PATH=/tmp:$PATH
			echo '#!/bin/cat'      >  /tmp/dut_exe #ensure there is an execve as bash optimizes cases where it calls itself
			echo 'dut_exe_content' >> /tmp/dut_exe
			chmod +x /tmp/dut_exe
			dut_exe > exec_dut.tmp
			cat exec_dut.tmp
		''')

else :

	import ut

	with open('lnk_dut.ref','w') as f :
		for p in range(2) :
			print('b'                    ,file=f)
			print('/tmp/c'               ,file=f)
			print(f'{lmake.root_dir}/src',file=f)
			print('/tmp/a'               ,file=f)
		print('src_content',file=f)

	with open('cp_dut.ref','w') as f :
		print('src_content',file=f)

	with open('touch_dut.ref','w') as f :
		print('src_content',file=f)
		print('/tmp/d'     ,file=f)

	with open('exec_dut.ref','w') as f :
		print('#!/bin/cat'     ,file=f)
		print('dut_exe_content',file=f)

	print("autodep='ld_preload'",file=open('step.py','w'))
	res = ut.lmake( 'lnk_dut.ok' , 'cp_dut.ok' , 'touch_dut.ok' , 'exec_dut.ok' , new=4 , may_rerun=... , rerun=... , done=... , steady=... )
	assert 1<=res['may_rerun']+res['rerun'] and res['may_rerun']+res['rerun']<=4
	assert res['done']+res['steady']==9

	if lmake.has_ld_audit :
		print("autodep='ld_audit'",file=open('step.py','w'))
		res = ut.lmake( 'lnk_dut.ok' , 'cp_dut.ok' , 'touch_dut.ok' , 'exec_dut.ok' , new=0 , steady=4 )
