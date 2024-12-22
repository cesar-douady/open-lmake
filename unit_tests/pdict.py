# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.utils import pdict
	from lmake.rules import PyRule

	lmake.manifest = ('Lmakefile.py',)

	param = pdict(a=1,b=2)

	class Dut(PyRule) :
		target = 'dut'
		def cmd() :
			assert param['a']==1 and param['b']==2
			assert param.a   ==1 and param.b   ==2

else :

	import ut

	ut.lmake( 'dut' , done=1 ) # check param can be used as a pdict
