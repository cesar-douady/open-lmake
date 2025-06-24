# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dut'
	,	'dut.ref'
	)

	class Fmt(Rule) :
		target = '{File:.*}.fmt'
		deps   = {'SRC':'{File}'}
		cmd = '''
			ltarget -s {SRC}
			# mimic clang-format with a straight forward formatter : suppress blank lines
			# more severe as SRC is alwasys written, even with same content (which clang-format does not do)
			# to exactly mimic clang-format :
			# sed -i -e'/^$/ d' {SRC} >$TMPDIR/fmt ; cmp {SRC} $TMPDIR/fmt || cp $TMPDIR/fmt {SRC}
			sed -i -e'/^$/ d' {SRC}
		'''

	class Ok(Rule) :
		target = '{File:.*}.ok'
		deps = {
			'FMT' : '{File}.fmt'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {File} >&2'

else :

	import os
	import os.path as osp

	import ut

	open('dut.ref','w').write('hello\n')
	open('dut'    ,'w').write('hello\n\n')

	ut.lmake( 'dut.ok' , new=2 , rerun=1 , done=2 ) # check reformat occurs (rerun because dep has changed during first run)
	ut.lmake( 'dut.ok'                            ) # check build is stable

	open('dut','a').write('\n')                           # mimic an edit session
	ut.lmake( 'dut.ok' , changed=1 , rerun=1 , steady=2 ) # check reformat is redone
