# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = ('Lmakefile.py',)

	class GenFile(PyRule) :
		target = 'file_{:\d+}'
		def cmd() :
			for x in range(1000) : print(x)

	class Trig(PyRule) :
		target = 'out_{N:\d+}'
		def cmd() :
			lmake.depend([f'file_{x}' for x in range(int(N))])

else :

	import ut

	n = 1000
	ut.lmake( f'out_{n}' , may_rerun=1 , done=n , steady=1 )
