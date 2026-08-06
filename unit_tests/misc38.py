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
	)

	lmake.config.codecs.dir_codec = '../codec_files/'

	class CodecSh(Rule) :
		target = 'dut'
		cmd = '''
			dir_code=$( echo dut | lencode -t dir_codec -x ctx )
			echo $dir_code
			ldecode -t dir_codec -x ctx -c $dir_code
		'''

else :

	import ut

	os.makedirs('codec_files/sub',exist_ok=True)
	os.makedirs('repo/LMAKE'     ,exist_ok=True)
	os.symlink('../Lmakefile.py','repo/Lmakefile.py')
	os.chdir('repo')

	ut.lmake( 'dut' , done=1 )

	assert os.system('ldebug -kn dut')==0 # check does not hang
