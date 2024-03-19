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

	class Test(Rule) :
		target       = r'test.{Method:\w+}'
		side_targets = {
			'WINE'   : ( '.wine/{*:.*}'   , 'incremental' )
		,	'LOCAL'  : ( '.local/{*:.*}'  , 'incremental' )
		,	'CONFIG' : ( '.config/{*:.*}' , 'incremental' )
		}
		environ_resources = {
			'DISPLAY' : os.environ['DISPLAY']
		}
		autodep      = '{Method}'
		allow_stderr = True
		cmd          = f'wine {hostname_exe}'

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
		ut.lmake( *(f'test.{m}' for m in methods) , done=len(methods) , new=0 , rc=0 )
