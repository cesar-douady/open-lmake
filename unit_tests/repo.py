# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,TraceRule

	import gxx

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep.h'
	,	'gxx.py'
	)

	class Clone(TraceRule) :
		force = True
		targets = {
			'MRKR' : ( 'src/{Dir:.*}.repo_dir.trigger'    , 'phony'                      )
		,	'TGT'  : ( 'src/{Dir   }.repo_dir/{File*:.*}' , 'incremental' , 'no_warning' ) # let git manage hard links : it does not support unification
		}
		cmd = '''
			if [ -f {TGT('.git/index')} ]
			then ( cd {TGT('')} ; git pull 2>&1 )
			else   git clone LMAKE/{Dir}.repo {TGT('')} 2>&1
			fi
		'''

	class Unzip(Rule) :
		targets = {
			'MRKR' : ( '{Dir:.*}.zip_dir.trigger'    , 'phony' )
		,	'TGT'  :   '{Dir   }.zip_dir/{File*:.*}'
		}
		deps = { 'ZIP' : '{Dir}.zip' }
		cmd = '''
			unzip {ZIP} -d {TGT('')}
		'''

	class Cc(Rule) :
		targets = { 'EXE' : 'exe/{File:.*}'   }
		deps    = { 'SRC' : 'src/{File   }.c' }
		cmd = f'''
			{gxx.gxx} -I . -o {{EXE}} {{SRC}}
		'''


else :

	import os
	import shutil

	if not shutil.which('zip') :
		print('zip not available',file=open('skipped','w'))
		exit()

	import ut

	ut.mk_gxx_module('gxx')

	print('''
		#include <stdio.h>
		#include "dep.h"
		int main() {
			printf("dep=%s\\n",dep) ;
		}
	''',file=open('c.c','w'))

	os.system('''
		mkdir -p LMAKE/a.repo
		zip LMAKE/a.repo/b.zip c.c ; rm c.c
		cd LMAKE/a.repo
		git init .      # -b main is not supported on older git's
		git add b.zip
		git commit -minit
	''')

	print('const char* dep = "my_dep" ;',file=open('dep.h','w'))

	ut.lmake( 'exe/a.repo_dir/b.zip_dir/c' , new=1 , done=3 , rerun=... ) # Cc may be rerun if .c dep is seen hot (too recent to be reliable)
	ut.lmake( 'exe/a.repo_dir/b.zip_dir/c' ,         done=1             ) # git pull creates a .git/ORIG_HEAD file

	os.system('''
		set -x
		rm -rf src exe
		lforget src/a.repo_dir.trigger
		lforget -d src/a.repo_dir.trigger
		lforget exe/a.repo_dir/b.zip_dir/c
		lforget -d exe/a.repo_dir/b.zip_dir/c
	''')

	ut.lmake( 'exe/a.repo_dir/b.zip_dir/c' , steady=2 , done=1 , rerun=... ) # Cc may be rerun if .c dep is seen hot (too recent to be reliable)

	print('const char* dep = "my_dep2" ;',file=open('dep.h','w'))

	ut.lmake( 'exe/a.repo_dir/b.zip_dir/c' , changed=1 , steady=1 , done=1 )
