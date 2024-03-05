# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake
	from lmake       import multi_strip
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step'
	)

	class Star(Rule) :
		targets = { 'DST' : r'a{*:\d}' }
		dep     = 'step'
		cmd = multi_strip('''
			echo good > a1
			[ -e a2 ]
			ln a1 a2
			rm a1
		''')

	class Cpy(Rule) :
		target = 'cpy'
		dep    = 'a2'
		def cmd() :
			assert sys.stdin.read().strip()=='good'

else :

	import ut

	print(1,file=open('step','w'))

	open('a2','w').write('bad1')

	ut.lmake( 'cpy' , rerun=1 , done=2 , new=1 )

	open('a2','w').write('bad1')

	ut.lmake( 'cpy' )

	print(2,file=open('step','w'))

	ut.lmake( 'cpy' , changed=1 , steady=1 )
