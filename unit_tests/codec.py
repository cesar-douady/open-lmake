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
	,	'codec_file'
	)

	class CodecSh(Rule) :
		target = r'{File:.*}_sh'
		shell  = ('/bin/bash','-e')
		cmd    = '''
			code=$( echo {File}_sh | lencode -f codec_file -x ctx -l 4 )
			echo $code
			ldecode -f codec_file -x ctx -c $code
			lcheck_deps
		'''

	class CodecPy(PyRule) :
		target = r'{File:.*}_py'
		def cmd() :
			code = lmake.encode( f'{os.getcwd()}/codec_file' , 'ctx' , File+'_py\n' , 3 ) # check absolute paths work
			print(code)
			print(lmake.decode('codec_file','ctx',code))
			lmake.check_deps()

	class Chk(PyRule) :
		target = r'{File:.*}.ok'
		dep    = '{File}'
		def cmd() :
			l = sys.stdin.read().split('\n')
			assert l[1]==File,f'{l[1]!r} != {File!r}'

else :

	import ut

	print('',file=open('codec_file','w'))

	ut.lmake( 'codec_sh.ok' , 'codec_py.ok' , new=1 , reformat=1 , done=4 , update=1 ) # suppress empty line in codec_file
	ut.lmake( 'codec_sh.ok' , 'codec_py.ok'                                          )

	os.unlink('codec_sh')
	os.unlink('codec_py')

	cnt = ut.lmake( 'codec_sh' , 'codec_py' , steady=2 )

	print(file=open('codec_file','a'))
	ut.lmake( 'codec_sh' , 'codec_py' , changed=1 , reformat=1 )

	print('\tpy\tctx\tcodec_py\\n',file=open('codec_file','a'))
	ut.lmake( 'codec_sh' , 'codec_py' , changed=1 , reformat=1 , done=1 )

	assert os.system("ldebug codec_sh")==0 # ensure lencode/ldecode is compatible with ldebug
