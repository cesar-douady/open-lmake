# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# 2 repos A and B share a cache, then the cache is damaged in various ways and repaired with lcache_repair
# - repo keys are renumbered by the repair (B is inserted before A) : run files must be renamed accordingly
# - 2 runs of the same job (t.asym) match each other asymmetrically (Lnk-only access in B, Reg access in A) : repair must not abort on this conflict
# - stray files/dirs/links in job dirs, garbage info file, missing data file : must be cleaned up, and lcache_server must survive victimization of such jobs
# - a run whose files have disappeared (phantom entry) must be replaced upon next upload without any repair

if __name__!='__main__' :

	import os.path as osp

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src'
	,	'x'
	)

	lmake.config.caches.my_cache = { 'dir':osp.dirname(lmake.repo_root)+'/CACHE' }

	class Cat(Rule) :
		target = r'{File:.*}.out'
		dep    = 'src'
		cache  = 'my_cache'
		cmd    = 'cat src ; echo {File}'

	class Asym(Rule) :             # accesses to x are Reg in repo A and Lnk only in repo B
		target = r'{File:.*}.asym'
		cache  = 'my_cache'
		cmd    = 'case $(pwd) in */A) cat x ;; *) readlink x || echo notalink ;; esac'

else :

	import os
	import os.path    as osp
	import shutil
	import subprocess as sp
	import textwrap
	import time

	import ut

	def wait_cache_server() :
		for _ in range(100) :                                   # lcache_server may linger a little after lmake has exited ...
			if not osp.exists('../CACHE/LMAKE/server') : return # ... and lcache_repair refuses to run while it is alive
			time.sleep(0.1)
		raise RuntimeError('lcache_server is still alive')

	def repair(*args) :
		wait_cache_server()
		res = sp.run( ('lcache_repair',*args,'../CACHE') , universal_newlines=True , stdout=sp.PIPE , check=True ).stdout
		print(res)
		return res

	def chk_cache() :
		wait_cache_server()
		sp.run( ('lcache_dump','../CACHE') , stdout=sp.DEVNULL , check=True ) # lcache_dump checks cache coherence

	def fresh() :                                 # forget everything so that next lmake goes through cache
		shutil.rmtree('LMAKE',ignore_errors=True)
		os.makedirs('LMAKE')                      # keep repo root unambiguous
		for f in os.listdir('.') :
			if f.endswith(('.out','.asym')) : os.unlink(f)

	def run_files() :
		res = []
		for d,_,fs in os.walk('../CACHE') :
			if d.startswith('../CACHE/LMAKE') : continue
			for f in fs : res.append(osp.join(d,f))
		return sorted(res)

	os.makedirs('CACHE/LMAKE')
	print(textwrap.dedent('''
		size = 1<<20
	''')[1:],file=open('CACHE/LMAKE/config.py','w'))
	for r in ('A','B') :
		os.makedirs(r+'/LMAKE')                           # ensure repo root is not ambiguous
		os.symlink('../Lmakefile.py',f'{r}/Lmakefile.py')
		print('src'       ,file=open(f'{r}/src','w'))
		print('x from '+r ,file=open(f'{r}/x'  ,'w'))

	os.chdir('A') ; ut.lmake( 'zz.out' , 't.asym' , done=2 , new=2 )
	print('src2',file=open('src','w')) ; ut.lmake( 'zz.out' , changed=1 , done=1 )                               # zz.out now has a first and a last entry for A
	os.chdir('../B') ; ut.lmake( 'aa.out' , 't.asym' , done=2 , new=2 )                                          # t.asym does not hit as x is different
	chk_cache()
	files = run_files()
	assert len(files)==10,files                                                                                  # 5 runs (2 for t.asym, 2 for zz.out)
	old   = time.time()-1000
	key_b = [ f for f in files if f.startswith('../CACHE/aa.out/') ][0].rsplit('/',1)[1].split('-')[0]           # aa.out is only built in B, so its run gives B key
	for f in files :
		if f.startswith('../CACHE/t.asym/') and f.rsplit('/',1)[1].startswith(key_b+'-') : os.utime(f,(old,old)) # make B run of t.asym the oldest so it is inserted first upon repair
	print('files :',files,'key_b :',key_b)

	#
	# repair a sane cache : must be a no-op for jobs, but repo keys are renumbered (aa.out, from B, comes first)
	#

	res = repair('-f')
	assert 'conflicts' in res,res                                    # A run of t.asym matches B run (which is more general), it is dropped
	chk_cache()
	files2 = run_files()
	assert len(files2)==8,files2
	print('key renumbering :',sorted({f.rsplit('/',1)[1].split('-')[0] for f in files}),'->',sorted({f.rsplit('/',1)[1].split('-')[0] for f in files2}))
	res = repair('-n')
	assert 'rm ' not in res,res                                      # repair is idempotent
	#
	fresh() ; ut.lmake( 'zz.out' , 't.asym' , hit_done=2 , new=... ) # A must still hit the cache after renumbering
	os.chdir('../B')
	fresh() ; ut.lmake( 'aa.out' , 't.asym' , hit_done=2 , new=... ) # idem for B
	chk_cache()

	#
	# damage cache
	#

	def job_dir(target) :
		for d,_,fs in os.walk('../CACHE/'+target) :
			if fs : return d
		raise RuntimeError(f'no run for {target}')
	aa_dir = job_dir('aa.out')
	zz_dir = job_dir('zz.out')
	os.symlink('nowhere'         , aa_dir+'/stray_lnk' )
	os.makedirs(                   aa_dir+'/stray_dir' )
	open(                          aa_dir+'/stray_dir/f','w')
	open(                          aa_dir+'/empty'      ,'w')
	os.makedirs('../CACHE/empty_job/sub')
	for f in os.listdir(zz_dir) :
		if f.endswith('-info') :                          # zz.out run is lost
			os.unlink(osp.join(zz_dir,f))                 # cache files are read-only
			open(osp.join(zz_dir,f),'w').write('garbage')
	res = repair('-f')
	for f in ('stray_lnk','stray_dir','empty','empty_job','zz.out') : assert f in res,(f,res)
	chk_cache()
	files3 = run_files()
	assert len(files3)==4,files3                          # aa.out and t.asym
	assert not osp.exists(aa_dir+'/stray_lnk')
	assert not osp.exists('../CACHE/empty_job')
	res = repair('-n')
	assert 'rm ' not in res,res                           # repair is idempotent
	#
	fresh() ; ut.lmake( 'aa.out' , 't.asym' , hit_done=2 , new=... )
	os.chdir('../A')
	fresh() ; ut.lmake( 'zz.out' , done=1 , new=... )     # zz.out is lost, it reruns (and is re-uploaded)
	chk_cache()

	#
	# phantom entry : data disappears without repair, next upload must replace entry
	#

	zz_dir = job_dir('zz.out')
	for f in os.listdir(zz_dir) :
		if f.endswith('-data') : os.unlink(osp.join(zz_dir,f))
	fresh() ; ut.lmake( 'zz.out' , bad_cache_download=1 , done=1 , new=... ) # entry cannot be downloaded, job reruns and replaces entry
	fresh() ; ut.lmake( 'zz.out' , hit_done=1                    , new=... ) # entry is now sane again
	chk_cache()

	#
	# stray file in a job dir must not kill lcache_server when job is victimized
	#

	zz_dir = job_dir('zz.out')
	open(zz_dir+'/stray','w')
	print( 'size = 3000' , file=open('../CACHE/LMAKE/config.py','w') )                     # cache can hold 2 runs, not 3
	for i in range(3) :                                                                    # all pre-existing jobs are victimized, including zz.out whose dir cannot be removed
		fresh() ; ut.lmake( f'new{i}.out' , done=1 , new=... )
	chk_cache()
	assert not [ f for f in os.listdir(zz_dir) if f.endswith('-data') ],os.listdir(zz_dir)
	res = repair('-f')                                                                     # stray file is cleaned up
	assert 'zz.out' in res,res
	chk_cache()
	assert not osp.exists(zz_dir)
	fresh() ; ut.lmake( 'new2.out' , hit_done=1 , new=... )
