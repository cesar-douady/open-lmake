# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake

	from step import numpy_home

	sys.path.append(numpy_home) # keep local repo as last entry to avoid spurious deps

	import numpy # check we can import numpy

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

else :

	import os
	import os.path as osp
	import sys

	import ut

	if 'VIRTUAL_ENV' in os.environ :                                             # manage case where numpy is installed in an activated virtual env
		venv = os.environ["VIRTUAL_ENV"]
		vi   = sys.version_info
		sys.path.append(f'{venv}/lib/python{vi.major}.{vi.minor}/site-packages') # mimic open-lmake config

	try :
		import numpy
	except :
		print('numpy not available',file=open('skipped','w'))
		exit()

	numpy_home = osp.dirname(osp.dirname(numpy.__file__))
	print(f'numpy_home={numpy_home!r}',file=open('step.py','w'))

	ut.lmake(done=0,new=0) # just check we can load Lmakefile.py
