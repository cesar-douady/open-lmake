# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'ok.opt.ref'
	,	'ko.opt.ref'
	)

	class Opt(Rule) :
		targets = { 'DST' : (r'{File:.*}.opt','Optional') }
		cmd     = '[ {File} != ok ] || echo 1 > {DST}'

	class Opt2(Rule) :
		prio    = -1
		targets = { 'DST' : r'{File:.*}.opt' }
		cmd     = 'echo 2 > {DST}'

	class Star(PyRule) :
		targets = { 'DST' : (r'{File:.*}.star{D*:\d+}',) }
		dep     = '{File}'
		def cmd() :
			text = sys.stdin.read()
			open(f'{File}.star1','w').write(text)
			open(f'{File}.star2','w').write(text)

	class Cpy(Rule) :
		target = r'{File:.*}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

	class Chk(Rule) :
		target = r'{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT}>&2'

else :

	import ut

	print( 'hello' , file=open('hello'     ,'w') )
	print( '1'     , file=open('ok.opt.ref','w') )
	print( '2'     , file=open('ko.opt.ref','w') )

	ut.lmake( 'hello.star1.cpy' ,                   done=2 , new=1 )
	ut.lmake( 'hello.star2'     ,                   done=0 , new=0 )
	ut.lmake( 'hello.star3'     , rc=1 ,            done=0 , new=0 )
	ut.lmake( 'ok.opt.ok'       ,                   done=2 , new=1 ) # select Opt
	ut.lmake( 'ko.opt.ok'       ,        steady=1 , done=2 , new=1 ) # select Opt2

