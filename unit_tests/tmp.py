# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'lmake_read_makefiles',False) :

	import lmake

	lmake.sources = ('Lmakefile.py',)

	class Base(lmake.Rule) :
		stems = { 'N' : r'\d' }

	class Tmp(Base) :
		target  = 'dut{N}'
		targets = { 'LNK' : 'lnk{N}' }
		tmp = '/tmp'
		cmd = '''
			ln -s /tmp/a          {LNK}
			ln -s $ROOT_DIR/{LNK} /tmp/b
			cd $TMPDIR
			sleep 1                    # ensure one will overwrite the other in cas of clash
			echo $TMPDIR
			pwd
			echo {N} > a
			cat a
			cd $ROOT_DIR
			cat {LNK}
			cat /tmp/b
		'''
	class Ref(Base) :
		target = 'ref{N}'
		cmd = '''
			echo /tmp                  # echo $TMPDIR
			echo /tmp                  # pwd
			echo {N}                   # cat a
			echo {N}                   # cat LNK
			echo {N}                   # cat /tmp/b
		'''
	class Cmp(Base) :
		target = 'ok{N}'
		deps = {
			'DUT' : 'dut{N}'
		,	'REF' : 'ref{N}'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import ut

	ut.lmake( 'ok1','ok2' , done=6 )   # check target is out of date
