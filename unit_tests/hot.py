# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	lmake.config.disk_date_precision = 2

	class Dep(Rule) :
		target = 'dep'
		cmd    = ''

	class Dut(Rule) :
		target = r'dut.{S:\d+}'
		cmd    = 'sleep {S} ; cat dep'

else :

	import os
	import os.path as osp

	import ut

	ut.lmake( 'dep' , 'dut.1' , 'dut.3' , done=3 , rerun=1 ) # check dut.1 is rerun despite dep being read 1s after available, but not dut.3
