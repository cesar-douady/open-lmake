# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os.path as osp

	from step import step

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	if step==2 : lmake.config.link_support = 'full_ext' # symlinks in $TMPDIR are then supported

	for sfx in ('','_lnk') :
		class Dut(Rule) :
			name    = 'Dut'+sfx
			targets = { 'DUT' : 'dut'+sfx }
			environ_resources = {
				'REPO_ROOT' : '$REPO_ROOT'
			,	'TMPDIR'    : osp.dirname(lmake.repo_root)+'/tmp'+sfx
			}
			cmd = '''
				ln -s $REPO_ROOT/{DUT} $TMPDIR/dut
				echo >$TMPDIR/dut
			'''

else :

	import os
	import shutil

	import ut

	os.makedirs('tmp'       ,exist_ok=True)
	os.makedirs('ext'       ,exist_ok=True)
	os.makedirs('repo/LMAKE',exist_ok=True)
	os.symlink('tmp','tmp_lnk')
	open('repo/Lmakefile.py','w').write(open('Lmakefile.py').read())
	os.chdir('repo')

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , 'dut_lnk' , done=1 , failed=1 , rc=1 ) # tmp dir cannot contain symlinks

	shutil.rmtree('LMAKE')
	os.unlink('dut')
	os.mkdir('LMAKE')

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'dut' , 'dut_lnk' , done=2 )   # tmp dir can contain symlinks when link_support==full_ext
