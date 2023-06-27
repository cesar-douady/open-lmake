# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import subprocess as sp
import sys
import time

import lmake

if getattr(sys,'reading_makefiles',False) :

	import step

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	)

	lmake.config.link_support = step.link_support

	class Base(lmake.Rule) :
		stems        = { 'File' : r'.*' }
		autodep      = step.autodep
		link_support = step.link_support

	class Src(Base) :
		target = 'hello'
		cmd    = f'echo hello.{step.p>=2}.{step.interp}.{step.autodep}.{step.link_support}'

	class Cpy(Base) :
		target = f'{{File}}.{step.interp}.{step.autodep}.{step.link_support}.cpy'
		if step.interp=='sh' :
			cmd = '''
				from_server=$(ldepend -v $File | awk '{print $2}')
				expected=$(   xxhsum     $File                   )
				echo $from_server
				[ $from_server = $expected ] || echo expected $expected got $from_server >&2
			'''
		elif step.interp=='py' :
			def cmd() :
				from_server = lmake.depend(File,verbose=True)[File][1]
				expected    = sp.check_output(('xxhsum',File),universal_newlines=True).strip()
				assert from_server==expected,f'expected {expected} got {from_server}'
				print(from_server)

else :

	import ut

	autodeps = []
	if lmake.has_ptrace     : autodeps.append('ptrace'    )
	if lmake.has_ld_audit   : autodeps.append('ld_audit'  )
	if lmake.has_ld_preload : autodeps.append('ld_preload')
	for autodep in autodeps :
		for link_support in ('none','file','full') :
			for interp in ('sh','py') :
				for p in range(3) :
					print(f'p={p!r}\ninterp={interp!r}\nautodep={autodep!r}\nlink_support={link_support!r}',file=open('step.py','w'))
					ut.lmake( f'hello.{interp}.{autodep}.{link_support}.cpy' , may_rerun=(p==0) , done=(p!=1)*2 )
