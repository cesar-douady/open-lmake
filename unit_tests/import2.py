# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'a/b/c.py'
	)

	class Dut(PyRule) :
		target = 'dut'
		environ = { 'PYTHONPATH' : 'a' } # check namespaces with several branches : based on repo root (automatic) and on a
		def cmd() :
			import b.c   # check namespace based import from a
			import a.b.c # check namespace based import from repo root

else :

	import os

	import ut

	os.makedirs('a/b',exist_ok=True)
	os.makedirs('b'  ,exist_ok=True) # this dir is searched, but c.py is not there
	open('a/b/c.py','w')

	ut.lmake( 'dut' , done=1 , new=1 )
