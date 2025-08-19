# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys
	import os

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	file = 'a/b/c'
	class Tar(Rule) :
		targets = { 'TAR' : r'hello.tar{*:.*}' }
		environ = { 'REPO_ROOT' : '$REPO_ROOT' }
		cmd = '''
			cd $TMPDIR
			mkdir -p $(dirname {file})
			echo yes >{file}
			tar cf $REPO_ROOT/hello.tar {file}
		'''
	class Untar(PyRule) :
		targets = {
			'TARGET' : r'{File:.*}.tardir/{*:.*}'
		,	'PROTO'  : r'{File:.*}.proto'
		}
		deps = { 'TAR' : '{File}.tar' }
		def cmd() :
			print(TARGET('<proto>'),file=open(PROTO,'w'))
			os.system(f'tar mxaf {TAR} -C {File}.tardir')

	class Cpy(PyRule) :
		target = 'cpy'
		dep    = f'hello.tardir/{file}'
		def cmd() :
			assert open('hello.proto').read()=='hello.tardir/<proto>\n'
			print(sys.stdin.read())

else :

	import ut

	ut.lmake( 'cpy' , done=3 )
