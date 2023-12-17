# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	)

	class Lnk(Rule) :
		targets = { 'LNK' : '{File:.*}.lnk' }
		deps    = { 'SRC' : '{File}'        }
		cmd     = 'ln -s {SRC} {LNK}'

	class Cat(Rule) :
		target = 'hello+{File:.*}'
		cmd    = 'cat hello.lnk {File}'

else :

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world' , may_rerun=1 , done=2 , new=2 ) # check link is distinguished from non-existent
