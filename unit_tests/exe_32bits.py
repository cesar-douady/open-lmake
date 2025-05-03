# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
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

	class Compile(Rule) :
		targets = { 'EXE' : r'{File:.*}-{Sz:32|64}' }
		deps    = { 'SRC' :  '{File   }.c'          }
		autodep = 'ld_preload'                                                                 # clang seems to be hostile to ld_audit
		cmd     = f'PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -m{{Sz}} -O0 -g -o {{EXE}} -xc {{SRC}}'

	class Dut(Rule) :
		target  = r'dut-{Sz:32|64}.{Method:\w+}'
		autodep = '{Method}'
		deps    = { 'EXE':'hello_world-{Sz}' }
		cmd     = './{EXE}'

	class Test(Rule) :
		target = r'test-{Sz:32|64}.{Method:\w+}'
		deps   = {
			'DUT' : 'dut-{Sz}.{Method}'
		,	'REF' : 'ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os
	import subprocess as sp

	from lmake import multi_strip
	import ut

	if not os.environ['HAS_32'] :
		print('no 32 bits support',file=open('skipped','w'))
		exit()

	ut.mk_gxx_module('gxx')

	open('hello_world.c','w').write(multi_strip(r'''
		#include <fcntl.h>
		#include <stdio.h>
		int main() {
			int fd = open("dep",O_RDONLY) ; // just create a dep
			printf("hello world\n") ;
		}
	'''))
	print('hello world',file=open('ref','w'))

	bad_32   = 'ptrace' in lmake.autodeps # 32 bits with ptrace is not supported
	autodeps = ('none',*lmake.autodeps)

	ut.lmake(
		*(f'test-{sz}.{m}' for sz in (64,32) for m in autodeps)
	,	new    = 2
	,	done   = 2 + 2*(2*len(autodeps)-bad_32)
	,	failed = bad_32
	,	rc     = bad_32
	)
