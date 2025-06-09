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
#		chroot_dir   = '/'                                                           # ensure pid namespace is used to ensure reliale job termination
		side_targets = {
			'WINE'  : (r'.wine/{*:.*}' ,'incremental')                               # wine writes in dir, even after init
		,	'CACHE' : (r'.cache/{*:.*}','incremental')                               # .
		,	'LOCAL' : (r'.local/{*:.*}','incremental')                               # .
		}
		side_deps = {
			'TOP_DIR'   : ('.'     ,'readdir_ok')                                    # wine seems to sometimes readdir that
		,	'LOCAL_DIR' : ('.local','readdir_ok')                                    # .
		}
		if xvfb : environ_resources = { 'SMALL_ID' : '$SMALL_ID'                   } # display is provided by xvfb-run
		else    : environ_resources = { 'DISPLAY'  : lmake.user_environ['DISPLAY'] } # use current display

	class WineInit(WineRule) :
		target       = '.wine/init'
		targets      = { 'WINE' : r'.wine/{*:.*}' }                                                    # for init wine env is not incremental
		side_targets = { 'WINE' : None            }
		stderr_ok    = True
		readdir_ok   = True
		environ      = { 'DBUS_SESSION_BUS_ADDRESS' : lmake.user_environ['DBUS_SESSION_BUS_ADDRESS'] } # else a file is created in .dbus/session-bus
		timeout      = 30                                                                              # actual time should be ~5s for the init rule, ...
		#                                                                                              # ... but seems to block from time to time when host is loaded
		if xvfb : cmd = f'{xvfb} wine64 cmd && sleep 1'                                                # do nothing, just to init support files (in targets) (sleep to allow X server to clean up)
		else    : cmd =  '       wine64 cmd'                                                           # .

	for ext in ('','64') :
		class Dut(Base,WineRule) :
			name    = f'Dut{ext}'
			target  = f'dut{ext}.{{Method}}'
			deps    = { 'WINE_INIT' : '.wine/init' }
			autodep = '{Method}'
			if xvfb : cmd = f'{xvfb} -n $((50+$SMALL_ID)) wine{ext} hostname && sleep 1' # wine terminates before hostname, so we have to wait to get the result
			else    : cmd = f'                            wine{ext} hostname && sleep 1' # .

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
