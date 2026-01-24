
# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'../codec_files/'
	,	'codec_file'
	)

	lmake.config.codecs.src_codec = 'codec_file'
	lmake.config.codecs.dir_codec = '../codec_files/tab/'

	class CodecSh(Rule) :
		target = r'{File:.*}_sh'
		shell  = ('/bin/bash','-e')
		autodep='ld_preload'
		cmd    = '''
			src_code=$( echo {File}_sh | lencode -t src_codec -x ctx -l 4 )
			dir_code=$( echo {File}_sh | lencode -t dir_codec -x ctx -l 4 )
			echo $src_code
			echo $dir_code
			ldecode -t src_codec -x ctx -c $src_code
			ldecode -t dir_codec -x ctx -c $dir_code
			lcheck_deps
		'''

	class CodecPy(PyRule) :
		target = r'{File:.*}_py'
		def cmd() :
			src_code = lmake.encode( 'src_codec' , 'ctx' , File+'_py\n' , 3 )
			dir_code = lmake.encode( 'dir_codec' , 'ctx' , File+'_py\n' , 3 )
			print(src_code)
			print(dir_code)
			print(lmake.decode('src_codec','ctx',src_code),end='')
			print(lmake.decode('dir_codec','ctx',dir_code),end='')
			lmake.check_deps()

	class Chk(PyRule) :
		target = r'{File:.*}.ok'
		dep    = '{File}'
		def cmd() :
			l = sys.stdin.read().split('\n')
			assert l[2]==File,f'{l[2]!r} != {File!r}'
			assert l[3]==File,f'{l[3]!r} != {File!r}'

else :

	import ut

	os.makedirs('codec_files/tab/LMAKE',exist_ok=True)
	print('',file=open('codec_files/tab/LMAKE/config.py','w'))

	os.makedirs('repo/LMAKE'       ,exist_ok=True)
	os.symlink('../Lmakefile.py','repo/Lmakefile.py')
	os.chdir('repo')

	print('',file=open('codec_file','w'))

	ut.lmake( 'codec_sh.ok' , 'codec_py.ok' , new=1 , reformat=1 , done=4 , update=1 )
	ut.lmake( 'codec_sh.ok' , 'codec_py.ok'                                          )

	os.unlink('codec_sh')
	os.unlink('codec_py')

	ut.lmake( 'codec_sh' , 'codec_py' , steady=2 )
