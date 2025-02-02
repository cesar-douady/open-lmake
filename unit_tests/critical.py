# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

n_goods = 10
n_bads  = 10

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'src1'
	,	'src2'
	)

	class Good(PyRule) :
		target = r'good{Digit:\d+}'
		def cmd() :
			if int(Digit)==0 : print(step)

	class Bad(Rule) :
		target = r'bad{:\d+}'
		cmd    = 'exit {step}'

	class Critical(PyRule) :
		target = 'tgt'
		def cmd() :
			lmake.depend('src1','src2'                        ,read=True,critical=True)
			lmake.depend(*(f'good{i}' for i in range(n_goods)),read=True,critical=True)
			if step==1 : lmake.depend('src1','bad0',*(f'bad{i}' for i in range(n_bads)))
			else       : lmake.depend('src1'                                           )

else :

	import ut

	print('1',file=open('src1','w'))
	print('2',file=open('src2','w'))

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'tgt' , may_rerun=2 , was_dep_err=1 , done=n_goods , failed=n_bads , new=2 , rc=1 ) # must discover good_*, then bad_*

	print('new 1',file=open('src1','w'))
	ut.lmake( 'tgt' , dep_err=1 , changed=1 , rc=1 ) # src* are read, so tgt is rerun

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'tgt' , steady=n_goods-1+1 , done=1 , rc=0 ) # modified critical good_0 implies that bad_* are not remade
