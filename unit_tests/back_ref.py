# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep'
	)

	class Cat(Rule) :
		targets = { 'TGT'   : r'{*:.*}+{File:.*}+{D*:.*}+{*:.*}+{D*}+{File}' }
		deps    = { 'FIRST' :  '{File}'                                      }
		cmd     = "echo {File} $(cat {FIRST}) >{TGT('single1','double','single2')}"

	class Test(Rule) :
		target = 'test'
		dep    = 'single1+dep+double+single2+double+dep'
		cmd    = '[ "$(cat)" = "dep hello" ]'

else :

	import os
	import os.path as osp

	import ut

	print('hello',file=open('dep','w'))

	ut.lmake( 'test' , done=2 , new=1 )
