# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import sys

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
		cmd    = 'sleep 0.5'

	class Src(Base) :
		target = 'hello'
		dep    = 'dly'                                         # ensure hello construction does not start too early, so that we are sure that we have may_rerun messages, not rerun
		cmd    = f'echo hello.{step.p>=2}.{step.link_support}'

	for ad in lmake.autodeps :
		class CpyShAcc(Base) :
			name    = f'cpy-sh-acc-{ad}'
			autodep = ad
			target  = f'{{File}}.sh.acc.{ad}.{step.link_support}.cpy'
			cmd     = 'cat {File}'
		class CpyShDep(Base) :
			name    = f'cpy-sh-dep-{ad}'
			autodep = ad
			target  = f'{{File}}.sh.dep.{ad}.{step.link_support}.cpy'
			cmd     = 'ldepend {File} ; echo yes'
		class CpyPyAcc(Base,PyRule) :
			name    = f'cpy-py-acc-{ad}'
			autodep = ad
			target  = f'{{File}}.py.acc.{ad}.{step.link_support}.cpy'
			def cmd() :
				sys.stdout.write(open(File).read())
		class CpyPyDep(Base,PyRule) :
			name    = f'cpy-py-dep-{ad}'
			autodep = ad
			target  = f'{{File}}.py.dep.{ad}.{step.link_support}.cpy'
			def cmd() :
				lmake.depend(File,'/usr/bin/x') # check external dependencies are ok
				print('yes')

else :

	import os
	import shutil

	import ut

	n_ads = len(lmake.autodeps)
	print(f'lmake.autodeps : {lmake.autodeps}')

	#
	for ls in ('none','file','full') :
		try                      : shutil.rmtree('LMAKE')
		except FileNotFoundError : pass
		for f in ('dly','hello') :
			try                      : os.unlink(f)
			except FileNotFoundError : pass
		print(f'p=0\nlink_support={ls!r}',file=open('step.py','w'))
		ut.lmake( 'Lmakefile.py' , new=1 )                                                     # prevent new Lmakefile.py in case of error as python reads it to display backtrace
		tgts = [
			f'hello.{interp}.{cmd}.{ad}.{ls}.cpy'
			for interp in ('sh','py')
			for cmd    in ('acc','dep')
			for ad     in lmake.autodeps
		]
		for p in range(3) :
			print(f'p={p!r}\nlink_support={ls!r}',file=open('step.py','w'))
			if p==0 :
				cnts = ut.lmake( *tgts , may_rerun=... , rerun=... , done=... , was_done=... ) # rerun versus may_rerun is timing dependent, but the sum is predictible
				assert cnts.done+cnts.was_done   == 2+4*n_ads
				assert cnts.may_rerun+cnts.rerun <= 4*n_ads   # depend on timing w.r.t hello, there may be may_rerun (if before), rerun (if during) or nothing (if after)
			elif p==1 :
				ut.lmake( *tgts )
			elif p==2 :
				ut.lmake( *tgts , done=1+2*n_ads )
