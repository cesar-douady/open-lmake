# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		cmd = 'cat {FIRST} {SECOND}'

else :

	import os
	import os.path    as osp
	import shutil
	import subprocess as sp

	import ut

	def git_run(*args,cwd=None) :
		cmd = 'git',*args
		if cwd : print(f"+ ( cd {cwd} ; {' '.join(cmd)} )",flush=True)
		else   : print(f"+ {             ' '.join(cmd)}"  ,flush=True)
		sp.run( cmd , check=True , stderr=sp.STDOUT , cwd=cwd )

	def git_glb_config(key,val  ) : git_run('config',            '--global',            key,val         )
	def git_init      (repo     ) : git_run('init'  ,                                   repo            )
	def git_sm_update (repo     ) : git_run('submodule','update','--init','--recursive',        cwd=repo)
	def git_add       (repo,path) : git_run('add',                                      path   ,cwd=repo)

	cnt = 0
	def git_commit(repo) :
		global cnt
		git_run('commit','-a',f'-m{cnt}',cwd=repo)
		cnt += 1

	def mk_file(repo,path,val) :
		fn = osp.join(repo,path)
		os.makedirs(osp.dirname(fn),exist_ok=True)
		print(val,file=open(fn,'w'))
		git_add(repo,path)

	def git_sm_add(repo,path,sm_repo) :
		fn = osp.join(repo,path)
		os.makedirs(osp.dirname(fn),exist_ok=True)
		git_run('submodule','add','--name',f'{osp.basename(path)}_name',osp.abspath(sm_repo),path,cwd=repo)

	os.environ['HOME'] = os.getcwd()                  # git requires some global configuration, make it local to this unit test
	git_glb_config('protocol.file.allow','always'   )
	git_glb_config('user.name'          ,'test'     )
	git_glb_config('user.email'         ,'test@test')
	git_glb_config('init.defaultBranch' ,'master'   )

	git_init  ('sub2_repo')
	mk_file   ('sub2_repo','sub2_file','sub2_file')
	git_commit('sub2_repo')

	git_init     ('sub_repo')
	mk_file      ('sub_repo','sub_file' ,'sub_content')
	git_sm_add   ('sub_repo','sub2_path','sub2_repo'  )
	git_sm_update('sub_repo')
	git_commit   ('sub_repo')

	git_init     ( 'top_repo' )
	mk_file      ( 'top_repo' , 'top_file'               , 'top_content'               )
	git_sm_add   ( 'top_repo' , 'sub_path1/sub_path2'    , 'sub_repo'                  )
	mk_file      ( 'top_repo' , 'Lmakefile.py'           , open('Lmakefile.py').read() )
	mk_file      ( 'top_repo' , 'sub_path1/Lmakefile.py' , open('Lmakefile.py').read() )
	git_sm_update( 'top_repo' )
	git_commit   ( 'top_repo' )

	os.chdir('top_repo')
	os.makedirs('LMAKE',exist_ok=True)
	ut.lmake( 'sub_path1/sub_path2/sub_file+sub_path1/sub_path2/sub2_path/sub2_file' , done=1 , new=2 )
	ut.lmake( 'sub_path1/sub_path2/sub_file+sub_path1/sub_path2/sub2_path/sub2_file' , done=0 , new=0 )

	shutil.rmtree('LMAKE')
	os.chdir('sub_path1')
	os.makedirs('LMAKE',exist_ok=True)
	ut.lmake( 'sub_path2/sub_file+sub_path2/sub2_path/sub2_file' , done=1 , new=2 )
	ut.lmake( 'sub_path2/sub_file+sub_path2/sub2_path/sub2_file' , done=0 , new=0 )
