# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	import step as step_mod
	from   step import step

	if step>=5 :
		sys.path.append('/toto')

	def import_step() :
		import step
		return step.step
	def get_step_step() :
		return step_mod.step
	def get_step() :
		return step
	def sourcify() :    # create a conflict with variables internally used by lmake (which manages to avoid such conflicts)
		return sys.path


	class Base(Rule) :
		cmd = 'echo $STEP'

	class BadTarget1(Base) :
		target  = 'bad_target1{Sfx:}'
		environ = {
			'STEP' : import_step # bad : imports are not allowed
		,	'SFX'  : '{Sfx}'     # ensures environ is dynamic (if constant, it is made static)
		}

	if step==1 :
		class BadMakefile(Base) :
			target  = 'bad_makefile'
			environ = { 'STEP' : get_step_step } # bad : imports in context makes a bad makefile
	elif step==2 :
		class BadTarget2(Base) :
			target  = 'bad_target2'
			environ = { 'STEP' : import_step }   # bad : not transformed into static because of disk access

	class OkStep(Base) :
		target  = 'ok_step'
		environ = { 'STEP' : get_step } # ok, imports are not allowed

	class OkSysPath(Base) :
		target = 'ok_sys_path'
		environ = { 'STEP' : sourcify } # ok, imports are not allowed

else :

	import os
	import os.path as osp

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( rc=8 , no_dump=True )          # just reading makefile produces an error

	print('step=2',file=open('step.py','w'))
	ut.lmake( 'bad_target2' , new=1 , early_rerun=1 , failed=1 , rc=1 , no_dump=True ) # cannot find step as sys.path only contains external dirs

	print('step=3',file=open('step.py','w'))
	ut.lmake( 'bad_target1' , early_rerun=1 , failed=1 , rc=1 )
	ut.lmake( 'ok_step'     ,                 done  =1        )
	ut.lmake( 'ok_sys_path' ,                 done  =1        )

	print('step=4',file=open('step.py','w'))
	ut.lmake( 'ok_step' , 'ok_sys_path' , done=1 ) # step changed, sys.path did not

	print('step=5',file=open('step.py','w'))
	ut.lmake( 'ok_step' , 'ok_sys_path' , done=2 ) # step and sys.path changed
