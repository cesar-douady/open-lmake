# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	from step import numba_home

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	class TestNumba(PyRule) :
		targets      = { 'TGT' : 'test.so'}
		side_targets = { 'O'   : 'test.o' }
		environ_cmd  = { 'PYTHONPATH' : numba_home+':...' } # '...' stands for inherited value
		def cmd() :
			from numba.pycc import CC
			import numpy as np
			import os
			cc = CC('test')
			@cc.export('test_nb','uint16[:]()')
			def test_nb():
				res = np.empty((1,),dtype=np.uint16)
				return res
			cc.target_cpu  = None
			cc.output_file = os.getcwd()+'/'+TGT            # provide absolute name so that it's ok even with use_script=True
			cc.compile()

else :

	import os
	import os.path as osp
	import sys

	import ut

	if 'VIRTUAL_ENV' in os.environ :                                                                                              # manage case where numba is installed in an activated virtual env
		sys.path.append(f'{os.environ["VIRTUAL_ENV"]}/lib/python{sys.version_info.major}.{sys.version_info.minor}/site-packages')

	try :
		import numba.pycc.CC
	except :
		print('numba not available',file=open('skipped','w'))
		exit()

	numba_home = osp.dirname(osp.dirname(numba.__file__))
	print(f'numba_home={numba_home!r}',file=open('step.py','w'))

	ut.lmake('test.so',done=1)
