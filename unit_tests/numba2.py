# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,_lmake_dir,root_dir

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.backends.slurm = {
		'n_max_queued_jobs' : 10
	}

	class TestNumba(Rule):
		target  = 'dut'
		autodep = 'ld_preload'
		backend = 'slurm'
		resources = {
			'cpu' : 1
		,	'mem' : '256M'
		}
		def cmd():
			from subprocess import check_call
			from numba      import vectorize , uint32
			#
			@vectorize( [uint32(uint32)] , target='parallel' )
			def clipUi32_(x) :
				return x
			check_call('hostname')

else :

	import ut

	try :
		import numba
		ut.lmake('dut',done=1)
	except : pass
