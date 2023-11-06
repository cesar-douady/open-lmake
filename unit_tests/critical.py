# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

n_good = 2

if __name__!='__main__' :

	import lmake
	from step import step

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	,	'src1'
	,	'src2'
	)

	class Good(lmake.Rule) :
		target = 'good{Digit:\d+}'
		def cmd() :
			if int(Digit)==0 : print(step)

	class Bad(lmake.Rule) :
		target = 'bad{:\d+}'
		cmd = 'exit {step}'

	class Critical(lmake.Rule) :
		target = 'tgt'
		def cmd() :
			lmake.depend('src1','src2',critical=True)
			lmake.depend(*(f'good{i}' for i in range(n_good)),critical=True)
			if step==1 : lmake.depend('src1','bad0','bad0','bad1')
			else       : lmake.depend('src1'                     )

else :

	import ut

	print('1',file=open('src1','w'))
	print('2',file=open('src2','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'tgt' , may_rerun=2 , was_dep_err=1 , done=n_good , failed=2 , new=2 , rc=1 ) # must discover good_*, then bad_*

	print('new 1',file=open('src1','w'))
	ut.lmake( 'tgt' , dep_err=1 , changed=1 , rc=1 )                           # src* are not critical, so error fires immediately

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'tgt' , steady=n_good-1+1 , done=1 , rc=0 )                      # modified critical good_0 implies that bad_* are not remade
