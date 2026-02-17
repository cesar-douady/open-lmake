# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import subprocess as sp

	from lmake.rules import Rule,PyRule

	import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	lmake.config.link_support = step.link_support

	class Base(Rule) :
		stems = { 'File' : r'.*' }

	class Delay(Base) :
		target = 'dly'
		cmd    = 'sleep 2'

	class Src(Base) :
		target = 'hello'
		dep    = 'dly'                                         # ensure hello construction does not start too early, so that we are sure that we have may_rerun messages, not rerun
		cmd    = f'echo hello.{step.p>=2}.{step.link_support}'

	for ad in lmake.autodeps :
		class CpySh(Base) :
			name    = f'cpy-sh-{ad}'
			target  = f'{{File}}.sh.{ad}.{step.link_support}.cpy'
			autodep = ad
			cmd = '''
				from_server=$(ldepend -v -R {File} | awk '{{print $2}}')
				expected=$(   xxhsum        {File}                     )
				echo $from_server
				[ "$from_server" = "$expected" ] || echo expected $expected got $from_server >&2
			'''
		class CpyPy(Base,PyRule) :
			name    = f'cpy-py-{ad}'
			target  = f'{{File}}.py.{ad}.{step.link_support}.cpy'
			autodep = ad
			def cmd() :
				from_server = lmake.depend(File,verbose=True,read=True)[File]
				expected    = sp.check_output(('xxhsum',File),universal_newlines=True).strip()
				assert from_server['checksum']==expected,f"expected {expected} got {from_server['checksum']}"
				print(from_server)

else :

	import os
	import shutil

	import ut

	n_ads = len(lmake.autodeps)

	for ls in ('none','file','full') :
		try                      : shutil.rmtree('LMAKE')
		except FileNotFoundError : pass
		for f in ('dly','hello') :
			try                      : os.unlink(f)
			except FileNotFoundError : pass
		for p in range(3) :
			print(f'p={p!r}\nlink_support={ls!r}',file=open('step.py','w'))
			cnts = ut.lmake(
				*( f'hello.{interp}.{ad}.{ls}.cpy' for interp in ('sh','py') for ad in lmake.autodeps )
			,	new=... , may_rerun=... , rerun=... , done=(p==0)+(p!=1)+(p!=1)*2*n_ads
			)
			assert cnts.may_rerun+cnts.rerun==(p==0)*2*n_ads , cnts
			assert cnts.new<=1                                      # python may access Lmakefile.py if it generates a backtrace, which may happen
