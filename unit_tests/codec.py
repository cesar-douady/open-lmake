# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

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
			code = lmake.encode('codec_file','ctx',File+'_py\n',3)
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

	import os

	import ut

	print('',file=open('codec_file','w'))

	ut.lmake      ( 'codec_sh.ok' , 'codec_py.ok' , reformat=1   , new=1 , done=4 )
	cnt = ut.lmake( 'codec_sh'    , 'codec_py.ok' , reformat=... , changed=...    )
	assert cnt.reformat in (0,1) and cnt.changed in (0,1)                           # depend on job order and crc details

	os.unlink('codec_sh')
	os.unlink('codec_py')

	cnt = ut.lmake( 'codec_sh' , 'codec_py' , changed=... , steady=2 )
	assert cnt.changed in (0,1)                                        # depend on previous job order and crc details

	print(file=open('codec_file','a'))
	ut.lmake( 'codec_sh' , 'codec_py' , reformat=1 , changed=1 )

	print(r' py ctx codec_py\n',file=open('codec_file','a'))
	ut.lmake( 'codec_sh' , 'codec_py' , reformat=1 , changed=... , done=1 ) # changed may be 1 or 2, its ok

	assert os.system("ldebug codec_sh")==0 # ensure lencode/ldecode is compatible with ldebug
