# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class Tmp(Rule) :  # tmp is used both as a plain dep for B and as a scratchpad for A
		target = 'tmp'
		cmd    = ''

	class A(Rule) :
		target = 'a'
		cmd    = 'echo >tmp ; rm tmp ; exit 0'

	class B(Rule) :
		target = 'b'
		dep    = 'a'
		cmd    = 'cat tmp a'

else :

	import ut

	ut.lmake( 'tmp' , done=1 ) # ensure tmp exists
	#
	cnt = ut.lmake( 'b'   , may_rerun=1 , done=... , rerun=... , steady=... , was_done=1 ) # tmp is unlinked by a, then regenerated for b
	# tmp may be to new for b to be done right away
	if   cnt.done==2 and cnt.rerun==0 and cnt.steady==0 : pass # normal situation
	elif cnt.done==1 and cnt.rerun==1 and cnt.steady==1 : pass # tmp is too new, b is rerun
	else :
		assert False,f'bad counts {cnt}'
