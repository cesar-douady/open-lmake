# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os.path as osp
import shutil

# try to adapt to various installations
wine = shutil.which('wine')
if wine :
	wine = osp.realpath(wine)
	if wine.startswith('/usr/bin/') : hostname_exe = '/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/hostname.exe'
	else                            : hostname_exe = osp.dirname(osp.dirname(wine))+'/lib64/wine/x86_64-windows/hostname.exe'

import lmake

if __name__!='__main__' :

	import os

	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Base(Rule) :
		stems = { 'Method' : r'\w+' }

	class WineRule(Rule) :
		side_targets      = { 'WINE' : ('.wine/{*:.*}','incremental') }
		environ_resources = { 'DISPLAY' : os.environ['DISPLAY'] }
		timeout           = 30                                    # actual time should be ~5s, but seems to block from time to time when host is loaded

	class WineInit(WineRule) :
		target       = '.wine/init'
		targets      = { 'WINE' : '.wine/{*:.*}' }                                                     # for init wine env is not incremental
		side_targets = { 'WINE' : None }
		allow_stderr = True
		cmd          = 'wine64 cmd >$TMPDIR/out 2>$TMPDIR/err ; cat $TMPDIR/out ; cat $TMPDIR/err >&2' # do nothing, just to init support files (in targets), avoid waiting for stdout/stderr

	class Dut(Base,WineRule) :
		target       = 'dut.{Method}'
		deps         = { 'WINE_INIT' : '.wine/init' }
		autodep      = '{Method}'
		allow_stderr = True                                                                                         # in some systems, there are fixme messages
		cmd          = f'wine64 {hostname_exe} > $TMPDIR/out 2>$TMPDIR/err ; cat $TMPDIR/out ; cat $TMPDIR/err >&2' # avoid waiting for stdout/stderr

	class Chk(Base) :
		target = r'test.{Method}'
		dep    =  'dut.{Method}'
		def cmd() :
			import socket
			import sys
			ref = socket.gethostname().lower()
			dut = sys.stdin.read().strip().lower()
			assert dut==ref,f'{dut} != {ref}'

else :

	import sys

	import ut

	if not wine :
		print('skipped (wine not found)',file=sys.stderr)
	elif not osp.exists(hostname_exe) :
		print(f'skipped ({hostname_exe} not found',file=sys.stderr)
	else :
		methods = ['none','ld_preload']
		if lmake.has_ptrace   : methods.append('ptrace'  )
		if lmake.has_ld_audit : methods.append('ld_audit')
		ut.lmake( *(f'test.{m}' for m in methods) , done=1+2*len(methods) , new=0 , rc=0 )
		ut.lmake( *(f'test.{m}' for m in methods)                                        ) # ensure nothing needs to be remade
