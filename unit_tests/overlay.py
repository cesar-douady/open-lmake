# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import lmake

if __name__!='__main__' :

	import os

	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'read/src'
	,	'write/dut.ref'
	,	'write/dut2.ref'
	,	'dut3.ref'
	)

	class Dut(Rule) :
		views   = { 'read_write' : {'upper':'write','lower':'read'} } # mount read_write as write on top of read
		targets = { 'DUT' : 'write/dut' }
		deps    = { 'SRC' : 'read/src'  }
		cmd     = 'cd read_write ; cp src dut'                           # a typical cmd that work in a dir rather than having inputs and outputs

	class Dut2(PyRule) :
		tmp_view = '/tmp'
		views    = { '/tmp/merged' : {'upper':'/tmp/upper','lower':'/usr/include','copy_up':'sys/'} } # create an overlay over a read-only dir
		target   = 'write/dut2'
		def cmd():
			import stat
			dir = '/tmp/merged/sys'                                                                      # a subdir that exists in /usr/include
			open(dir+'/testfile','w').write('good')
			print(open(dir+'/testfile').read())

	class Dut3(PyRule) :
		tmp_view = '/tmp'
		views    = { '/tmp/merged' : {'upper':'/tmp/upper','lower':'.'} }
		target   = 'dut3'
		def cmd() :
			print(open('/tmp/merged/read/src').read(),end='')

	class Test(Rule) :
		target = r'{Test:.*}.ok'
		deps = {
			'DUT' : '{Test}'
		,	'REF' : '{Test}.ref'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os

	import ut

	os.makedirs('read' ,exist_ok=True)
	os.makedirs('write',exist_ok=True)

	print('good',file=open('read/src','w'))

	print('good',file=open('write/dut.ref' ,'w'))
	print('good',file=open('write/dut2.ref','w'))
	print('good',file=open('dut3.ref'      ,'w'))

	ut.lmake( 'write/dut.ok' , 'write/dut2.ok' , 'dut3.ok'  , new=4 , done=6 )
