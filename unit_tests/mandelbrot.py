# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,RustRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'mandelbrot.zip'
	)

	class Unzip(Rule) :
		targets = { 'FILE' : r'mandelbrot/{*:.*}' }
		deps    = { 'ZIP'  : 'mandelbrot.zip'     }
		cmd     = 'unzip {ZIP} ; mv mandelbrot/output.txt mandelbrot/output.ref'

	class RunRust(RustRule) :
		cwd = 'mandelbrot'
		targets = {
			'OUT'     : 'output.dut'
		,	'COMPILE' : r'target/{*:.*}'
		}
		side_targets = {
			'SCRATCH' : ( '.cargo/{*:.*}' , 'Top' )
		}
		deps         = { 'MAIN' : 'src/main.rs' }
		allow_stderr = True
		autodep      = 'ld_preload_jemalloc'
		environ_cmd  = { 'LD_PRELOAD' : 'libjemalloc.so' }
		cmd          = 'cargo run --release ; mv output.txt {OUT}'

	class Cmp(Rule) :
		target = 'ok'
		deps = {
			'DUT' : 'mandelbrot/output.dut'
		,	'REF' : 'mandelbrot/output.ref'
		}
		cmd = 'diff {REF} {DUT} >&2'

else :

	import os
	import subprocess as sp
	import sys

	import ut

	try    : sp.check_output('cargo') # dont test rust if rust is not installed
	except :
		print('cargo not available',file=sys.stderr)
		exit()

	os.symlink('../mandelbrot.zip','mandelbrot.zip')
	ut.lmake( 'ok' , done=3 , new=1 )
