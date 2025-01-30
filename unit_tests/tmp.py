# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	from lmake       import multi_strip
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Base(Rule) :
		stems = { 'N' : r'\d' }

	class Tmp(Base) :
		target   = 'dut{N}'
		targets  = { 'LNK' : 'lnk{N}' }
		tmp_view = '/tmp'
		environ  = { 'REPO_ROOT' : '$REPO_ROOT' }
		cmd = multi_strip('''
			ln -s $TMPDIR/a        {LNK}
			ln -s $REPO_ROOT/{LNK} $TMPDIR/b
			cd $TMPDIR
			sleep 1      # ensure one will overwrite the other in cas of clash
			echo $TMPDIR
			pwd
			echo {N} > a
			cat a
			cd $REPO_ROOT
			cat {LNK}
			cat $TMPDIR/b
		''')

	class Ref(Base) :
		target = 'ref{N}'
		cmd    = multi_strip('''
			echo /tmp         # echo $TMPDIR
			echo /tmp         # pwd
			echo {N}          # cat a
			echo {N}          # cat LNK
			echo {N}          # cat /tmp/b
		''')

	class Cmp(Base) :
		target = 'ok{N}'
		deps   = {
			'DUT' : 'dut{N}'
		,	'REF' : 'ref{N}'
		}
		cmd = 'diff {REF} {DUT}'

	class GenDir(Base) :
		targets  = { 'DST' : r'{File:.*}.dir/{*:.*}' }
		tmp_view = '/tmp'
		environ  = { 'REPO_ROOT' : '$REPO_ROOT' }
		cmd = '''
			cd $TMPDIR
			mkdir d
			echo a >d/a
			echo b >d/b
			mkdir d/c
			cp -a d $REPO_ROOT/{File}.dir/d
		'''

	class UpdateDir(Base) :
		targets  = { 'DST' : r'{File:.*}.dir2/{*:.*}' }
		deps     = { 'SRC' :  '{File}.dir/d/a'        }
		tmp_view = '/tmp'
		environ  = { 'REPO_ROOT' : '$REPO_ROOT' }
		cmd      = '''
			cd $TMPDIR
			cp -a $REPO_ROOT/{File}.dir/d d
			a=$(cat d/a)
			echo $a:aa >d/a
			cp -a d $REPO_ROOT/{File}.dir2/d
		'''

	class ChkDir(Base) :
		target = r'{File:.*}.chk'
		deps   = { 'DUT' : '{File}.dir2/d/a' }
		cmd    = '''
			[ $(cat {DUT}) = a:aa ]
		'''

else :

	import ut

	ut.lmake( 'ok1','ok2' , done=6 ) # check target is out of date
	ut.lmake( 'test.chk'  , done=3 )
