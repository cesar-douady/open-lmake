# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
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
		targets = { 'LNK' : r'{File:.*}.lnk' }
		cmd     = 'ln -s {File} {LNK}'

	class Cat(Rule) :
		target = r'hello+{File:.*}'
		cmd    = 'cat hello.lnk {File}'

	class Abc(Rule) :
		targets = { 'ABC' : 'a/b/c' }
		cmd     = 'ln -s ../../d/e {ABC}'

	class De(Rule) :
		targets = { 'DE' : 'd/e' }
		cmd     = 'echo abc > a/b/c'

	class A(Rule) :
		targets = { 'A' : 'a2' }
		cmd     = 'ln -s b2 {A}'

	class B(Rule) :
		targets = { 'B' : 'b2' }
		cmd     = 'echo a2 > a2'

else :

	import os

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world' , may_rerun=1 , done=2 , new=2 ) # check link is distinguished from non-existent

	ut.lmake( 'd/e' , may_rerun=1 , done=2 ) # check we acquire dep to a/b/c although we write to it (a/b/c cant be written up front)
	ut.lmake( 'b2'  , may_rerun=1 , done=2 ) # check we acquire dep to a     although we write to it (a2    can  be written up front)

	ut.lmake( 'a.lnk/b/c' , done  =1 )
	os.unlink('a.lnk')
	ut.lmake( 'a.lnk/b/c' , steady=1 )
