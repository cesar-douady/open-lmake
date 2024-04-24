# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

from lmake.rules import Rule

if __name__!='__main__' :

	import lmake
	from lmake       import multi_strip

	lmake.manifest = ('Lmakefile.py',)

	class Base(Rule) :
		stems = { 'N' : r'\d' }

	for tmp in (None,'/tmp','/new_tmp') :
		for repo in (None,'/repo') :
			class Dut(Rule) :
				name   = f'dut {tmp} {repo}'
				target = f'dut.{tmp}{repo}'
				if tmp  : tmp  = tmp
				if repo : root = repo
				cmd = multi_strip('''
					unset PWD                     # ensure pwd calls getcwd
					pwd          > $TMPDIR/stdout
					echo $TMPDIR > $TMPDIR/stdout
					cat $TMPDIR/stdout
				''')
				if tmp  : cmd += f'[ $TMPDIR = {tmp } ] || exit 1\n'
				if repo : cmd += f'[ $(pwd)  = {repo} ] || exit 1\n'
else :

	import ut

	ut.lmake( *(f'dut.{t}{r}' for t in (None,'/tmp','/new_tmp') for r in (None,'/repo') ) , done=6 )
