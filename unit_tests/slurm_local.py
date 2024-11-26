# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import socket

	from lmake.rules import Rule,PyRule

	lmake.config.backends.slurm = {
		'interface' : lmake.user_environ.get('LMAKE_INTERFACE',socket.gethostname())
	}

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	)

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		backend   = 'slurm'
		resources = {'mem':'20M'}

	class CatSh(Cat) :
		target = '{File1}+{File2}_sh'
		cmd    = '''
			cat {FIRST} {SECOND}
		'''

	class CatPy(Cat,PyRule) :
		target    = '{File1}+{File2}_py'
		resources = {'gres':1}           # use a resource with no local translation, forcing a single job
		def cmd() :
			print(open(FIRST ).read(),end='')
			print(open(SECOND).read(),end='')

else :

	import os.path as osp

	if 'slurm' not in lmake.backends :
		print('slurm not compiled in',file=open('skipped','w'))
		exit()
	if not osp.exists('/etc/slurm/slurm.conf') :
		print('slurm not available',file=open('skipped','w'))
		exit()

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( '-l' , 'hello+world_sh' , 'hello+world_py' , done=2 , new=2 ) # check targets are out of date
	ut.lmake( '-l' , 'hello+world_sh' , 'hello+world_py' , done=0 , new=0 ) # check targets are up to date
	ut.lmake( '-l' , 'hello+hello_sh' , 'world+world_py' , done=2         ) # check reconvergence
