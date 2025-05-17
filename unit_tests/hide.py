# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	)

	class Auto(Rule) :
		target = r'auto{Digit:\d}'
		cmd    = "echo '#auto'{Digit}"

	class Hide(Rule) :
		target    = r'{File:.*}.hide'
		stderr_ok = True
		cmd       = 'cat {File} || :'

	class Cat(Rule) :
		prio = 1
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		cmd = 'cat {FIRST} {SECOND}'

else :

	import ut

	print('hello',file=open('hello','w'))

	ut.lmake( 'hello+auto1.hide' , done=3 , may_rerun=1 , new=1 ) # check target is out of date
	ut.lmake( 'hello+auto1.hide' , done=0 ,               new=0 ) # check target is up to date
