# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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

def lmake(*args,rc=0,summary=None,**kwds) :
	if not summary : summary = {}
	else           : summary = dict(summary)

	kwds.setdefault('start',...)

	try :

		cmd = ('lmake',*args)
		print()
		print(time.ctime())
		print( '+ ' + ' '.join(cmd) )
		proc = sp.run( cmd , universal_newlines=True , stdin=None , stdout=sp.PIPE )
		print(proc.stdout,end='',flush=True)
		if proc.returncode!=rc : raise RuntimeError(f'bad return code {proc.returncode} != {rc}')
		sp.run( ('ldump',) , universal_newlines=True , stdin=None , stdout=sp.PIPE , check=True )

		if osp.exists('LMAKE/server') :
			time.sleep(1)
			if osp.exists('LMAKE/server') : raise RuntimeError('server is still alive')

		# analysis
		cnt          = { k:0 for k in kwds    }
		sum_cnt      = { k:0 for k in summary }
		seen_summary = False
		for l in proc.stdout.splitlines() :
			if l=='| SUMMARY |' : seen_summary = True
			if seen_summary :
				for k in summary :
					if not re.fullmatch(k,l) : continue
					sum_cnt[k] += 1
					break
			else :
				m = re.fullmatch(r'(\d\d:\d\d:\d\d(\.\d+)? )?(?P<key>\w+) .*',l)
				if not m : continue
				k = m.group('key')
				if k not in cnt : raise RuntimeError(f'unexpected key {k}')
				cnt[k] += 1

		# wrap up
		res = pdict()
		for k,v in list(kwds.items()) :
			if v==... :
				res[k] = cnt[k]
				del cnt [k]
				del kwds[k]
		if cnt!=kwds :
			for k in cnt :
				if cnt[k]!=kwds[k] : raise RuntimeError(f'bad count for {k} : {cnt[k]} != {kwds[k]}')
		if sum_cnt!=summary :
			for k in sum_cnt :
				if sum_cnt[k]!=summary[k] : raise RuntimeError(f'bad count for summary {k} : {sum_cnt[k]} != {summary[k]}')

		return res

	except RuntimeError as e :
		print('*** '+e.args[0])
		raise
	finally :
		sys.stdout.flush()

def mk_gxx_module(module) :
	with open(f'{module}.py','w') as fp :
		gxx     = os.environ.get('CXX') or 'g++'
		gxx_dir = osp.dirname(shutil.which(gxx))
		print(f"gxx     = {gxx!r}"    ,file=fp)
		print(f"gxx_dir = {gxx_dir!r}",file=fp)
