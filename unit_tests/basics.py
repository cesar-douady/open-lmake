# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

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

	class CatSh(Cat) :
		target = '{File1}+{File2}_sh'
		cmd    = 'cat {FIRST} {SECOND}'

	class CatPy(Cat,PyRule) :
		target = '{File1}+{File2}_py'
		def cmd() :
			for fn in (FIRST,SECOND) :
				with open(fn) as f : print(f.read(),end='')

else :

	import os
	import os.path as osp
	import time

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=2 , new=2   ) # check targets are out of date
	ut.lmake( 'hello+world_sh' , 'hello+world_py' , done=0 , new=0   ) # check targets are up to date
	ut.lmake( 'hello+hello_sh' , 'world+world_py' , done=2           ) # check reconvergence
	ut.lmake( 'hello+_sh'      , '+world_py'      , bad_dep=2 , rc=1 ) # check empty deps prevent job from matching

	assert os.system('ldebug -tn hello+world_sh'                   )==0 # coverage is not collected if -n flag not present
	assert os.system('ldebug -t hello+world_sh'                    )==0 # check no crash
	assert os.system('ldump_job LMAKE/lmake/local_admin/job_data/2')==0 # check no crash
	assert os.system('lkpi'                                        )==0 # check no crash

	while osp.exists('LMAKE/server') : time.sleep(1)         # ensure server has gone
	assert           os.system('chmod -w -R .'          )==0 # check we can interrogate a read-only repo
	try     : assert os.system('lshow -i hello+world_sh')==0
	finally : assert os.system('chmod u+w -R .'         )==0 # restore state

	assert not osp.exists('LMAKE/server'),'server is still alive'
