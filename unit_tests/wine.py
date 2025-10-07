# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import shutil

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.network_delay = 10    # WineInit is still alive after job end for ~1s but may last more than 5s
	lmake.config.trace.n_jobs  = 10000 # ensure we keep all traces

	xvfb = shutil.which('xvfb-run')

	class Base(Rule) :
		stems = { 'Method' : r'\w+' }

	class WineRule(Rule) :
		tmp_view = '/tmp'                                                            # ensure pid namespace is used to ensure reliable job termination
		side_targets = {
			'WINE'  : (r'.wine/{*:.*}' ,'incremental')                               # wine writes in dir, even after init
		,	'CACHE' : (r'.cache/{*:.*}','incremental')                               # .
		,	'LOCAL' : (r'.local/{*:.*}','incremental')                               # .
		,	'DBUS'  : (r'.dbus/{*:.*}' ,'incremental')                               # .
		}
		side_deps = {
			'TOP_DIR'    : ('.'            ,'readdir_ok')                            # wine seems to sometimes readdir that
		,	'LOCAL_DIR'  : ('.local'       ,'readdir_ok')                            # .
		,	'CONFIG_DIR' : ('.config'      ,'readdir_ok')                            # .
		,	'MENUS_DIR'  : ('.config/menus','readdir_ok')                            # .
		}
		environ = { 'WINEDEBUG' : '-all' }
		if xvfb : environ_resources = { 'SMALL_ID' : '$SMALL_ID'                   } # display is provided by xvfb-run
		else    : environ_resources = { 'DISPLAY'  : lmake.user_environ['DISPLAY'] } # use current display

	class WineInit(WineRule) :
		target       = 'wine_init'
		targets      = { 'WINE' : (r'.wine/{*:.*}','incremental') }                 # for init wine env is not incremental
		side_targets = { 'WINE' : None                            }
		timeout      = 100                                                          # actual time is ~10s but seems to sometimes block under heavy load
		stderr_ok    = True
		readdir_ok   = True
		if xvfb : cmd = f'D=$(($SMALL_ID+50)) ; {xvfb} -n $D wine64 cmd && sleep 1' # do nothing, init support files (in targets) wait for wineserver to die (3s by default)
		else    : cmd =  '                                   wine64 cmd && sleep 1' # .

	for ext in ('','64') :
		class Dut(Base,WineRule) :
			name    = f'Dut{ext}'
			target  = f'dut{ext}.{{Method}}'
			deps    = { 'WINE_INIT' : 'wine_init' }
			timeout = 20                                                                                                    # actual time is ~2s
			autodep = '{Method}'
			# wine sends err messages, that do occur, to stdout !
			if xvfb : cmd = f'set -o pipefail ; D=$(($SMALL_ID+50)) ; {xvfb} -n $D wine{ext} hostname | head -1 && sleep 2' # wine exits before hostname has finished !
			else    : cmd = f'set -o pipefail ;                                    wine{ext} hostname | head -1 && sleep 2' # .

	class Chk(Base,PyRule) :
		target = r'test{Ext:64|}.{Method}'
		dep    =  'dut{Ext}.{Method}'
		def cmd() :
			import socket
			import sys
			ref = socket.gethostname().lower()
			dut = sys.stdin.read().strip().lower()
			assert dut==ref,f'{dut} != {ref}'

else :

	import os
	import os.path as osp
	import subprocess as sp
	import sys

	import ut

	wine64 = shutil.which('wine64')
	if not wine64 :
		print('wine64 not available',file=open('skipped','w'))
		exit()

	# try to adapt to various installations
	wine64 = osp.realpath(wine64)
	if wine64.startswith('/usr/bin/') : prefix_exe = '/usr/lib/x86_64-linux-gnu/wine/x86_64-windows'
	else                              : prefix_exe = osp.dirname(osp.dirname(wine64))+'/lib64/wine/x86_64-windows'

	cmd_exe = f'{prefix_exe}/cmd.exe'
	if not osp.exists(cmd_exe) :
		print(f'{cmd_exe} not found',file=open('skipped','w'))
		exit()

	hostname_exe = f'{prefix_exe}/hostname.exe'
	if not osp.exists(hostname_exe) :
		print(f'{hostname_exe} not found',file=open('skipped','w'))
		exit()

	ut.lmake( *(f'test64.{m}' for m in lmake.autodeps) , done=1+2*len(lmake.autodeps) , new=0 , rc=0 )
	ut.lmake( *(f'test64.{m}' for m in lmake.autodeps)                                               ) # ensure nothing needs to be remade
	if os.environ['HAS_32'] and shutil.which('wine') :
		methods_32 = [m for m in lmake.autodeps if m!='ptrace']
		ut.lmake( *(f'test.{m}' for m in methods_32) , done=2*len(methods_32) , new=0 , rc=0 )         # ptrace is not supported in 32 bits
		ut.lmake( *(f'test.{m}' for m in methods_32)                                         )         # ensure nothing needs to be remade
