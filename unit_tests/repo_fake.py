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
			'MRKR' : ( 'src/{Dir:.*}.repo_dir.trigger'    , 'phony'       )
		,	'TGT'  : ( 'src/{Dir   }.repo_dir/{File*:.*}' , 'incremental' )
		}
		cmd = '''
			[ "$(cat {TGT('a')} 2>/dev/null)" = a ] && exit
			mkdir -p {TGT('')}
			echo a > {TGT('a')}
			echo b > {TGT('b.zip')}
		'''

	class Unzip(Rule) :
		targets = {
			'MRKR' : ( '{Dir:.*}.zip_dir.trigger'    , 'phony' )
		,	'TGT'  :   '{Dir:.*}.zip_dir/{File*:.*}'
		}
		dep = '{Dir}.zip'
		cmd = '''
			cat >{TGT('c.c')}
		'''

	class Cc(Rule) :
		target = 'obj/{Dir:.*}.repo_dir/{File:.*}.o'
		deps =  {
			'SRC'  : 'src/{Dir}.repo_dir.trigger'
		,	'MRKR' : 'src/{Dir}.repo_dir/b.zip_dir.trigger'
		,	'TRIG' : 'trig'
		}
		cmd = '''
			cat trig src/{Dir}.repo_dir/b.zip_dir/{File}.c
		'''


else :

	import ut,os

	print('1',file=open('trig','w'))
	ut.lmake( 'src/a.repo_dir.trigger' ,                    done=1             )
	ut.lmake( 'obj/a.repo_dir/c.o'     , new=1 , steady=1 , done=2 , rerun=... ) # Cc may be rerun if .c dep is seen hot (too recent to be reliable)

	os.system('''
		set -x
		rm -rf src obj
		lforget src/a.repo_dir.trigger
		lforget -d src/a.repo_dir.trigger
		lforget obj/a.repo_dir/c.o
		lforget -d obj/a.repo_dir/c.o
	''')

	print('2',file=open('trig','w'))
	ut.lmake( 'src/a.repo_dir.trigger' ,             steady=1                                   )
	ut.lmake( 'obj/a.repo_dir/c.o'     , changed=1 , steady=2 , may_rerun=1 , done=1 ,rerun=... ) # Cc may be rerun if .c dep is seen hot (too recent to be reliable)
