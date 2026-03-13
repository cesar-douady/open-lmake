# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	def server_start_proc() : print('server_start_proc',file=open('procs','a'))
	def server_end_proc  () : print('server_end_proc'  ,file=open('procs','a'))
	def req_start_proc   () : print('req_start_proc'   ,file=open('procs','a'))
	def req_end_proc     () : print('req_end_proc'     ,file=open('procs','a'))

	lmake.config.server_start_proc = server_start_proc
	lmake.config.server_end_proc   = server_end_proc
	lmake.config.req_start_proc    = req_start_proc
	lmake.config.req_end_proc      = req_end_proc

else :

	import ut

	ut.lmake()

	procs = open('procs').read()
	assert procs==(
		'server_start_proc\n'
	+	'req_start_proc\n'
	+	'req_end_proc\n'
	+	'server_end_proc\n'
	),procs
