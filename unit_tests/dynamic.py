# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

import lmake

autodeps = []
if lmake.has_ptrace     : autodeps.append('ptrace'    )
if lmake.has_ld_audit   : autodeps.append('ld_audit'  )
if lmake.has_ld_preload : autodeps.append('ld_preload')

if getattr(sys,'lmake_read_makefiles',False) :

	import os

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	,	'hello'
	,	'world'
	,	'deps.hello+world.ref'
	,	'env.hello.ref'
	,	*(f'autodep.{ad}.ref' for ad in autodeps)
	,	'resources.1.ref'
	,	'resources.2.ref'
	,	'auto_mkdir.no.ref'
	,	'auto_mkdir.yes.ref'
	)

	from step import step

	class Cmp(lmake.Rule) :
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

	class Deps(lmake.Rule) :
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

	class Env(lmake.Rule) :
		target = r'env.{File:\w*}'
		environ_cmd       = { 'VAR_CMD'       : file_func }
		environ_resources = { 'VAR_RESOURCES' : file_func }
		environ_ancillary = { 'VAR_ANCILLARY' : file_func }
		def cmd() :
			print(os.environ['VAR_CMD'      ])
			print(os.environ['VAR_RESOURCES'])
			print(os.environ['VAR_ANCILLARY'])

	class StartDelay(lmake.Rule) :
		target = r'start_delay.{File:\w*}'
		force  = True
		def start_delay() :
			if step==1 : raise RuntimeError
			return File=='yes'
		cmd = ''

	class Autodep(lmake.Rule) :
		target = r'autodep.{File:\w*}'
		def autodep() :
			if step==1 : raise RuntimeError
			return File
		cmd = 'cat hello'

	class Resources(lmake.Rule) :
		target    = r'resources.{File:\w*}'
		resources = { 'cpu' : file_func }
		cmd       = 'echo {cpu}'

	class StderrLen(lmake.Rule) :
		target = r'stderr_len.{File:\w*}'
		force  = True
		def stderr_len() :
			if step==1 : raise RuntimeError
			return File
		cmd = ''

	class AllowStderr(lmake.Rule) :
		target = r'allow_stderr.{File:\w*}'
		def allow_stderr() :
			if step==1 : raise RuntimeError
			return File=='yes'
		cmd = 'echo hello >&2'

	class AutoMkdir(lmake.Rule) :
		target = r'auto_mkdir.{File:\w*}'
		allow_stderr = True
		def auto_mkdir() :
			if step==1 : raise RuntimeError
			return File=='yes'
		cmd = 'cd auto ; cat ../hello ; :'

	class Cmd(lmake.Rule) :
		target = 'cmd'
		deps = { 'HELLO' : 'hello' }
		if step==1 : cmd = 'cat {HELLO} ; echo {bad}'
		else       : cmd = 'cat {HELLO} ; echo {step}'


else :

	import ut

	print('hello'              ,file=open('hello'               ,'w'))
	print('world'              ,file=open('world'               ,'w'))
	print('hello\nworld'       ,file=open('deps.hello+world.ref','w'))
	print('hello\nhello\nhello',file=open('env.hello.ref'       ,'w'))
	print(1                    ,file=open('resources.1.ref'     ,'w'))
	print(2                    ,file=open('resources.2.ref'     ,'w'))
	print('hello'              ,file=open('auto_mkdir.yes.ref'  ,'w'))
	#
	for ad in autodeps : print('hello',file=open(f'autodep.{ad}.ref','w'))
	open('auto_mkdir.no.ref','w')

	print(f'step=0',file=open('step.py','w'))
#	ut.lmake( 'cmd' , done=1 , new=1 )                                         # create file cmd to ensure transition bad->good does not leave a manual file

#	for s in (1,2) :
	for s in (1,) :
		print(f'step={s}',file=open('step.py','w'))
		rc = 1 if s==1 else 0
		#
#		ut.lmake( 'deps.hello+world.ok' , done=2-rc*2 , steady=0    , failed=0  , new=2-rc*2 , no_deps=rc   , rc=rc )
		ut.lmake( 'env.hello.ok'        , done=2-rc*2 , steady=0    , failed=rc , new=rc     ,                rc=rc )
#		ut.lmake( 'start_delay.no'      , done=rc     , steady=1-rc , failed=0  , new=0      , start=1      , rc=0  )
#		ut.lmake( 'start_delay.yes'     , done=rc     , steady=1-rc , failed=0  , new=0      , start=rc     , rc=0  )
#		ut.lmake( 'resources.1.ok'      , done=2-rc*2 , steady=0    , failed=rc , new=rc     ,                rc=rc )
#		ut.lmake( 'resources.2.ok'      , done=2-rc*2 , steady=0    , failed=rc , new=rc     ,                rc=rc )
#		ut.lmake( 'stderr_len.1'        , done=rc     , steady=1-rc , failed=0  , new=0      ,                rc=0  )
#		ut.lmake( 'stderr_len.2'        , done=rc     , steady=1-rc , failed=0  , new=0      ,                rc=0  )
#		ut.lmake( 'allow_stderr.no'     , done=0      , steady=0    , failed=1  , new=0      ,                rc=1  )
#		ut.lmake( 'allow_stderr.yes'    , done=0      , steady=1-rc , failed=rc , new=0      ,                rc=rc )
#		ut.lmake( 'auto_mkdir.no.ok'    , done=2-rc*2 , steady=0    , failed=rc , new=rc     ,                rc=rc )
#		ut.lmake( 'auto_mkdir.yes.ok'   , done=2-rc*2 , steady=0    , failed=rc , new=rc     ,                rc=rc )
#		ut.lmake( 'cmd'                 , done=1-rc   , steady=0    , failed=rc , new=0      ,                rc=rc )
#		#
#		for ad in autodeps : ut.lmake( f'autodep.{ad}.ok' , done=2-rc*2 , steady=0 , failed=rc , new=rc , rc=rc )
