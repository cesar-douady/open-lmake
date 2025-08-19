# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

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
		tmp_view = '/tmp'
		autodep  = autodep
		cmd      = f'#{autodep}' # force cmd modification

	class Src(Rule) :
		target = 'src'
		cmd    = 'echo src_content'

	class Chk(Rule) :
		target = r'{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT} >&2'

	class Lnk(TmpRule) :
		target       = 'lnk_dut'
		side_targets = { 'SIDE'      : 'lnk_dut.tmp' }
		environ      = { 'REPO_ROOT' : '$REPO_ROOT'  }
		# make a chain of links with all potential cases
		cmd = '''
			ln -s /tmp/a         lnk_dut.tmp      # link from repo to tmp
			cd /tmp
			ln -s b              a                # relative link within tmp
			ln -s /tmp/c         b                # absolute link within tmp
			ln -s $REPO_ROOT/src c                # link from tmp to repo
			readlink a b c $REPO_ROOT/lnk_dut.tmp
			cd $REPO_ROOT
			readlink /tmp/a /tmp/b /tmp/c lnk_dut.tmp
			cat lnk_dut.tmp
		'''

	class Cp(TmpRule) :
		target       = 'cp_dut'
		side_targets = { 'SIDE'      : 'cp_dut.tmp' }
		environ      = { 'REPO_ROOT' : '$REPO_ROOT' }
		# make a chain of copies with all potential cases
		cmd = '''
			cp src    /tmp/c
			cd /tmp
			cp /tmp/c b
			cp b      a
			cd $REPO_ROOT
			cp /tmp/a cp_dut.tmp
			cat cp_dut.tmp
		'''

	class Touch(TmpRule) :
		target       = 'touch_dut'
		side_targets = { 'SIDE'      : 'touch_dut.tmp' }
		environ      = { 'REPO_ROOT' : '$REPO_ROOT'    }
		cmd = '''
			cd /tmp
			mkdir d
			cp $REPO_ROOT/src d/a
			cd d
			pwd > $REPO_ROOT/{SIDE}
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
			cd $REPO_ROOT
			cat {SIDE}
		'''

	class Exec(TmpRule) :
		target       = 'exec_dut'
		side_targets = { 'SIDE' : 'exec_dut.tmp' }
		cmd = '''
			PATH=/tmp:$PATH
			echo '#!/bin/cat'      >  /tmp/dut_exe #ensure there is an execve as bash optimizes cases where it calls itself
			echo 'dut_exe_content' >> /tmp/dut_exe
			chmod +x /tmp/dut_exe
			dut_exe > exec_dut.tmp
			cat exec_dut.tmp
		'''

else :

	import ut

	with open('lnk_dut.ref','w') as f :
		for p in range(2) :
			print('b'                     ,file=f)
			print('/tmp/c'                ,file=f)
			print(f'{lmake.repo_root}/src',file=f)
			print('/tmp/a'                ,file=f)
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
	cnts = ut.lmake( 'lnk_dut.ok' , 'cp_dut.ok' , 'touch_dut.ok' , 'exec_dut.ok' , new=4 , may_rerun=... , rerun=... , done=... , steady=... )
	assert 1<=cnts.may_rerun+cnts.rerun and cnts.may_rerun+cnts.rerun<=4
	assert cnts.done+cnts.steady==9

	if 'ld_audit' in lmake.autodeps :
		print("autodep='ld_audit'",file=open('step.py','w'))
		ut.lmake( 'lnk_dut.ok' , 'cp_dut.ok' , 'touch_dut.ok' , 'exec_dut.ok' , new=0 , steady=4 )
