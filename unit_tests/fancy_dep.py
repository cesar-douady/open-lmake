# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

ref = 'src1+{src2.version}.ref'

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'src1'
	)

	class Cpy(Rule) :
		stems  = { 'File'  : r'.*' }
		target = r'{File}.cpy'
		dep    = '{File}'
		cmd    = 'cat'

	def dec(x) :
		x = int(x)
		if x==1 : return ''
		else    : return x-1
	class Fstring(Cpy) :
		stems  = { 'Digit' : r'\d+' }
		target = '{File}.cpy{Digit}'
		dep    = '{File}.cpy{dec(Digit)}'

else :

	import os

	import ut

	print('#src1',file=open('src1','w'))

	ut.lmake( 'src1.cpy2' , new=1 , done=3 )
