# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import os

	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.network_delay = 10    # WineInit is still alive after job end for ~1s but may last more than 5s
	lmake.config.trace.n_jobs  = 10000 # ensure we keep all traces

	class Base(Rule) :
		stems = { 'Method' : r'\w+' }

	class WineRule(Rule) :
		chroot_dir        = '/'                                             # ensure pid namespace is used to ensure reliale job termination
		side_targets      = { 'WINE'    : (r'.wine/{*:.*}','incremental') }
		environ_resources = { 'DISPLAY' : lmake.user_environ['DISPLAY']   }
		timeout           = 30                                              # actual time should be ~5s for the init rule, but seems to block from time to time when host is loaded

	class WineInit(WineRule) :
		target       = '.wine/init'
		targets      = { 'WINE' : r'.wine/{*:.*}' }                                                    # for init wine env is not incremental
		side_targets = { 'WINE' : None            }
		allow_stderr = True
		environ_cmd  = { 'DBUS_SESSION_BUS_ADDRESS' : lmake.user_environ['DBUS_SESSION_BUS_ADDRESS'] } # else a file is created in .dbus/session-bus
		cmd          = 'wine64 cmd'                                                                    # do nothing, just to init support files (in targets)

	class Dut(Base,WineRule) :
		target  = 'dut.{Method}'
		deps    = { 'WINE_INIT' : '.wine/init' }
		autodep = '{Method}'
		cmd     = 'wine64 hostname ; sleep 1' # wine64 terminates before hostname, so we have to wait to get the result

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

	import os.path as osp
	import shutil
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

	cmd_exe      = f'{prefix_exe}/cmd.exe'
	hostname_exe = f'{prefix_exe}/hostname.exe'
	if   not osp.exists(cmd_exe)      : print(f'{cmd_exe} not found'     ,file=open('skipped','w'))
	elif not osp.exists(hostname_exe) : print(f'{hostname_exe} not found',file=open('skipped','w'))
	else :
		methods = ['none','ld_preload']
		if lmake.has_ptrace   : methods.append('ptrace'  )
		if lmake.has_ld_audit : methods.append('ld_audit')
		ut.lmake( *(f'test.{m}' for m in methods) , done=1+2*len(methods) , new=0 , rc=0 )
		ut.lmake( *(f'test.{m}' for m in methods)                                        ) # ensure nothing needs to be remade
