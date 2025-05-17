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
	,	'world'
	)

	class Cat(Rule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}

	class CatSh(Cat) :
		target = '{File1}+{File2}_sh'
		cmd    = 'cat {FIRST} {SECOND}'

	class CatPy(Cat,PyRule) :
		target = '{File1}+{File2}_py'
		def cmd() :
			for fn in (FIRST,SECOND) :
				with open(fn) as f : print(f.read(),end='')

	class Dut(Rule) :
		stderr_ok = True
		targets = { 'DUT':'dut' }
		deps = {
			'SH' : 'hello+world_sh'
		,	'PY' : 'hello+world_py'
		}
		cmd = '''
			ldepend {SH} {PY} # parallel deps
			echo dut > {DUT}
			echo stdout
			echo stderr >&2
		'''

else :

	import os
	import subprocess as sp

	import ut

	def lshow(key,*args) :
		x            = sp.check_output(('lshow',key[0],               *args),universal_newlines=True)
		long_x       = sp.check_output(('lshow',key[1],               *args),universal_newlines=True)
		porcelaine_x = sp.check_output(('lshow',key[1],'--porcelaine',*args),universal_newlines=True)
		assert x==long_x,('/'+x+'/','/'+long_x+'/')
		return x,eval(porcelaine_x)

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake() # init repo

	x,xp = lshow( ('-i','--info') , 'dut' )
	assert 'consider' in x and 'dut' in x
	assert xp['dut']==None

	ut.lmake( 'dut' , done=3 , new=2 )

	assert os.system('chmod -w -R .')==0 # check we can interrogate a read-only repo

	try :

		x      = sp.run(('lshow','-h'),stderr=sp.PIPE,universal_newlines=True).stderr
		long_x = sp.run(('lshow','-h'),stderr=sp.PIPE,universal_newlines=True).stderr
		assert x==long_x
		assert x.startswith('lshow ') and '--porcelaine' in x and 'man lshow' in x

		x = sp.run(('lshow','--version'),stderr=sp.PIPE,universal_newlines=True).stderr
		assert x.startswith('version ')

		x,px = lshow( ('-b','--bom') , 'hello+world_sh' , 'hello+world_py' )
		assert 'hello' in x and 'world' in x
		assert px=={'hello','world'}

		x,px = lshow( ('-b','--bom') , '--verbose' , 'hello+world_sh' )
		assert 'hello' in x and 'world' in x and 'hello+world_sh' in x
		assert set(px)=={'hello+world_sh','hello','world'} and px[0]=='hello+world_sh'

		x,px = lshow( ('-c','--cmd') , 'hello+world_sh' )
		e    = px['hello+world_sh']
		assert all( w in x for w in ('cat','hello','world') )
		assert all( w in e for w in ('cat','hello','world') )

		x,px = lshow( ('-d','--deps') ,        'hello+world_py' , 'hello+world_sh' )
		_,e  = lshow( ('-d','--deps') , '-J' , 'hello+world_py'                    )
		assert e == px['hello+world_py'][('CatPy','hello+world_py','generating')]
		assert all( w in x for w in ('hello+world_py','CatPy','hello+world_sh','CatSh','FIRST','hello','SECOND','world') )
		assert len(e)==2 and tuple(e[0])[0][2:]==('FIRST','hello') and tuple(e[1])[0][2:]==('SECOND','world')

		x,px = lshow( ('-d','--deps') , '-v' ,        'hello+world_py' )
		_,e  = lshow( ('-d','--deps') , '-v' , '-J' , 'hello+world_py' )
		assert e == px['hello+world_py'][('CatPy','hello+world_py','generating')]
		assert all( w in x for w in ('site-packages','FIRST','hello','SECOND','world') )
		assert len(e)==3 and 'site-package' in tuple(e[0])[0][3] and tuple(e[1])[0][2:]==('FIRST','hello') and tuple(e[2])[0][2:]==('SECOND','world')

		x,px = lshow( ('-d','--deps') , 'dut' )
		e    = px['dut'][('Dut','dut','generating')]
		assert all( w in x for w in ('/','\\','SH','hello+world_sh','PY','hello+world_py') )
		print(px)
		assert len(e)==1 and len(e[0])==2

		x,px = lshow( ('-E','--env') ,        'hello+world_sh' )
		_,e  = lshow( ('-E','--env') , '-J' , 'hello+world_sh' )
		assert e == px['hello+world_sh']
		assert all( w in x for w in ('HOME','$REPO_ROOT') )
		assert len(e)>=4 and all( k in e for k in ('HOME','PATH','UID','USER')) and e['HOME']=='$REPO_ROOT'

		x,px = lshow( ('-i','--info') ,        'hello+world_sh' )
		_,e  = lshow( ('-i','--info') , '-J' , 'hello+world_sh' )
		assert e == px['hello+world_sh']
		assert all(
			w in x for w in (
				'job'         , 'hello+world_sh'
			,	'ids'
			,	'required by' , 'dut'
			,	'reason'      , 'job was never run'
			,	'host'
			,	'scheduling'
			,	'autodep'
			,	'end date'
			,	'status'      , 'ok'
			,	'tmp dir'
			,	'rc'          , 'ok'
			,	'cpu time'
			,	'elapsed in job'
			,	'elapsed total'
			,	'used mem'
			,	'cost'
			,	'total size'
			)
		)
		assert e['job']=='hello+world_sh' and len(e['ids'])==3 and e['required by']=='dut' and e['reason']=='job was never run' and e['status']=='ok' and e['rc']=='ok'
		assert e['required resources']=={'cpu':1} and e['allocated resources']=={'cpu':1}

		x,px = lshow( ('-D','--inv-deps') , 'hello' )
		assert 'hello+world_sh' in x and 'hello+world_py' in x
		assert px['hello']=={ ('CatSh','hello+world_sh') , ('CatPy','hello+world_py') }

		x,px = lshow( ('-T','--inv-targets') , 'hello+world_sh' )
		assert 'CatSh' in x and 'hello+world_sh' in x
		assert px['hello+world_sh']=={ ('CatSh','hello+world_sh') }

		x,px = lshow( ('-r','--running') , 'hello+world_sh' )
		assert not x
		assert not px

		x,px = lshow( ('-e','--stderr') , 'dut' )
		assert x.strip()=='stderr'
		assert px['dut']=='stderr\n'

		x,px = lshow( ('-o','--stdout') , 'dut' )
		assert x.strip()=='stdout'
		assert px['dut']=='stdout\n'

		x,px = lshow( ('-t','--targets') , '-J' , 'dut' )
		assert 'DUT' in x and 'dut' in x
		assert all( e[2:]==('DUT','dut') for e in px )

		x,px = lshow( ('-u','--trace') , '-J' , 'hello+world_sh' )
		assert all(
			w in x for w in (
				'start_overhead' , 'start_info(reply)'
			,	'washed'
			,	'static_unlnk'   , 'hello+world_sh'
			,	'static_dep'     , 'hello' , 'world'
			,	'start_job'
			,	'open(read)'     , 'hello' , 'world'
			,	'end_job'        , '0000'
			,	'analyzed'
			,	'computed_crcs'
			,	'end_overhead'   , 'ok'
			)
		)
		y = tuple( e[1:] for e in px if e[1] not in ('static_dep','static_unlnk') )
		z =      { e[1:] for e in px if e[1]     in ('static_dep','static_unlnk') }
		assert y==(
			( 'start_overhead'    , ''      )
		,	( 'start_info(reply)' , ''      )
		,	( 'washed'            , ''      )
		,	( 'start_job'         , ''      )
		,	( 'open(read)'        , 'hello' )
		,	( 'open(read)'        , 'world' )
		,	( 'end_job'           , '0000'  )
		,	( 'analyzed'          , ''      )
		,	( 'computed_crcs'     , ''      )
		,	( 'end_overhead'      , 'ok'    )
		)
		assert all( e in z for e in (
			( 'static_unlnk' , 'hello+world_sh' )
		,	( 'static_dep'   , 'hello'          )
		,	( 'static_dep'   , 'world'          )
		) )

	except  : raise
	finally : assert os.system('chmod u+w -R .')==0 # restore state
