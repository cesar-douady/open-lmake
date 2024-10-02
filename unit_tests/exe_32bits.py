# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake.rules import Rule,PyRule

	gxx = lmake.user_environ.get('CXX') or 'g++'

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello_world.c'
	,	'ref'
	)

	class Compile(Rule) :
		targets = { 'EXE' : r'{File:.*}-{Sz:32|64}' }
		deps    = { 'SRC' :  '{File}.c'             }
		autodep = 'ld_preload'                                                   # clang seems to be hostile to ld_audit
		cmd     = 'PATH=$(dirname {gxx}) {gxx} -m{Sz} -O0 -g -o {EXE} -xc {SRC}'

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

	gxx = os.environ.get('CXX') or 'g++'

	open('hello_world.c','w').write(multi_strip(r'''
		#include <fcntl.h>
		#include <stdio.h>
		int main() {
			int fd = open("dep",O_RDONLY) ; // just create a dep
			printf("hello world\n") ;
		}
	'''))
	print('hello world',file=open('ref','w'))

	methods = ['none','ld_preload']
	if lmake.has_ptrace   : methods.append('ptrace'  )
	if lmake.has_ld_audit : methods.append('ld_audit')

	word_szs = [64]
	if os.environ['HAS_32BITS'] :
		bad_32 = lmake.has_ptrace                                                                            # 32 bits with ptrace is not supported
		word_szs.append(32)
	else :
		bad_32 = False

	ut.lmake(
		*(f'test-{sz}.{m}' for sz in word_szs for m in methods)
	,	new  = 2
	,	done = len(word_szs) + 2*(len(word_szs)*len(methods)-bad_32)
	,	failed=bad_32
	,	rc=bad_32
	)
