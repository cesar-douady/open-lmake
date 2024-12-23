# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as osp
def chk_numba() :
	from numba import vectorize , uint32
	@vectorize( [uint32(uint32)] , target='parallel' )
	def clipUi32_(x) :
		return x
	return numba

if __name__!='__main__' :

	import socket

	import lmake
	from lmake.rules import Rule

	if 'slurm' in lmake.backends and osp.exists('/etc/slurm/slurm.conf') :
		lmake.config.backends.slurm = {
			'interface' : lmake.user_environ.get('LMAKE_INTERFACE',socket.gethostname())
		}
		backend = 'slurm'
	else :
		backend = 'local'

	from step import numba_home

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	class TestNumba(Rule):
		target  = 'dut'
		autodep = 'ld_preload'
		environ = { 'PYTHONPATH' : numba_home+':...' } # ... stands for inherited value
		backend = backend
		resources = {
			'cpu' : 1
		,	'mem' : '256M'
		}
		def cmd():
			from subprocess import check_call
			chk_numba()
			check_call('hostname')

else :

	import os
	import sys

	import ut

	if 'VIRTUAL_ENV' in os.environ :                                                                                              # manage case where numba is installed in an activated virtual env
		sys.path.append(f'{os.environ["VIRTUAL_ENV"]}/lib/python{sys.version_info.major}.{sys.version_info.minor}/site-packages')

	try :
		numba = chk_numba()
	except :
		print('numba not available',file=open('skipped','w'))
		exit()

	numba_home = osp.dirname(osp.dirname(numba.__file__))
	print(f'numba_home={numba_home!r}',file=open('step.py','w'))

	ut.lmake('dut',done=1)
