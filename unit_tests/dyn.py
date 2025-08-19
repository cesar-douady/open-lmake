# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import os

	from lmake.rules import Rule,PyRule
	from lmake.rules import python as system_python

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'local_module.py'
	,	'hello'
	,	'world'
	,	'deps.hello+world.ref'
	,	'interpreter.hello+world.ref'
	,	'env.hello.ref'
	,	*(f'autodep.{ad}.ref' for ad in lmake.autodeps)
	,	'resources.1.ref'
	,	'resources.2.ref'
	,	'auto_mkdir.no.ref'
	,	'auto_mkdir.yes.ref'
	)

	from step         import step
	from local_module import local_func

	class Cmp(Rule) :
		target = r'{File:.*}.ok'
		deps = {
			'DUT' : '{File}'
		,	'REF' : '{File}.ref'
		}
		cmd = 'diff {REF} {DUT}'

	def file_func () :
		if step==1 : raise RuntimeError
		return File
	def file2_func() :
		if step==1 : raise RuntimeError
		return File2

	class Deps(PyRule) :
		stems = {
			'File1' : r'\w*'
		,	'File2' : r'\w*'
		}
		target = 'deps.{File1}+{File2}'
		def deps() :
			if step==1 : raise RuntimeError
			return {
				'FIRST'  : File1
			,	'SECOND' : file2_func()
			}
		def cmd() :
			print(open(deps['FIRST' ]).read(),end='')
			print(open(deps['SECOND']).read(),end='')

	class Interpreter(PyRule) :
		stems = {
			'File1' : r'\w*'
		,	'File2' : r'\w*'
		}
		target = 'interpreter.{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		def python() :
			if step==1 : raise RuntimeError
			return (system_python,)
		def cmd() :
			print(open(deps['FIRST' ]).read(),end='')
			print(open(deps['SECOND']).read(),end='')

	class Env(PyRule) :
		target = r'env.{File:\w*}'
		environ           = { 'VAR'           : file_func }
		environ_resources = { 'VAR_RESOURCES' : file_func }
		environ_ancillary = { 'VAR_ANCILLARY' : file_func }
		def cmd() :
			print(os.environ['VAR'          ])
			print(os.environ['VAR_RESOURCES'])
			print(os.environ['VAR_ANCILLARY'])

	class StartDelay(Rule) :
		target = r'start_delay.{File:\w*}'
		force  = True
		def start_delay() :
			if step==1 : raise RuntimeError
			return File=='yes'
		cmd = ''

	class Autodep(Rule) :
		target = r'autodep.{File:\w*}'
		def autodep() :
			if step==1 : raise RuntimeError
			return File
		cmd = 'cat hello'

	class Resources(Rule) :
		target = r'resources.{File:\w*}'
		def resources() :
			if step==1 : raise RuntimeError
			return {'cpu':local_func(File)}
		cmd = "echo {resources['cpu']}"

	class StderrLen(Rule) :
		target = r'max_stderr_len.{File:\w*}'
		force  = True
		def max_stderr_len() :
			if step==1 : raise RuntimeError
			return File
		cmd = ''

	class StderrOk(Rule) :
		target = r'stderr_ok.{File:\w*}'
		def stderr_ok() :
			if step==1 : raise RuntimeError
			return File=='yes'
		cmd = 'echo hello >&2'

	class AutoMkdir(Rule) :
		target    = r'auto_mkdir.{File:\w*}'
		stderr_ok = True
		def auto_mkdir() :
			if step==1 : raise RuntimeError
			return File=='yes'
		cmd = 'cd auto ; cat ../hello ; :'

	class Cmd(Rule) :
		target = 'cmd'
		deps   = { 'HELLO' : 'hello' }
		if step==1 : cmd = 'cat {HELLO} ; echo {bad}'
		else       : cmd = 'cat {HELLO} ; echo {step}'

