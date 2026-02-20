# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import ut

if __name__!='__main__' :

	import sys
	import time

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'ut.py'
	,	'../codec_files/'
	)

	class CodecSh(Rule) :
		target = r'{File:.*}_sh'
		shell  = ('/bin/bash','-e')
		autodep='ld_preload'
		cmd    = '''
			dir_code=$( echo {File}_sh | lencode -t ../codec_files/sub/ -x ctx -l 4 )
			echo $dir_code
			ldecode -t ../codec_files/sub/ -x ctx -c $dir_code
			lcheck_deps
		'''

	class CodecPy(PyRule) :
		target = r'{File:.*}_py'
		def cmd() :
			dir_code = lmake.encode( '../codec_files/sub/' , 'ctx' , File+'_py\n' , 3 )
			print(dir_code)
			print(lmake.decode('../codec_files/sub/','ctx',dir_code),end='')
			lmake.check_deps()

	class CodecPy1(PyRule) :
		target = r'{File:.*}_py1'
		def cmd() :
			ut.wait_sync(0)
			dir_code = lmake.encode( '../codec_files/sub/' , 'ctx' , File+'_py\n' , 3 )
			print(dir_code)
			print(lmake.decode('../codec_files/sub/','ctx',dir_code),end='')
			ut.trigger_sync(1)
			lmake.check_deps()

	class CodecPy2(PyRule) :
		target = r'{File:.*}_py2'
		def cmd() :
			dir_code = lmake.encode( '../codec_files/sub/' , 'ctx' , File+'_py\n' , 3 )
			print(dir_code)
			print(lmake.decode('../codec_files/sub/','ctx',dir_code),end='')
			ut.trigger_sync(0)
			ut.wait_sync   (1)
			time.sleep(1)      # ensure CodecPy1 job has had time to report
			lmake.check_deps()

	class Chk(PyRule) :
		target = r'{File:.*}.ok'
		dep    = '{File}'
		def cmd() :
			l = sys.stdin.read().split('\n')
			assert l[1]==File,f'{l[1]!r} != {File!r}'

	class Chk2(PyRule) :
		target = r'chk2'
		def cmd() :
			v = lmake.decode('../codec_files/sub/','ctx','user_code')
			assert v=='user_val\n',f'bad user_val : {v}'

else :

	import os

	os.makedirs('codec_files/sub',exist_ok=True)
	os.makedirs('repo/LMAKE'     ,exist_ok=True)
	os.symlink('../Lmakefile.py','repo/Lmakefile.py')
	os.chdir('repo')

	ut.mk_syncs(2)

	ut.lmake( 'codec_py1'   , 'codec_py2'   , new=1 , done=2 )
	ut.lmake( 'codec_sh.ok' , 'codec_py.ok' ,         done=4 )
	ut.lmake( 'codec_sh.ok' , 'codec_py.ok'                  )

	os.unlink('codec_sh')
	os.unlink('codec_py')

	ut.lmake( 'codec_sh' , 'codec_py' , steady=2 )

	print('user_val',file=open('../codec_files/sub/tab/ctx/user_code.decode','w'))
	assert os.system('lcodec_repair -fr ../codec_files/sub')==0

	ut.lmake( 'chk2' , done=1 )
