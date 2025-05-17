# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import shutil

import lmake

if __name__!='__main__' :

	import os.path as osp
	import socket

	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	)

	sge_bin  = osp.dirname(shutil.which('qsub',path=lmake.user_environ['PATH']))
	sge_root = osp.dirname(osp.dirname(sge_bin))
	lmake.config.backends.sge = {
		'interface'    : lmake.user_environ.get('LMAKE_INTERFACE',socket.gethostname())
	,	'environ'      : { 'DUT':'dut' }
	,	'bin'          : sge_bin
	,	'root'         : sge_root
	#,	'cpu_resource' : 'cpu'
	#,	'mem_resource' : 'mem'
	#,	'tmp_resource' : 'tmp'
	}

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		backend   = 'sge'
		resources = {'mem':'20M'}

	class CatSh(Cat) :
		target  = '{File1}+{File2}_sh'
		environ = { 'DUT':... }
		cmd = '''
			[ "$DUT" = dut ] || echo bad '$DUT :' "$DUT != dut" >&2
			cat {FIRST} {SECOND}
		'''

	class CatPy(Cat,PyRule) :
		target = '{File1}+{File2}_py'
		def cmd() :
			print(open(FIRST ).read(),end='')
			print(open(SECOND).read(),end='')

else :

	import subprocess as sp

	if 'sge' not in lmake.backends :
		print('sge not compiled in',file=open('skipped','w'))
		exit() ;
	if not shutil.which('qsub') :
		print('sge not available',file=open('skipped','w'))
		exit() ;
	if sp.run(('qsub','-b','y','-o','/dev/null','-e','/dev/null','-N','<tu_sense_daemon>','/dev/null'),stdin=sp.DEVNULL,stdout=sp.DEVNULL,stderr=sp.DEVNULL).returncode!=0 :
		print('sge not usable',file=open('skipped','w'))
		exit() ;

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=2 , new=2 ) # check targets are out of date
	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=0 , new=0 ) # check targets are up to date
	ut.lmake( 'hello+hello_sh' , 'world+world_py' , done=2         ) # check reconvergence
