# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# check that uploads reserved in cache are dismissed without crashing lcache_server :
# - job is ok for job_exec (upload is done) but in error for lmake_server => lmake_server dismisses upload
# - upload fails after upload key is acquired                             => job_exec dismisses upload
# - lmake_server dies after upload, before end of job is reported         => upload is dismissed when connection closes, job_exec dismiss must be harmless
#                                                                            and lmake must see lmake_server death although lcache_server is still alive

import ut

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'ut.py'
	)

	lmake.config.caches.my_cache = { 'dir':lmake.repo_root+'/CACHE' }

	class Err(Rule) :
		target = 'err'
		cache  = 'my_cache'
		cmd    = 'echo out ; echo err >&2' # stderr is not allowed, job is in error after upload

	class NoUpload(PyRule) :
		target = 'no_upload'
		cache  = 'my_cache'
		def cmd() :
			ut.trigger_sync(0) # let test script prevent upload
			ut.wait_sync   (1)
			print('no_upload')

	class Big(Rule) :
		target      = 'big'
		cache       = 'my_cache'
		compression = ('zlib',9) # make upload slow enough for lmake_server to be killed meanwhile
		cmd         = '''
			python3 -c 'import ut ; ut.trigger_sync(2) ; ut.wait_sync(3)'
			head -c 32000000 /dev/urandom
		'''

else :

	import os
	import signal
	import socket
	import stat
	import subprocess
	import time

	ut.mk_syncs(4)

	os.makedirs( 'CACHE/LMAKE' , mode=stat.S_ISGID|stat.S_IRWXU|stat.S_IRWXG )
	print('size = 1<<30',file=open('CACHE/LMAKE/config.py','w'))

	def mrkr(file) :                                                       # marker is made of 2 lines : service and pid
		try                      : service,pid = open(file).read().split()
		except FileNotFoundError : return None,0
		return service,int(pid)

	def alive(pid) :                                                                                        # zombies are dead
		try                      : return open(f'/proc/{pid}/stat').read().rsplit(')',1)[1].split()[0]!='Z'
		except FileNotFoundError : return False

	def chk_lcache(after) :   # lcache_server must exit cleanly once its last connection is closed
		for _ in range(100) :
			pid = mrkr('CACHE/LMAKE/server')[1]
			if not pid        : return
			if not alive(pid) : raise RuntimeError(f'lcache_server crashed after {after}')
			time.sleep(0.1)
		raise RuntimeError(f'lcache_server still alive 10s after {after}')

	def job_execs() :
		res = []
		for p in os.listdir('/proc') :
			if not p.isdigit() : continue
			try :
				if os.readlink(f'/proc/{p}/exe').endswith('/job_exec') and os.readlink(f'/proc/{p}/cwd')==os.getcwd() : res.append(int(p))
			except OSError : pass
		return res

	# lmake_server dismisses upload
	ut.lmake( 'err' , failed=1 , rc=1 )
	chk_lcache('lmake_server dismissed upload')

	# job_exec dismisses upload, lmake_server is alive
	proc = ut.lmake( 'no_upload' , wait=False , new=... , done=1 )
	ut.wait_sync(0)
	os.chmod('CACHE/LMAKE/reserved',stat.S_IRUSR|stat.S_IXUSR) # job_exec cannot write data after upload key is acquired
	ut.trigger_sync(1)
	proc()
	chk_lcache('job_exec dismissed upload')
	os.chmod('CACHE/LMAKE/reserved',stat.S_IRWXU)

	# job_exec dismisses upload, lmake_server is dead
	proc = ut.lmake( 'big' , wait=False , rc=... )
	ut.wait_sync(2)
	service,lcache_pid = mrkr('CACHE/LMAKE/server')
	port               = int(service.split(':')[1].split('/')[0])
	other_repo         = socket.create_connection(('127.0.0.1',port)) # emulate another repo using the same cache so lcache_server stays alive
	ut.trigger_sync(3)
	t0 = time.time()
	while not any( f.endswith('-data') for f in os.listdir('CACHE/LMAKE/reserved') ) :
		if time.time()>t0+30 : raise RuntimeError('upload did not start')
		time.sleep(0.001)
	os.kill( mrkr('LMAKE/server')[1] , signal.SIGKILL )               # upload is on going, job end cannot be reported
	try                              : proc.proc.wait(timeout=30)
	except subprocess.TimeoutExpired : raise RuntimeError('lmake did not see lmake_server death')
	for _ in range(300) :
		if not job_execs() : break
		time.sleep(0.1)
	else :
		raise RuntimeError('job_exec still alive')
	if not alive(lcache_pid) : raise RuntimeError('lcache_server crashed after job_exec dismissed upload of a dead lmake_server')
	other_repo.close()
	chk_lcache('job_exec dismissed upload of a dead lmake_server')
