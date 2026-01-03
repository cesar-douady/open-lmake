# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import re
import os
import os.path    as osp
import shutil
import subprocess as sp
import sys
import time

from lmake import pdict

class Ut :
	idx = 0
	def __init__( self , *args , rc=0 , no_dump=False , fast_exit=False , host_len=None , **kwds ) :
		self.__class__.idx += 1
		self.stdout = f'tok.{self.idx}'
		kwds.setdefault('start',...)
		#
		self.rc        = rc
		self.no_dump   = no_dump
		self.fast_exit = fast_exit
		self.host_len  = host_len
		self.kwds      = kwds
		#
		cmd = ('lmake',*args)
		now = time.time()
		print(                                                                  )
		print( f'{time.ctime(now).rsplit(None,1)[0]}.{(str(now%1)+"000")[2:5]}' ) # generate date with ms precision
		print( '+ ' + ' '.join(cmd)                                             )
		self.proc = sp.Popen( cmd , universal_newlines=True , stdin=None , stdout=open(self.stdout,'w') )
		sys.stdout.flush()
	def __call__( self , rc=0 , no_dump=False , fast_exit=True , **kwds ) :
		if rc        : self.rc        = rc
		if no_dump   : self.no_dump   = no_dump
		if fast_exit : self.fast_exit = fast_exit
		if kwds      : self.kwds.update(kwds)
		self.proc.wait()
		stdout       = open(self.stdout).read()
		sys.stdout.write('\n'  )
		sys.stdout.write(stdout)
		sys.stdout.flush(      )
		#
		if self.proc.returncode!=self.rc : raise RuntimeError(f'bad return code {self.proc.returncode} != {self.rc}')
		if not self.no_dump              : sp.run( ('lmake_dump',) , universal_newlines=True , stdin=None , stdout=sp.PIPE , check=True )
		#
		if not self.fast_exit and osp.exists('LMAKE/server') :
			time_out = 10
			for i in range(int(10*time_out)) :
				time.sleep(0.1)
				if not osp.exists('LMAKE/server') : break
			else :
				raise RuntimeError(f'server is still alive after {time_out}s')
		#
		# analysis
		cnt          = { k:0 for k in self.kwds }
		seen_summary = False
		self.summary = ''
		for l in stdout.splitlines() :
			if l=='| SUMMARY |' : seen_summary = True
			if seen_summary :
				self.summary += l+'\n'
				continue
			if self.host_len : m = re.fullmatch(rf"(\d\d:\d\d:\d\d(\.\d+)? )?{'.'*self.host_len} (?P<key>\w+) .*",l)
			else             : m = re.fullmatch(r'(\d\d:\d\d:\d\d(\.\d+)? )?(?P<key>\w+) .*'                     ,l)
			if not m : continue
			k = m.group('key')
			if k in cnt : cnt[k] += 1
			else        : cnt[k]  = 1
		#
		# wrap up
		res = pdict()
		for k,v in list(self.kwds.items()) :
			if v==... :
				res[k] = cnt[k]
				del cnt      [k]
				del self.kwds[k]
		if cnt!=self.kwds :
			for k in cnt :
				if cnt[k]!=self.kwds[k] : raise RuntimeError(f'bad count for {k} : {cnt[k]} != {self.kwds[k]}')
		#
		return res

def lmake( *args , rc=0 , no_dump=False , wait=True , **kwds ) :
	proc = Ut( *args , rc=rc , no_dump=no_dump , **kwds )
	if wait : return proc()
	else    : return proc

def mk_syncs(n) :
	os.makedirs('LMAKE/lmake',exist_ok=True)
	for i in range(n) : os.mkfifo(f'LMAKE/lmake/sync{i}')
	os.symlink(__file__,f'{__name__}.py')

def trigger_sync(i) :
	open(f'LMAKE/lmake/sync{i}','w').write('')

def wait_sync(i) :
	open(f'LMAKE/lmake/sync{i}').read()

def file_sync() :
	time.sleep(0.1) # ensure file date granularity see order, may take as long as 30ms on WSL

def lshow(key,*args) :
	x            = sp.check_output(('lshow',key[0],               *args),universal_newlines=True)
	long_x       = sp.check_output(('lshow',key[1],               *args),universal_newlines=True)
	porcelaine_x = sp.check_output(('lshow',key[1],'--porcelaine',*args),universal_newlines=True)
	assert x==long_x,('/'+x+'/','/'+long_x+'/')
	return x,eval(porcelaine_x)

def mk_gxx_module(module) :
	with open(f'{module}.py','w') as fp :
		gxx     = os.environ.get('CXX') or 'g++'
		gxx_dir = osp.dirname(shutil.which(gxx))
		print(f"gxx     = {gxx!r}"    ,file=fp)
		print(f"gxx_dir = {gxx_dir!r}",file=fp)
