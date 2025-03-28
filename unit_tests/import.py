# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os

	import lmake
	from   lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'lcl_config.py'
	,	'lcl_job.py'
	)

	from step import step

	import lcl_config
	from lcl_config import lcl_config_var , lcl_config_func

	def get_import_config() :
		return f'{lcl_config.lcl_config_var}{lcl_config.lcl_config_func()}'

	def get_from_config() :
		return f'{lcl_config_var}{lcl_config_func()}'

	def get_import_job() :
		import lcl_job
		return f'{lcl_job.lcl_job_var}{lcl_job.lcl_job_func()}'

	if step==2 :
		class CmdImportConfigSh(Rule) :
			target = 'cmd_import_config_sh'
			cmd    = 'echo {lcl_config.lcl_config_var}{lcl_config.lcl_config_func()}'

	class CmdImportJobSh(Rule) :
		target = 'cmd_import_job_sh'
		cmd    = 'echo {get_import_job()}'

	class CmdFromConfigSh(Rule) :
		target = 'cmd_from_config_sh'
		cmd    = 'echo {lcl_config_var}{lcl_config_func()}'

	class CmdImportConfigPy(PyRule) :
		target = 'cmd_import_config_py'
		def cmd() :
			print(lcl_config.lcl_config_var,lcl_config.lcl_config_func(),sep='')

	class CmdImportJobPy(PyRule) :
		target = 'cmd_import_job_py'
		def cmd() :
			import lcl_job
			print(lcl_job.lcl_job_var,lcl_job.lcl_job_func(),sep='')

	class CmdFromConfigPy(PyRule) :
		target = 'cmd_from_config_py'
		def cmd() :
			print(lcl_config_var,lcl_config_func(),sep='')

	if step==3 :
		class DynAttrImportConfigSh(Rule) :
			target  = 'dyn_attr_import_config_sh'
			environ = { 'VAR' : get_import_config }
			cmd     = 'echo $VAR'

	class DynAttrImportJobSh(Rule) :
		target  = 'dyn_attr_import_job_sh'
		environ = { 'VAR' : get_import_job }
		cmd     = 'echo $VAR'

	class DynAttrFromConfigSh(Rule) :
		target  = 'dyn_attr_from_config_sh'
		environ = { 'VAR' : get_from_config }
		cmd     = 'echo $VAR'

	if step==4 :
		class DynAttrImportConfigPy(PyRule) :
			target  = 'dyn_attr_import_config_py'
			environ = { 'VAR' : get_import_config }
			def cmd() :
				print(os.environ['VAR'])

	class DynAttrImportJobPy(PyRule) :
		target  = 'dyn_attr_import_job_py'
		environ = { 'VAR' : get_import_job }
		def cmd() :
			print(os.environ['VAR'])

	class DynAttrFromConfigPy(PyRule) :
		target  = 'dyn_attr_from_config_py'
		environ = { 'VAR' : get_from_config }
		def cmd() :
			print(os.environ['VAR'])

	class Chk(Rule) :
		target = r'{File:.*}.{Val:\d+}'
		dep    =  '{File}'
		cmd    = '[ $(cat) = {Val} ]'

else :

	from   lmake import multi_strip
	import ut

	print(multi_strip('''
		lcl_config_var=0
		def lcl_config_func() : return 1
	'''),file=open('lcl_config.py','w'))
	print(multi_strip('''
		lcl_job_var=2
		def lcl_job_func() : return 3
	'''),file=open('lcl_job.py','w'))

	print('step=2',file=open('step.py','w')) ; ut.lmake( no_ldump=True                                                     , rc=4 ) # job script is prepared in server, no local module access
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'cmd_import_job_sh'          , new=1 , failed=1 , early_rerun=... , rc=1 ) # .
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'cmd_from_config_sh.01'      ,         done  =2                          ) # ok as local module is inlined
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'cmd_import_config_py.01'    , new=1 , done  =2                          ) # job can import local module
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'cmd_import_job_py.23'       , new=1 , done  =2                          ) # .
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'cmd_from_config_py.01'      ,         done  =2                          ) # local module is inlined

	print('step=3',file=open('step.py','w')) ; ut.lmake( no_ldump=True                                                     , rc=4 ) # dynamic attribute is prepared in server, no local module access
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'dyn_attr_import_job_sh'     ,         failed=1 , early_rerun=... , rc=1 ) # .
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'dyn_attr_from_config_sh.01' ,         done  =2                          ) # ok as local module is inlined
	print('step=4',file=open('step.py','w')) ; ut.lmake( no_ldump=True                                                     , rc=4 ) # same for python
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'dyn_attr_import_job_py'     ,         failed=1 , early_rerun=... , rc=1 ) # .
	print('step=1',file=open('step.py','w')) ; ut.lmake( 'dyn_attr_from_config_py.01' ,         done  =2                          ) # .
