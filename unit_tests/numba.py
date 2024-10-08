# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = ('Lmakefile.py',)

	class TestNumba(PyRule) :
		targets = { 'TGT' : 'test.so'}
		def cmd() :
			from numba.pycc import CC
			import numpy as np
			cc = CC('test')
			@cc.export('test_nb','uint16[:]()')
			def test_nb():
				res = np.empty((1,),dtype=np.uint16)
				return res
			cc.target_cpu  = None
			cc.output_file = TGT
			cc.compile()

else :

	import ut

	try :
		import numba
		ut.lmake('test.so',done=1)
	except :
		print('numba not available',file=open('skipped','w'))
