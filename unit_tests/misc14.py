# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys
	import time

	import lmake
	from lmake.rules import Rule

	lmake.manifest = ('Lmakefile.py',)

	class DutSh(Rule) :
		targets = { 'DUT' : r'dut_sh{N*:\d}' }
		cmd     = '''
			[ -f dut_sh1 ] && exit 1 ; echo 1 >dut_sh1
			[ -f dut_sh2 ] && exit 1 ; echo 2 >dut_sh2
		'''

	class DutPy(Rule) :
		targets = { 'DUT' : r'dut_py{N*:\d}' }
		def cmd() :
			print(1,file=open('dut_py1','x'))
			print(2,file=open('dut_py2','x'))

else :

	import ut

	print('manual',file=open('dut_sh2','w'))
	print('manual',file=open('dut_py2','w'))
	ut.lmake( 'dut_sh1' , rerun=1 , done=1         ) # check dut_sh2 is quarantined and job rerun
	ut.lmake( 'dut_py1' , rerun=1 , done=1 , new=1 ) # check dut_py2 is quarantined and job rerun