else :

	import textwrap

	import ut

	print('hello'              ,file=open('hello'                      ,'w'))
	print('world'              ,file=open('world'                      ,'w'))
	print('hello\nworld'       ,file=open('deps.hello+world.ref'       ,'w'))
	print('hello\nworld'       ,file=open('interpreter.hello+world.ref','w'))
	print('hello\nhello\nhello',file=open('env.hello.ref'              ,'w'))
	print(1                    ,file=open('resources.1.ref'            ,'w'))
	print(2                    ,file=open('resources.2.ref'            ,'w'))
	print('hello'              ,file=open('auto_mkdir.yes.ref'         ,'w'))
	#
	for ad in lmake.autodeps : print('hello',file=open(f'autodep.{ad}.ref','w'))
	open('auto_mkdir.no.ref','w')
	#
	print(textwrap.dedent('''
		def local_func(f) : return f
	'''[1:]),file=open('local_module.py','w')) # strip initial \n

	print(f'step=0',file=open('step.py','w'))
	ut.lmake( 'cmd' , done=1 , new=1 )        # create file cmd to ensure transition bad->good does not leave a manual file

	for s in (1,2) :
		print(f'step={s}',file=open('step.py','w'))
		rc = 1 if s==1 else 0
		#
		ut.lmake( 'deps.hello+world.ok'        , done=2-rc*2 , steady=0    , failed=0  , new=1-rc , deps_not_avail=rc          , rc=rc )
		ut.lmake( 'interpreter.hello+world.ok' , done=2-rc*2 , steady=0    , failed=rc , new=3*rc , early_rerun=rc  ,            rc=rc ) # interpreter is a dep
		ut.lmake( 'env.hello.ok'               , done=2-rc*2 , steady=0    , failed=rc , new=rc   , early_rerun=rc  ,            rc=rc )
		ut.lmake( 'start_delay.no'             , done=rc     , steady=1-rc , failed=0  , new=0    , early_rerun=rc  , start=1  , rc=0  )
		ut.lmake( 'start_delay.yes'            , done=rc     , steady=1-rc , failed=0  , new=0    , early_rerun=rc  , start=rc , rc=0  )
		ut.lmake( 'resources.1.ok'             , done=2-rc*2 , steady=0    , failed=rc , new=rc   ,                              rc=rc )
		ut.lmake( 'resources.2.ok'             , done=2-rc*2 , steady=0    , failed=rc , new=rc   ,                              rc=rc )
		ut.lmake( 'max_stderr_len.1'           , done=rc     , steady=1-rc , failed=0  , new=0    , early_rerun=rc  ,            rc=0  )
		ut.lmake( 'max_stderr_len.2'           , done=rc     , steady=1-rc , failed=0  , new=0    , early_rerun=rc  ,            rc=0  )
		ut.lmake( 'stderr_ok.no'               , done=0      , steady=0    , failed=1  , new=0    , early_rerun=rc  ,            rc=1  )
		ut.lmake( 'stderr_ok.yes'              , done=1-rc   , steady=0    , failed=rc , new=0    , early_rerun=rc  ,            rc=rc )
		ut.lmake( 'auto_mkdir.no.ok'           , done=2-rc*2 , steady=0    , failed=rc , new=rc   , early_rerun=rc  ,            rc=rc )
		ut.lmake( 'auto_mkdir.yes.ok'          , done=2-rc*2 , steady=0    , failed=rc , new=rc   , early_rerun=rc  ,            rc=rc )
		ut.lmake( 'cmd'                        , done=1-rc   , steady=0    , failed=rc , new=0    , early_rerun=... ,            rc=rc ) # python3.6 accesses file <code> to report backtrace, ...
		#                                                                                                                                # ... pytho3.12 does not
		for ad in lmake.autodeps : ut.lmake( f'autodep.{ad}.ok' , done=2-rc*2 , steady=0 , failed=rc , new=rc , early_rerun=rc , rc=rc )
