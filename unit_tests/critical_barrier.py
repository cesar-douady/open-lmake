# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import lmake

	lmake.sources = (
		'Lmakefile.py'
	,	'src1'
	,	'src2'
	)

	class Bad(lmake.Rule) :
		target = 'bad{:\d}'
		cmd = 'exit 1'
	class CriticalBarrier(lmake.Rule) :
		target = 'tgt'
		def cmd() :
			lmake.depend('src1','src2')
			lmake.critical_barrier()
			lmake.depend('src1','bad1','bad2')

else :

	import ut

	print('1',file=open('src1','w'))
	print('2',file=open('src2','w'))

	ut.lmake( 'tgt' , may_rerun=1 , was_dep_err=1 , failed=2 , new=2 , rc=1 )

	print('new 1',file=open('src1','w'))
	ut.lmake( 'tgt' , dep_err=1 , new=1 , rc=1 )
