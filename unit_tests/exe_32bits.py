# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	import gxx

	lmake.manifest = (
		'Lmakefile.py'
	,	'gxx.py'
	,	'hello_world.c'
	,	'ref'
	)

	class Base(Rule) :
		stems = { 'Sz' : r'32|x32|64' }

	class Compile(Base) :
		targets = { 'EXE' : r'{File:.*}-{Sz}' }
		deps    = { 'SRC' :  '{File   }.c'    }
		autodep = 'ld_preload'                                                                 # clang seems to be hostile to ld_audit
		cmd     = f'PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -m{{Sz}} -O0 -g -o {{EXE}} -xc {{SRC}}'

	class Dut(Base) :
		target  = r'dut-{Sz}.{Method:\w+}'
		autodep = '{Method}'
		deps    = { 'EXE':'hello_world-{Sz}' }
		cmd     = './{EXE}'

	class Test(Base) :
		target = r'test-{Sz}.{Method:\w+}'
		deps   = {
			'DUT' : 'dut-{Sz}.{Method}'
		,	'REF' : 'ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os
	import subprocess as sp
	import textwrap

	import ut

	if not os.environ['HAS_32'] :
		print('no 32 bits support',file=open('skipped','w'))
		exit()

	ut.mk_gxx_module('gxx')

	open('hello_world.c','w').write(textwrap.dedent(r'''
		#include <fcntl.h>
		#include <stdio.h>
		int main() {
			int fd = open("dep",O_RDONLY) ; // just create a dep
			printf("hello world\n") ;
		}
	'''[1:]))
	print('hello world',file=open('ref','w'))

	autodeps = ('none',*lmake.autodeps)

	ut.lmake(
		*(f'test-{sz}.{m}' for sz in ('32',64) for m in autodeps) # x32 is generally not supported
	,	new    = 2
	,	done   = 2 + 4*len(autodeps)
	)
