# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os

if __name__!='__main__' :

	from step import rustup_home
	os.environ['RUSTUP_HOME'] = rustup_home # set before importing lmake.rules so RustRule is correctly configured

	import lmake
	from lmake.rules import Rule,RustRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'mandelbrot.zip'
	)

	class Unzip(Rule) :
		targets = { 'FILE' : r'mandelbrot/{*:.*}' }
		deps    = { 'ZIP'  : 'mandelbrot.zip'     }
		cmd     = 'unzip {ZIP} ; mv mandelbrot/output.txt mandelbrot/output.ref'

	class RunRust(RustRule) :
		targets = {
			'OUT'     : 'mandelbrot/output.dut'
		,	'COMPILE' : r'mandelbrot/target/{*:.*}'
		}
		side_targets = {
			'SCRATCH' : ( r'.cargo/{*:.*}' , 'Top' )
		}
		deps         = { 'MAIN' : 'mandelbrot/src/main.rs' }
		allow_stderr = True
		autodep      = 'ld_preload_jemalloc'
		environ_cmd  = { 'LD_PRELOAD' : 'libjemalloc.so' }
		cmd          = 'cd mandelbrot ; cargo run --release ; mv output.txt output.dut'

	class Cmp(Rule) :
		target = 'ok'
		deps = {
			'DUT' : 'mandelbrot/output.dut'
		,	'REF' : 'mandelbrot/output.ref'
		}
		cmd = 'diff {REF} {DUT} >&2'

else :

	import os.path as osp
	import shutil
	import subprocess as sp
	import sys

	import ut

	cargo = shutil.which('cargo')
	if not cargo :
		print('cargo not available',file=open('skipped','w'))
		exit()

	rustup_home = osp.dirname(osp.dirname(osp.dirname(cargo)))+'/.rustup'
	print(f'rustup_home={rustup_home!r}',file=open('step.py','w'))

	os.symlink('../mandelbrot.zip','mandelbrot.zip')
	ut.lmake( 'ok' , done=3 , new=1 )
