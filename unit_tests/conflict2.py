# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class A(Rule):
		target = 'a'
		def cmd(): pass

	class B(Rule):
		targets   = { 'B' :   'b'                 }
		side_deps = { 'A' : (r'a{*:.*}','Ignore') }
		def cmd():
			open(A('')    )
			open(B    ,'w')

else :

	import ut
	ut.lmake(     'b',new=1,failed=1,rc=1) # a does not exist
	ut.lmake(     'a',      done  =1     )
	ut.lmake(     'b',      done  =0,rc=1) # b is up to date
	ut.lmake('-e','b',      done  =1     ) # b is remade because it was in error
	ut.lmake(     'a'                    ) # check a is not remade because b ran
