# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
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

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	class Unzip(Rule) :
		targets     = { 'FILE' : r'mandelbrot/{*:.*}' }
		deps        = { 'ZIP'  : 'mandelbrot.zip'     }
		cache       = 'my_cache'
		compression = 1
		cmd         = 'unzip {ZIP} ; mv mandelbrot/output.txt mandelbrot/output.ref'

	class RunRust(RustRule) :
		targets = {
			'OUT'     :  'mandelbrot/output.dut'
		,	'COMPILE' : r'mandelbrot/target/{*:.*}'
		}
		side_targets = { 'SCRATCH' : ( r'.cargo/{*:.*}'          , 'top'        ) }
		side_deps    = { 'DIR'     : (  'mandelbrot/{*:.*}'      , 'readdir_ok' ) }
		deps         = { 'MAIN'    :    'mandelbrot/src/main.rs'                  }
		stderr_ok    = True
		autodep      = 'ld_preload_jemalloc'
		environ      = { 'LD_PRELOAD' : 'libjemalloc.so' }
		cache        = 'my_cache'
		compression  = 1
		cmd          = 'cd mandelbrot ; cargo run --release ; mv output.txt output.dut'

	class Cmp(Rule) :
		target = 'ok'
		deps = {
			'DUT' : 'mandelbrot/output.dut'
		,	'REF' : 'mandelbrot/output.ref'
		}
		cmd = 'diff {REF} {DUT} >&2'

else :

	import shutil

	cargo = shutil.which('cargo')
	if not cargo :
		print('cargo not available',file=open('skipped','w'))
		exit()

	import os.path as osp

	import ut

	# cache dir must be writable by all users having access to the cache
	# use setfacl(1) with adequate rights in the default ACL, e.g. :
	# os.system('setfacl -m d:g::rw,d:o::r CACHE')
	os.makedirs('CACHE/LMAKE')
	print('size=1<<30',file=open('CACHE/LMAKE/config.py','w'))

	rustup_home = osp.dirname(osp.dirname(osp.dirname(cargo)))+'/.rustup'
	print(f'rustup_home={rustup_home!r}',file=open('step.py','w'))

	os.symlink('../mandelbrot.zip','mandelbrot.zip')
	ut.lmake( 'ok' , done=3 , new=1 )

	os.system('find mandelbrot -type d -o -print | sort >before_cache')

	os.system('mv LMAKE LMAKE.1 ; mv mandelbrot mandelbrot.1')

	ut.lmake( 'ok' , hit_done=2 , done=1 , new=1 )

	os.system('find mandelbrot -type d -o -print | sort >after_cache')

	assert os.system('set -x ; diff before_cache after_cache')==0
