# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

n_jobs    = 100
n_files   = 10
n_ctxs    = 10
n_codes   = 10
n_targets = 100
n_encodes = 1000

n_dones   = 11001 # too complicated to anticipate, juste try and report correct number

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.config.backends.local.cpu = n_jobs
	lmake.config.network_delay      =    10  # host is overloaded
	lmake.config.trace.n_jobs       = 20000  # ensure we keep all traces for analysis

	lmake.manifest = (
		'Lmakefile.py'
	,	*( f'codec_{i}' for i in range(n_files) )
	)

	class Decode(Rule) :
		target = r'decode/{File:.*}/{Ctx:.*}/{Code:.*}'
		cmd    = 'ldecode -f {File} -x {Ctx} -c {Code}'

	class Encode(PyRule) :
		target = r'encode/{I:\d+}'
		def cmd() :
			fail = None
			for i in range(n_encodes) :
				n    = int(I)*n_targets+i
				file = f'codec_{n*97%n_files}'
				ctx  = f'ctx_{n*53%n_files}'
				code = lmake.encode( val=('val',int(I)+i) , file=file , ctx=ctx )
				try    : print(open(f'decode/{file}/{ctx}/{code}').read())
				except : fail = True
			assert not fail,'missing some vals'

	class Dut(PyRule) :
		target = 'dut'
		def cmd() :
			lmake.depend( *(f'encode/{i}' for i in range(n_targets)) , read=True )

else :

	import ut

	for i in range(n_files) :
		open(f'codec_{i}','w')

	ut.lmake( 'dut' , new=1+n_files , expand=n_files , done=n_dones , may_rerun=1+n_targets , update=n_files )
