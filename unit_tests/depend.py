# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
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
		cmd    = f'echo hello.{step.p>=2}.{step.interp}.{step.cmd}.{step.autodep}.{step.link_support}'

	class Cpy(Base) :
		target = f'{{File}}.{step.interp}.{step.cmd}.{step.autodep}.{step.link_support}.cpy'
		if step.interp=='sh' :
			if step.cmd=='acc' :
				cmd = 'cat $File'
			elif step.cmd=='dep' :
				cmd = 'ldepend $File ; echo yes'
		elif step.interp=='py' :
			if step.cmd=='acc' :
				def cmd() :
					sys.stdout.write(open(File).read())
			elif step.cmd=='dep' :
				def cmd() :
					lmake.depend(File)
					print('yes')

else :

	import ut

	autodeps = []
	if lmake.has_ptrace     : autodeps.append('ptrace'    )
	if lmake.has_ld_audit   : autodeps.append('ld_audit'  )
	if lmake.has_ld_preload : autodeps.append('ld_preload')
	for autodep in autodeps :
		for link_support in ('none','file','full') :
			for interp in ('sh','py') :
				for cmd in ('acc','dep') :
					for p in range(3) :
						print(f'p={p!r}\ninterp={interp!r}\ncmd={cmd!r}\nautodep={autodep!r}\nlink_support={link_support!r}',file=open('step.py','w'))
						ut.lmake( f'hello.{interp}.{cmd}.{autodep}.{link_support}.cpy' , may_rerun=(p==0) , done=(p!=1)+(p!=1 and cmd=='acc') , steady=(p!=1 and cmd=='dep')  )
