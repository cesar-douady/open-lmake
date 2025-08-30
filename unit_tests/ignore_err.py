# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	from step import step

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	,	'hello'
	,	'world'
	)

	class Bad(Rule) :
		target = 'bad'
		if step==2 : cmd = 'exit 0'
		else       : cmd = 'exit 1'

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  :   '{File1}'
		,	'SECOND' : ( '{File2}' , 'IgnoreError' )
		}
		cmd = 'cat {FIRST} {SECOND}'

	class DepVerboseSh(Rule) :
		target = 'dep_verbose_sh'
		# ensure cmd is independent of step, so dont check status
		cmd = '''
				from_server="$(ldepend -e -v bad)"
				[ "$from_server" = "ok empty-R bad"    ] \
			||	[ "$from_server" = "error empty-R bad" ]
		'''

	class DepVerbosePy(PyRule) :
		target = 'dep_verbose_py'
		def cmd() :
			from_server = lmake.depend('bad',ignore_error=True,verbose=True)
			assert (                                                                          # ensure cmd is independent of step, so dont check status
				from_server=={ 'bad' : { 'ok':True  , 'checksum':'empty-R' } }
			or	from_server=={ 'bad' : { 'ok':False , 'checksum':'empty-R' } }
			)

else :

	import ut

	print('step=1',file=open('step.py','w'))

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+bad' , new=1 , failed=1 , done=1      ) # check this is ok
	ut.lmake( 'bad+world' , new=1 ,                   rc=1 ) # check this is not

	ut.lmake( 'dep_verbose_sh' , 'dep_verbose_py' , done=2 )

	print('step=2',file=open('step.py','w'))

	ut.lmake( 'dep_verbose_sh' , 'dep_verbose_py' , steady=3 ) # check dep_verbose_sh is remade although dep content is not modified
