# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	)

	class Bad(Rule) :
		target = 'bad_dep'
		cmd    = 'exit 1'

	class Scratch(Rule) :
		targets      = { 'DST'     :  'dut_scratch'                            }
		side_targets = { 'SCRATCH' : ('scratch'    ,'incremental','source_ok') }
		cmd = '''                                                                # check hello does not become a dep
			echo 2nd line >> scratch
			cat scratch > {DST}
		'''

	class SideTarget(Rule) :
		target       = 'dut_side_target'
		side_targets = { 'BAD' : ('bad_target','ignore') }
		allow_stderr = True
		cmd          = 'cat >bad_target'

	class DynTargetSh(Rule) :
		target       = 'dut_dyn_target_sh'
		allow_stderr = True
		cmd = '''
			ltarget --ignore bad_target
			ldepend          bad_target
			cat             >bad_target
		'''

	class DynTargetPy(PyRule) :
		target       = 'dut_dyn_target_py'
		allow_stderr = True
		def cmd() :
			lmake.target('bad_target',ignore=True)
			open('bad_target','w')

	class SideDep(Rule) :
		target       = 'dut_side_dep'
		side_deps    = { 'BAD' : ('bad_dep','ignore') }
		allow_stderr = True
		cmd          = '! cat bad_dep' # cat must fail as bad_dep must not be produced

	class DynDepSh(Rule) :
		target       = 'dut_dyn_dep_sh'
		allow_stderr = True
		cmd = '''
			ldepend --ignore bad_dep
			! cat bad_dep            # cat must fail as bad_dep must not be produced
		'''

	class DynDepPy(PyRule) :
		target       = 'dut_dyn_dep_py'
		allow_stderr = True
		def cmd() :
			lmake.depend('bad_dep',ignore=True)
			try :
				open('bad_dep')
				return 'bad_dep' # open must fail as bad_dep must not be produced
			except :
				return None      # ok

else :

	import ut

	print('hello',file=open('hello','w'))

	ut.lmake( 'dut_scratch' , 'dut_side_target' , 'dut_dyn_target_sh' , 'dut_dyn_target_py' , 'dut_side_dep' , 'dut_dyn_dep_sh' , 'dut_dyn_dep_py' , done=7 )
