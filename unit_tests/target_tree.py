# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os
	import os.path as osp

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = ('Lmakefile.py',)

	class Dut(PyRule) :
		targets = { 'DUT':r'dut/{*:.*}' }
		def cmd() :
			open('dut/a1','w')
			open('dut/a2','w')
			open('dut/b' ,'w')
			#
			lmake.cp_target_tree('dut','dut2',regexpr='dut/a.')
			assert     osp.exists('dut/a1' )
			assert     osp.exists('dut/a2' )
			assert     osp.exists('dut/b'  )
			assert     osp.exists('dut2/a1')
			assert     osp.exists('dut2/a2')
			assert not osp.exists('dut2/b' )
			#
			lmake.rm_target_tree('dut','dut/a.')
			assert not osp.exists('dut/a1' )
			assert not osp.exists('dut/a2' )
			assert     osp.exists('dut/b'  )
			#
			lmake.rm_target_tree('dut')
			assert not osp.exists('dut')
			#
			lmake.mv_target_tree('dut2','dut')
			assert     osp.exists('dut/a1' )
			assert     osp.exists('dut/a2' )
			assert not osp.exists('dut2/a1')
			assert not osp.exists('dut2/a2')
			#
			open('dut/b','w')

else :

	import ut

	ut.lmake( 'dut/b' , done=1 )
