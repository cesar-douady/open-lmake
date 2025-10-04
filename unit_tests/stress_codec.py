# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

n_jobs    = 100
n_files   = 10
n_ctxs    = 10
n_codes   = 10
n_targets = 10
n_encodes = 1000

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.config.backends.local.cpu = n_jobs
	lmake.config.trace.n_jobs       = 10000  # ensure we keep all traces for analysis

	lmake.manifest = (
		'Lmakefile.py'
	,	*( f'codec_{i}' for i in range(n_files) )
	)

	class Decode(Rule) :
		target = r'decode_{File:.*}/{Ctx:.*}/{Code:.*}'
		cmd    = 'ldecode -f {File} -x {Ctx} -c {Code}'

	class Encode(PyRule) :
		target = r'encode_{I:\d+}'
		def cmd() :
			fail = None
			for i in range(n_encodes) :
				n    = int(I)*n_targets+i
				file = f'codec_{n*97%n_files}'
				ctx  = f'ctx_{n*53%n_files}'
				code = lmake.encode( val=('val',int(I)+i) , file=file , ctx=ctx )
				try    : print(open(f'decode_{file}/{ctx}/{code}').read())
				except : fail = True
			assert not fail,'missing some vals'

	class Dut(PyRule) :
		target = 'dut'
		def cmd() :
			lmake.depend( *(f'encode_{i}' for i in range(n_targets)) , read=True )

else :

	import ut

	for i in range(n_files) :
		open(f'codec_{i}','w')

	cnt = ut.lmake( 'dut' , new=1+n_files , done=1+n_targets*(1+n_encodes) , may_rerun=1+n_targets , reformat=n_files )
