# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake

	from step import numpy_home

	sys.path.append(numpy_home)

	try :
		import numpy                                          # check we can import numpy
	except ModuleNotFoundError :
		numpy = None                                          # but ignore test if module does not exist
		print('numpy not available',file=open('skipped','w'))

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

else :

	import os
	import os.path as osp
	import sys

	import ut

	if 'VIRTUAL_ENV' in os.environ :                                                                                              # manage case where numpy is installed in an activated virtual env
		sys.path.append(f'{os.environ["VIRTUAL_ENV"]}/lib/python{sys.version_info.major}.{sys.version_info.minor}/site-packages')

	try :
		import numpy
	except :
		print('numpy not available',file=open('skipped','w'))
		exit()

	numpy_home = osp.dirname(osp.dirname(numpy.__file__))
	print(f'numpy_home={numpy_home!r}',file=open('step.py','w'))

	ut.lmake(done=0,new=0) # just check we can load Lmakefile.py
