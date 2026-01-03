# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import os
	import os.path as osp

	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'../'
	)

	class Dut(Rule) :
		views  = { '/lmake_bin':osp.dirname(osp.dirname(os.getcwd()))+'/bin' }
		target = 'dut'
		cmd    = 'stat /lmake_bin/lmake'

else :

	import ut

	ut.lmake( 'dut' , done=1 )
