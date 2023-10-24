# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import re
import subprocess as sp

def lmake(*args,rc=0,summary={},**kwds) :

	kwds.setdefault('start',...)

	try :

		cmd         = ('lmake',*args)
		print()
		print( '+ ' + ' '.join(cmd) )
		proc        = sp.run( cmd , universal_newlines=True , stdin=None , stdout=sp.PIPE )
		print(proc.stdout,end='',flush=True)
		if proc.returncode!=rc : raise RuntimeError(f'bad return code {proc.returncode} != {rc}')
		sp.run( ('ldump',) , universal_newlines=True , stdin=None , stdout=sp.PIPE , check=True )

		cnt          = { k:0 for k in kwds    }
		sum_cnt      = { k:0 for k in summary }
		seen_summary = False
		for l in proc.stdout.splitlines() :
			if l=='| SUMMARY |' : seen_summary = True
			if seen_summary :
				for k in summary :
					m = re.fullmatch(k,l)
					if m : sum_cnt[k] += 1
			else :
				m = re.fullmatch(r'(?P<key>\w+) .*',l)
				if m :
					k = m.group('key')
					if k not in cnt : raise RuntimeError(f'unexpected key {k}')
					cnt[k] += 1
		for k,v in list(kwds.items()) :
			if v==... :
				del cnt [k]
				del kwds[k]
		if cnt!=kwds :
			for k in cnt :
				if cnt[k]!=kwds[k] : raise RuntimeError(f'bad count for {k} : {cnt[k]} != {kwds[k]}')
		if sum_cnt!=summary :
			for k in sum_cnt :
				if sum_cnt[k]!=summary[k] : raise RuntimeError(f'bad count for summary {k} : {sum_cnt[k]} != {summary[k]}')

	except RuntimeError as e :
		print('*** '+e.args[0])
		raise
