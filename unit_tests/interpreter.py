# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep' , 'dep1' , 'dep2'
	,	*( f'interpreter{i}' for i in range(1,7) )
	)

	class Indirect(Rule) :
		target = r'indirect{N:\d}'
		shell  = ('interpreter{N}',)
		cmd    = ''

	def get_deps() : return { 'D1':'dep1' , 'D2':'dep2' }
	class Dyn(Rule) :
		target = 'dyn'
		dep    = 'dep'
		deps   = get_deps
		shell  = ('interpreter1',)
		cmd    = ''

	def get_dep        () : return 'dep'
	def get_interpreter() : return ('interpreter1',)
	class DynCallable(Rule) :
		target = 'dyn_callable'
		dep    = get_dep
		deps   = get_deps
		shell  = get_interpreter
		cmd    = ''


else :

	import os

	import ut

	if True             : print('dep'                          ,file=open( 'dep'             ,'w'))
	if True             : print('dep1'                         ,file=open( 'dep1'            ,'w'))
	if True             : print('dep2'                         ,file=open( 'dep2'            ,'w'))
	if True             : print(f'#!/bin/bash\necho found "$@"',file=open( 'interpreter1'    ,'w'))
	for i in range(1,6) : print(f'#!interpreter{i}'            ,file=open(f'interpreter{i+1}','w'))
	for i in range(1,7) : os.chmod(f'interpreter{i}',0o755)

	ut.lmake( 'indirect6' , failed=1 , new=5 , rc=1 )         # only recurse 4 times as per POSIX doc, so interpreter1 is not a dep
	ut.lmake( 'indirect5' , done  =1 , new=1        )         # interpreter1 is new as a source
	for i in range(1,5) : ut.lmake( f'indirect{i}' , done=1 ) # sanity check

	ut.lmake( 'dyn'          , done=1 , new=3 )
	ut.lmake( 'dyn_callable' , done=1         )
