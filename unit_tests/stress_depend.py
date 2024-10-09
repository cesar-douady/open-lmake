# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	lmake.config.backends.local.cpu = 1000 # a unreasonable but stressing value
	lmake.config.backends.slurm = {
		'use_nice'          : True
	,	'n_max_queued_jobs' : 10
	}

	lmake.manifest = ('Lmakefile.py',)

	for backend in ('local','slurm') :

		class Touch(Rule) :
			name    = f'touch {backend}'
			backend = backend
			target  = fr'touch_{backend}_{{N:\d+}}'
			resources = {
					'cpu' : 1
				,	'mem' : '10M'
				}
			cmd = ''

		class Test(PyRule):
			name    = f'test {backend}'
			backend = backend
			target  = fr'out_{backend}{{Verbose:(_verbose)?}}_{{N:\d+}}'
			python  = ('/usr/bin/python3','-B','-tt')
			resources = {
				'cpu' : 1
			,	'mem' : '10M'
			}
			def cmd(backend=backend) :
				lmake.depend(f'touch_{backend}_{N}',verbose=Verbose)

	class All(PyRule):
		target = r'all_{Backend:slurm|local}{Verbose:(_verbose)?}_{N:\d+}'
		def cmd():
			lmake.depend(*(f'out_{Backend}{Verbose}_{i}' for i in range(int(N))))

else :

	n = 100 # use a higher value, e.g. 10000, for a really stressing test

	import ut

	if 'slurm' in lmake.backends : ut.lmake( f'all_slurm_{n}'         , steady=n+1 , done=n , may_rerun=n+1 )
	if True                      : ut.lmake( f'all_local_{n}'         , steady=n+1 , done=n , may_rerun=n+1 )
	if not lmake.Autodep.IsFake  : ut.lmake( f'all_local_verbose_{n}' , steady=  1 , done=n , may_rerun=  1 )
