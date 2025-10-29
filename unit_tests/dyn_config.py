
# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import ut

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	from step import step

	lmake.manifest = [
		'Lmakefile.py'
	,	'ut.py'
	,	'step.py'
	,	'src'
	]
	if step==2 : lmake.manifest.append('new_src')

	lmake.config.console.date_precision = 3

	class Wait(PyRule) :
		target = 'hold_server'
		deps   = { 'SRC' : 'src' }
		def cmd() :
			ut.trigger_sync(0)
			ut.wait_sync   (1)
			sys.stdout.write(open(SRC).read())

	class Cpy(Rule) :
		target = r'{File:.*}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

else :

	import os
	import os
	import sys
	print(os.environ['PYTHONPATH'],flush=True)
	print(sys.path,flush=True)

	ut.mk_syncs(2)

	print('step=1',file=open('step.py','w'))
	print('src'   ,file=open('src'    ,'w'))

	proc = ut.lmake( '-s' , 'hold_server' , wait=False , new=2 , done=1 )
	#
	ut.wait_sync(0)
	#
	ut.lmake( 'src.cpy' , fast_exit=True , done=1 ) # this is ok, config modif can be done dynamically
	#
	ut.file_sync()
	print('new_src',file=open('new_src','w'))       # create new source
	print('step=2' ,file=open('step.py','w'))       # .
	#
	ut.lmake( 'new_src' , fast_exit=True , rc=5 )   # cannot dynamically modify sources
	#
	ut.trigger_sync(1)
	#
	proc( fast_exit=True )                          # -s option ensures server is done even with fast_exit
	#
	ut.lmake( 'new_src' , new=1 )                   # can build target once server is steady
