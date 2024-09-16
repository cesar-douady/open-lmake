# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import Rule,PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'read/src'
	,	'ref'
	,	'ref2'
	)

	class Dut(Rule) :
		views   = { 'read_write/' : {'upper':'write/','lower':'read/'} } # mount read_write as write on top of read
		targets = { 'DUT' : 'write/dut' }
		deps    = { 'SRC' : 'read/src'  }
		cmd     = 'cd read_write ; cp src dut'                           # a typical cmd that work in a dir rather than having inputs and outputs

	class Dut2(PyRule) :
		tmp_view = '/tmp'
		views    = { '/tmp/merged/' : {'upper':'/tmp/upper/','lower':'/usr/include/','copy_up':'sys/'} } # create an overlay over a read-only dir
		target   = 'write/dut2'
		def cmd():
			import stat
			dir = '/tmp/merged/sys'                                                                      # a subdir that exists in /usr/include
			open(dir+'/testfile','w').write('good')
			print(open(dir+'/testfile').read())

	class Test(Rule) :
		target = 'test{Test:.*}'
		deps = {
			'DUT' : 'write/dut{Test}'
		,	'REF' : 'ref{Test}'
		}
		cmd = 'diff {REF} {DUT}'

else :

	import os

	import ut

	os.makedirs('read',exist_ok=True)

	print('good',file=open('ref'     ,'w'))
	print('good',file=open('read/src','w'))
	print('good',file=open('ref2'    ,'w'))

	ut.lmake( 'test'  , new=2 , done=2 )
	ut.lmake( 'test2' , new=1 , done=2 )
