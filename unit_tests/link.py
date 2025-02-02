# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

date_prec = 1

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.config.disk_date_precision = date_prec

	lmake.manifest = (
		'Lmakefile.py'
	,	'a/src'
	)

	class MkB(Rule) :
		targets = { 'LNK' : 'b' }
		cmd     = 'ln -s a {LNK}'

	class Test(Rule) :
		target = 'test'
		cmd    = 'cat b/src'

else :

	import os
	import time

	import ut

	os.makedirs('a',exist_ok=True)
	print('src',file=open('a/src','w'))
	time.sleep(date_prec)               # ensure source a/src is old enough

	ut.lmake( 'test' , may_rerun=1 , done=2 , new=1 )
