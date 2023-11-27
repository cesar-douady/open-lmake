# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	)

	class Cpy(Rule) :
		target = 'cpy'
		dep    = 'src'
		cmd    = 'cat'

	class Lnk(Rule) :
		targets = { 'DST':'lnk' }
		deps    = { 'SRC':'cpy' }
		cmd     = 'ln {SRC} {DST}'

	class Chk(PyRule) :
		target = 'chk'
		deps   = {
			'DUT' : 'lnk'
		,	'REF' : 'src'
		}
		cmd = 'diff {DUT} {REF} >&2'

else :

	import ut

	print('hello',file=open('src','w'))

	ut.lmake( 'chk' , done=3 , new=1 )                                         # check targets are out of date
	ut.lmake( 'chk' , done=0 , new=0 )                                         # check targets are up to date

	print('hello2',file=open('src','w'))

	ut.lmake( 'chk' , done=2 , steady=1 , changed=1 )                          # check targets are out of date
	ut.lmake( 'chk' , done=0                        )                          # check targets are up to date
