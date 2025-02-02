# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os.path as osp
	import socket

	import lmake
	from lmake.rules import PyRule

	if 'slurm' in lmake.backends and osp.exists('/etc/slurm/slurm.conf') :
		lmake.config.backends.slurm = {
			'interface'         : lmake.user_environ.get('LMAKE_INTERFACE',socket.gethostname())
		,	'use_nice'          : True
		,	'n_max_queued_jobs' : 10
		}
		backend = 'slurm'
	else :
		backend = 'local'


	lmake.manifest = ('Lmakefile.py',)

	lmake.config.backends.local.cpu = 10
	lmake.config.trace.n_jobs       = 10000

	class GenFile(PyRule) :
		target    = r'file_{:\d+}'
		backend   = backend
		resources = { 'mem' : '1M' }
		def cmd() :
			for x in range(1000) : print(x)

	class Trig(PyRule) :
		target = r'out_{P:\d+}_{N:\d+}'
		def cmd() :
			p = int(P)
			if p : lmake.depend(*(f'out_{p-1}_{x}' for x in range(int(N)+1)))
			else : lmake.depend(*(f'file_{x}'      for x in range(int(N)+1)))

else :

	import ut

	n = 20
	p = 40
	d = ut.lmake( f'out_{p}_{n}' , may_rerun=... , rerun=... , was_done=... , done=... , steady=... )
	expected_done = n*p+p+n+2
	assert d['was_done']+d['done']+d['steady']==n*p+p+n+2,f'bad counts : {d} expected done : {expected_done}'
