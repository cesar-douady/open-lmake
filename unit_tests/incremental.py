# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'dep'
	)

	class Inc(PyRule) :
		targets = {
			'MANIFEST' : ( 'manifest'      , 'incremental' )           # we need old value to compute old/new files
		,	'TGT'      : ( 'dut.{Sfx*:.*}' , 'incremental' )
		}
		deps = { 'DEP' : 'dep' }
		def cmd() :
			old_targets = set()
			new_targets = set()
			#
			if True                  : new_txt = open(DEP     ).read()
			try                      : old_txt = open(MANIFEST).read()
			except FileNotFoundError : old_txt = ''
			for f in new_txt.strip().split() : new_targets.add(TGT(f))
			for f in old_txt.strip().split() : old_targets.add(TGT(f))
			#
			print(old_targets,new_targets)
			open(MANIFEST,'w').write(new_txt)
			for f in new_targets-old_targets : print(file=open(f,'w')) # create new targets
			for f in old_targets-new_targets : os.unlink(f)            # remove old ones

else :

	import ut

	print( 'a' , 'b' , file=open('dep','w') )
	ut.lmake( 'dut.b' , done=1 , new=1 )

	print( 'a' , 'c' , file=open('dep','w') )
	ut.lmake( 'dut.a' , done=1 , changed=1        )
	ut.lmake( 'dut.b' ,                      rc=1 )
	ut.lmake( 'dut.c'                             )
