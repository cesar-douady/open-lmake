# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,TraceRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'trig'
	)

	class Clone(TraceRule) :
		force = True
		targets = {
			'MRKR' : ( r'src/{Dir:.*}.repo'             , 'incremental' )
		,	'TGT'  : (  'src/{Dir}.repo_dir/{File*:.*}' , 'incremental' )
		}
		cmd = '''
			[ -f {MRKR} -a "$(cat {MRKR} 2>/dev/null)" = a ] && exit
			mkdir -p {TGT('')}
			echo a > {MRKR}
			echo b > {TGT('b.zip')}
		'''

	class Unzip(Rule) :
		targets = {
			'MRKR' : ( r'src/{Dir:.*}.unzip' , 'phony' )
		,	'TGT'  :    'src/{Dir}.zip_dir/{File*:.*}'
		}
		dep = 'src/{Dir}.zip'
		cmd = '''
			cat  > {TGT('c.c')}
		'''

	class Cc(Rule) :
		target = r'obj/{Dir:.*}.zip_dir/{File:.*}.o'
		deps =  {
			'MRKR' : 'src/{Dir}.unzip'
		,	'TRIG' : 'trig'
		}
		cmd = 'cat trig src/{Dir}.zip_dir/{File}.c'


else :

	import ut

	print('1',file=open('trig','w'))
	ut.lmake( 'src/a.repo'                   ,                    done=1             )
	ut.lmake( 'obj/a.repo_dir/b.zip_dir/c.o' , new=1 , steady=1 , done=2 , rerun=... ) # Cc may be rerun if .c dep is seen hot (too recent to be reliable)

	print('2',file=open('trig','w'))
	ut.lmake( 'obj/a.repo_dir/b.zip_dir/c.o' , changed=1 , steady=1 , done=1 )
