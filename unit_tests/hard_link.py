# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Dep(Rule) :
		targets = {
			'TGT1' : ( 'a' , 'incremental' )
		,	'TGT2' : ( 'b' , 'incremental' )
		}
		dep = 'src'
		cmd = '''
			cat > {TGT1}
			[ "$(cat {TGT2} 2>/dev/null)" == "$(cat {TGT1})" ] || ln -f {TGT1} {TGT2}
		'''

	class Dut(Rule) :
		targets = {
			'TGT1' : ( 'c' , 'incremental' )
		,	'TGT2' : ( 'd' , 'incremental' )
		}
		deps = {
			'DEP1' : 'a'
		,	'DEP2' : 'b'
		}
		cmd = '''
			[ "$(cat {TGT1} 2>/dev/null)" == "$(cat {DEP1})" ] || ln -f {DEP1} {TGT1}
			[ "$(cat {TGT2} 2>/dev/null)" == "$(cat {DEP2})" ] || ln -f {DEP2} {TGT2}
		'''

else :

	import os
	import os.path as osp

	import ut

	print('1',file=open('src','w'))

	ut.lmake( 'c' , 'd' , done=2 , new=1 )
	assert os.stat('c').st_nlink==4

	print('2',file=open('src','w'))

	ut.lmake( 'a' , 'b' , done=1 , changed=1 )
	assert os.stat('a').st_nlink==2
	assert os.stat('c').st_nlink==2

	ut.lmake( 'c' , 'd' , done=1 )
	assert os.stat('a').st_nlink==4
