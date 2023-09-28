# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

if getattr(sys,'reading_makefiles',False) :

	import os

	import lmake

	lmake.sources = ('Lmakefile.py',)

	class A(lmake.Rule):
		target = 'a'
		def cmd(): pass

	class B(lmake.Rule):
		targets = {
			'B' : 'b'
		,	'A' : ('a{*:.*}', '-Dep','Incremental','-Match','-Write','-Stat')
		}
		def cmd():
			open('a'    )
			open(B  ,'w')

else :

	import ut
	ut.lmake('a',done  =1)
	ut.lmake('b',done  =1)
	ut.lmake('a',steady=0)                                                     # check a is not remade because b ran
